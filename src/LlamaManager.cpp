#include "LlamaManager.h"

#include "AppSettings.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpServer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const QString kRepoBase = QStringLiteral("https://github.com/ggml-org/llama.cpp/releases");

void hideConsole(QProcess &p)
{
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
        a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        a->startupInfo->wShowWindow = SW_HIDE;
    });
#else
    Q_UNUSED(p)
#endif
}
} // namespace

LlamaManager::LlamaManager(QObject *parent)
    : QObject(parent)
{
    hideConsole(m_proc);
    m_proc.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_dl, &Downloader::progress, this, [this](qint64 got, qint64 total) {
        emit progress(got, total, m_jobs.isEmpty() ? QString() : m_jobs.first().label);
    });
    connect(&m_dl, &Downloader::done, this, [this](bool ok, const QString &err) {
        if (!ok) {
            finishTask(false, err);
            return;
        }
        const Job job = m_jobs.takeFirst();
        if (job.file.endsWith(QLatin1String(".zip")))
            m_zips << job.file;
        if (m_jobs.isEmpty()) {
            if (m_zips.isEmpty())
                finishTask(true, tr("下載完成"));
            else
                extractAll();
        } else {
            downloadNext();
        }
    });

    connect(&m_proc, &QProcess::readyRead, this, [this] {
        const QString out = QString::fromUtf8(m_proc.readAll());
        for (const QString &l : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            m_lastLog = l.trimmed();
            emit serverLog(m_lastLog);
        }
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit status(tr("無法啟動 llama-server.exe"));
            setState(State::Stopped);
        }
    });
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                m_healthTimer.stop();
                if (!m_stopRequested)
                    emit status(tr("llama-server 已結束（代碼 %1）：%2").arg(code).arg(m_lastLog));
                setState(State::Stopped);
            });

    m_healthTimer.setInterval(1000);
    connect(&m_healthTimer, &QTimer::timeout, this, &LlamaManager::pollHealth);
}

LlamaManager::~LlamaManager()
{
    m_stopRequested = true;
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(2000);
    }
}

QString LlamaManager::rootDir()
{
    const QString d = App::dataDir() + QStringLiteral("/llama");
    QDir().mkpath(d);
    return d;
}

QString LlamaManager::serverExe() const
{
    QDirIterator it(rootDir() + QStringLiteral("/bin"), {QStringLiteral("llama-server.exe")}, QDir::Files,
                    QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

QString LlamaManager::installedTag() const
{
    return App::settings().value(QStringLiteral("local/tag")).toString();
}

QUrl LlamaManager::modelUrl() const
{
    const QString s = App::settings().value(QStringLiteral("local/modelUrl")).toString().trimmed();
    return QUrl(s.isEmpty() ? QString::fromLatin1(kDefaultModelUrl) : s);
}

QString LlamaManager::modelPath() const
{
    QString name = modelUrl().fileName();
    if (name.isEmpty())
        name = QStringLiteral("model.gguf");
    return rootDir() + QStringLiteral("/models/") + name;
}

bool LlamaManager::modelInstalled() const
{
    return QFileInfo(modelPath()).size() > 100 * 1024 * 1024;
}

bool LlamaManager::modelPartial() const
{
    return QFileInfo::exists(modelPath() + QStringLiteral(".part"));
}

QUrl LlamaManager::chatEndpoint() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(m_port));
}

void LlamaManager::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void LlamaManager::setBusy(bool b)
{
    m_busy = b;
}

void LlamaManager::finishTask(bool ok, const QString &msg)
{
    setBusy(false);
    m_jobs.clear();
    m_zips.clear();
    emit taskFinished(ok, msg);
}

void LlamaManager::cancelTask()
{
    if (m_tagReply)
        m_tagReply->abort();
    m_dl.cancel();
}

// ---------------------------------------------------------------------------
// 安裝 llama.cpp
//
// ggml-org/llama.cpp 的 releases/latest 目前指向一個「指標 release」（只含 nightly-tag.txt），
// 真正的版本標籤（例如 b12359）寫在那個檔案裡。所以流程是：
//   1. HEAD releases/latest（不跟轉址）→ 取得指標標籤
//   2. 下載 <指標>/nightly-tag.txt → 真正標籤；若格式不對就直接用指標標籤（相容舊做法）
//   3. 組出 llama-<tag>-bin-win-<backend>-x64.zip
// ---------------------------------------------------------------------------
void LlamaManager::installServer(Backend backend)
{
    if (m_busy)
        return;
    setBusy(true);
    m_backend = backend;
    m_jobs.clear();
    m_zips.clear();
    emit status(tr("查詢 llama.cpp 最新版本…"));
    resolveTag();
}

void LlamaManager::resolveTag()
{
    QNetworkRequest req{QUrl(kRepoBase + QStringLiteral("/latest"))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MCDebugger/1.0"));
    req.setTransferTimeout(20000);
    m_tagReply = m_nam.head(req);
    connect(m_tagReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_tagReply;
        r->deleteLater();
        const QString loc = r->header(QNetworkRequest::LocationHeader).toUrl().toString();
        const QString pointer = loc.section(QLatin1Char('/'), -1);
        if (pointer.isEmpty()) {
            finishTask(false, tr("無法取得 llama.cpp 版本：%1").arg(r->errorString()));
            return;
        }
        emit status(tr("取得版本標籤…"));
        QNetworkRequest r2{QUrl(kRepoBase + QStringLiteral("/download/") + pointer + QStringLiteral("/nightly-tag.txt"))};
        r2.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        r2.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MCDebugger/1.0"));
        r2.setTransferTimeout(20000);
        m_tagReply = m_nam.get(r2);
        connect(m_tagReply, &QNetworkReply::finished, this, [this, pointer] {
            QNetworkReply *rr = m_tagReply;
            rr->deleteLater();
            QString tag = QString::fromUtf8(rr->readAll()).trimmed();
            static const QRegularExpression ok(QStringLiteral(R"(^b\d+$)"));
            if (rr->error() != QNetworkReply::NoError || !ok.match(tag).hasMatch())
                tag = pointer;
            m_tag = tag;

            const QString base = kRepoBase + QStringLiteral("/download/") + m_tag + QLatin1Char('/');
            const QString dl = rootDir() + QStringLiteral("/downloads/");
            auto add = [&](const QString &name) {
                m_jobs.append({QUrl(base + name), dl + name, name});
            };
            switch (m_backend) {
            case Backend::Vulkan: add(QStringLiteral("llama-%1-bin-win-vulkan-x64.zip").arg(m_tag)); break;
            case Backend::Cpu: add(QStringLiteral("llama-%1-bin-win-cpu-x64.zip").arg(m_tag)); break;
            case Backend::Cuda:
                add(QStringLiteral("llama-%1-bin-win-cuda-12.4-x64.zip").arg(m_tag));
                add(QStringLiteral("cudart-llama-bin-win-cuda-12.4-x64.zip")); // CUDA 執行階段 DLL
                break;
            }
            downloadNext();
        });
    });
}

void LlamaManager::downloadNext()
{
    const Job &j = m_jobs.first();
    emit status(tr("下載 %1 …").arg(j.label));
    m_dl.start(j.url, j.file, j.file.endsWith(QLatin1String(".gguf")));
}

QString LlamaManager::powershellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

void LlamaManager::extractAll()
{
    stopServer(); // 檔案被占用時無法覆蓋
    const QString bin = rootDir() + QStringLiteral("/bin");
    QDir(bin).removeRecursively();
    QDir().mkpath(bin);
    emit status(tr("解壓縮中…"));

    // 一次 PowerShell 解完所有 zip（Windows 內建 Expand-Archive，不需額外相依）
    QStringList cmds;
    for (const QString &z : std::as_const(m_zips))
        cmds << QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                    .arg(powershellQuote(QDir::toNativeSeparators(z)), powershellQuote(QDir::toNativeSeparators(bin)));
    auto *p = new QProcess(this);
    hideConsole(*p);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int code, QProcess::ExitStatus) {
                const QString out = QString::fromLocal8Bit(p->readAll()).trimmed();
                p->deleteLater();
                if (code != 0 || serverExe().isEmpty()) {
                    finishTask(false, tr("解壓縮失敗（%1）：%2").arg(code).arg(out.left(300)));
                    return;
                }
                for (const QString &z : std::as_const(m_zips))
                    QFile::remove(z);
                App::settings().setValue(QStringLiteral("local/tag"), m_tag);
                App::settings().setValue(QStringLiteral("local/backend"), static_cast<int>(m_backend));
                finishTask(true, tr("llama.cpp %1 安裝完成").arg(m_tag));
            });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            finishTask(false, tr("無法執行 PowerShell 進行解壓縮"));
        }
    });
    p->start(QStringLiteral("powershell.exe"),
             {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
              QStringLiteral("-Command"), cmds.join(QStringLiteral("; "))});
}

void LlamaManager::downloadModel()
{
    if (m_busy)
        return;
    setBusy(true);
    m_jobs.clear();
    m_zips.clear();
    m_jobs.append({modelUrl(), modelPath(), modelUrl().fileName()});
    downloadNext();
}

// ---------------------------------------------------------------------------
// 啟動 / 停止 llama-server
// ---------------------------------------------------------------------------
void LlamaManager::startServer(int contextSize, int gpuLayers)
{
    if (m_state != State::Stopped)
        return;
    const QString exe = serverExe();
    if (exe.isEmpty() || !modelInstalled()) {
        emit status(tr("尚未安裝 llama.cpp 或模型"));
        return;
    }
    QTcpServer probe; // 找一個空閒連接埠
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        emit status(tr("找不到可用的連接埠"));
        return;
    }
    m_port = probe.serverPort();
    probe.close();

    m_stopRequested = false;
    m_healthTries = 0;
    m_lastLog.clear();
    m_proc.setWorkingDirectory(QFileInfo(exe).absolutePath()); // DLL 與 exe 同目錄
    m_proc.start(exe, {QStringLiteral("-m"), QDir::toNativeSeparators(modelPath()),
                       QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                       QStringLiteral("--port"), QString::number(m_port),
                       QStringLiteral("-c"), QString::number(contextSize),
                       QStringLiteral("-ngl"), QString::number(gpuLayers)});
    setState(State::Starting);
    emit status(tr("載入模型中（首次可能需要一到數分鐘）…"));
    m_healthTimer.start();
}

void LlamaManager::pollHealth()
{
    if (m_state != State::Starting)
        return;
    if (++m_healthTries > 900) { // 15 分鐘
        emit status(tr("伺服器啟動逾時"));
        stopServer();
        return;
    }
    QNetworkRequest req{QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port))};
    req.setTransferTimeout(3000);
    QNetworkReply *r = m_nam.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (m_state == State::Starting
            && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            m_healthTimer.stop();
            setState(State::Running);
            emit status(tr("本地模型已就緒"));
        }
    });
}

void LlamaManager::stopServer()
{
    m_healthTimer.stop();
    m_stopRequested = true;
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(3000);
    }
    setState(State::Stopped);
}
