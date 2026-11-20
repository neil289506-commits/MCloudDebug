#include "CopilotClient.h"

#include "AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>

namespace {

// Windows 的命令列總長上限是 32767 個字元；預留給其他參數
constexpr int kMaxPromptChars = 30000;

QString existingFile(const QString &path)
{
    const QFileInfo fi(path);
    return fi.isFile() ? fi.absoluteFilePath() : QString();
}

// npm 全域安裝在 Windows 會留下 copilot.cmd / copilot.ps1 外殼。為了不經過 cmd.exe（避免提示詞被 shell 解析），
// 直接找外殼旁邊 node_modules 裡真正的平台執行檔；找不到才退而用 node 執行 npm-loader.js（同樣不經 shell）。
CopilotClient::Launcher resolveShim(const QFileInfo &shim)
{
    const QDir dir = shim.absoluteDir();
    const bool arm = QSysInfo::currentCpuArchitecture().contains(QLatin1String("arm"), Qt::CaseInsensitive);
    const QStringList archs = arm ? QStringList{QStringLiteral("arm64"), QStringLiteral("x64")}
                                  : QStringList{QStringLiteral("x64"), QStringLiteral("arm64")};
    for (const QString &a : archs) {
        const QString base = dir.filePath(QStringLiteral("node_modules/@github/copilot-win32-") + a);
        for (const QString &exe : {QStringLiteral("copilot.exe"), QStringLiteral("copilot")}) {
            const QString f = existingFile(base + QLatin1Char('/') + exe);
            if (!f.isEmpty())
                return {f, {}};
        }
    }
    const QString loader = existingFile(dir.filePath(QStringLiteral("node_modules/@github/copilot/npm-loader.js")));
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!loader.isEmpty() && !node.isEmpty())
        return {node, {loader}};
    return {};
}

CopilotClient::Launcher fromPath(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile())
        return {};
    const QString suffix = fi.suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat") || suffix == QLatin1String("ps1"))
        return resolveShim(fi);
    return {fi.absoluteFilePath(), {}};
}

} // namespace

CopilotClient::CopilotClient(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (!m_proc)
            return;
        m_timedOut = true;
        killTree();
    });
}

QString CopilotClient::workDir()
{
    // 空資料夾：CLI 的工作目錄。不放任何專案檔、AGENTS.md，避免它讀到不相干的東西。
    const QString d = App::dataDir() + QStringLiteral("/copilot-work");
    QDir().mkpath(d);
    return d;
}

CopilotClient::Launcher CopilotClient::locate(const QString &userPath)
{
    if (!userPath.trimmed().isEmpty())
        return fromPath(userPath.trimmed());

    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("copilot"));
    if (!onPath.isEmpty()) {
        const Launcher l = fromPath(onPath);
        if (l.ok())
            return l;
    }
#ifdef Q_OS_WIN
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString appData = qEnvironmentVariable("APPDATA");
    QStringList candidates;
    if (!localAppData.isEmpty())
        candidates << localAppData + QStringLiteral("/Microsoft/WinGet/Links/copilot.exe");
    if (!appData.isEmpty())
        candidates << appData + QStringLiteral("/npm/copilot.cmd");
    for (const QString &c : std::as_const(candidates)) {
        const Launcher l = fromPath(c);
        if (l.ok())
            return l;
    }
#endif
    return {};
}

QString CopilotClient::composePrompt(const QJsonArray &messages)
{
    QString system;
    QString convo;
    for (const QJsonValue &v : messages) {
        const QJsonObject o = v.toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        const QString content = o.value(QStringLiteral("content")).toString();
        if (role == QLatin1String("system"))
            system += content;
        else
            convo += QStringLiteral("\n[%1]\n%2\n").arg(role == QLatin1String("assistant") ? QStringLiteral("Assistant")
                                                                                           : QStringLiteral("User"),
                                                       content);
    }
    // CLI 沒有獨立的 system 欄位，所以把系統提示詞放在最前面，並明確指定要回答哪一則
    return system + QStringLiteral("\n\n--- Conversation ---") + convo
           + QStringLiteral("\n--- End ---\nAnswer only the last [User] message. Do not modify files or run commands.");
}

void CopilotClient::send(const ProviderConfig &cfg, const QJsonArray &messages)
{
    cancel();
    m_canceled = false;
    m_timedOut = false;
    m_gotOutput = false;
    m_err.clear();
    m_decoder = QStringDecoder(QStringDecoder::Utf8);

    const Launcher l = locate(cfg.cliPath);
    if (!l.ok()) {
        emit failed(tr("找不到 Copilot CLI。請先安裝（npm install -g @github/copilot），或到「設定 → GitHub Copilot」指定 copilot 的路徑。"));
        return;
    }

    const QString prompt = composePrompt(messages);
#ifdef Q_OS_WIN
    if (prompt.size() > kMaxPromptChars) {
        emit failed(tr("提示詞共 %1 字元，超過 Windows 命令列長度上限（約 %2）。請在左側調低「最大字元數」，"
                       "或重新開始分析（追問會累積先前的回答）。").arg(prompt.size()).arg(kMaxPromptChars));
        return;
    }
#endif

    QStringList args = l.prefixArgs;
    args << QStringLiteral("-p") << prompt                   // 非互動：跑完就結束
         << QStringLiteral("-s")                             // 只輸出回答本文（不含統計與裝飾）
         << QStringLiteral("--stream") << QStringLiteral("on")
         << QStringLiteral("--no-ask-user")                  // 不要停下來問使用者
         << QStringLiteral("--no-custom-instructions")       // 不載入 AGENTS.md 等自訂指示
         << QStringLiteral("--no-auto-update")
         << QStringLiteral("--no-color");
    if (!cfg.model.trimmed().isEmpty())
        args << QStringLiteral("--model") << cfg.model.trimmed();
    if (!cfg.reasoningEffort.isEmpty())
        args << QStringLiteral("--reasoning-effort") << cfg.reasoningEffort;

    // 權限：deny 規則優先於所有 allow（含 --allow-all-tools），所以 shell／寫檔／記憶／GitHub MCP 一律封鎖。
    args << QStringLiteral("--deny-tool=shell") << QStringLiteral("--deny-tool=write")
         << QStringLiteral("--deny-tool=memory") << QStringLiteral("--deny-tool=github-mcp-server");
    if (cfg.cliAllowTools) // 只為了網路搜尋／抓網頁；非互動模式沒有 allow 規則的工具請求會被直接拒絕
        args << QStringLiteral("--allow-all-tools") << QStringLiteral("--allow-all-urls");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COPILOT_AUTO_UPDATE"), QStringLiteral("false"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.remove(QStringLiteral("COPILOT_ALLOW_ALL")); // 不接受外部環境變數把權限全開
    if (!cfg.apiKey.isEmpty())
        env.insert(QStringLiteral("COPILOT_GITHUB_TOKEN"), cfg.apiKey); // 沒存 Token 就沿用 copilot login 的登入
    if (!cfg.autoTier.isEmpty())
        env.insert(QStringLiteral("COPILOT_AUTO_TIER"), cfg.autoTier);

    auto *proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(workDir());
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    // 取消後立刻重送時，舊行程的訊號可能晚到；一律以「目前這個行程」為準，舊的直接忽略
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        if (m_proc.data() != proc)
            return;
        const QByteArray chunk = proc->readAllStandardOutput();
        if (chunk.isEmpty())
            return;
        armIdleTimer();
        const QString text = m_decoder(chunk);
        if (!text.isEmpty()) {
            m_gotOutput = true;
            emit contentDelta(text);
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        if (m_proc.data() != proc)
            return;
        const QByteArray chunk = proc->readAllStandardError();
        armIdleTimer();
        if (m_err.size() < 65536)
            m_err += chunk;
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (m_proc.data() != proc) {
            proc->deleteLater();
            return;
        }
        m_idle.stop();
        proc->deleteLater();
        m_proc.clear();
        emit failed(tr("無法啟動 Copilot CLI：%1").arg(proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus st) {
        if (m_proc.data() != proc) {
            proc->deleteLater();
            return;
        }
        m_idle.stop();
        proc->deleteLater();
        m_proc.clear();
        if (m_canceled)
            return;
        if (m_timedOut) {
            emit failed(tr("Copilot 太久沒有回應，已中止。"));
            return;
        }
        const QString err = QString::fromUtf8(m_err).trimmed();
        if (st == QProcess::CrashExit || code != 0) {
            emit failed(err.isEmpty() ? tr("Copilot CLI 異常結束（代碼 %1）。").arg(code) : err.left(1500));
            return;
        }
        if (!m_gotOutput) {
            emit failed(err.isEmpty() ? tr("Copilot 沒有回應任何內容。") : err.left(1500));
            return;
        }
        emit finished();
    });

    armIdleTimer();
    proc->start(l.program, args);
}

void CopilotClient::armIdleTimer()
{
    // 「多久沒收到任何輸出」才算逾時；搜尋與推理期間可能很久沒有輸出
    m_idle.start(10 * 60 * 1000);
}

bool CopilotClient::busy() const
{
    return !m_proc.isNull();
}

void CopilotClient::cancel()
{
    if (!m_proc)
        return;
    m_canceled = true;
    m_idle.stop();
    killTree();
}

void CopilotClient::killTree()
{
    if (!m_proc)
        return;
#ifdef Q_OS_WIN
    // 以 node 執行 npm-loader.js 時，真正的 CLI 是它的子行程；QProcess::kill 只會終止父行程
    const qint64 pid = m_proc->processId();
    if (pid > 0)
        QProcess::execute(QStringLiteral("taskkill"), {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
#endif
    m_proc->kill();
}
