#include "SettingsDialog.h"

#include "AppSettings.h"
#include "CopilotClient.h"
#include "ModelCatalog.h"
#include "SecureStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QFrame *makeCard(QVBoxLayout **inner)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("card"));
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(16, 14, 16, 14);
    l->setSpacing(10);
    *inner = l;
    return f;
}

QLabel *muted(const QString &t)
{
    auto *l = new QLabel(t);
    l->setObjectName(QStringLiteral("muted"));
    l->setWordWrap(true);
    l->setOpenExternalLinks(true);
    return l;
}

QLineEdit *secretEdit()
{
    auto *e = new QLineEdit;
    e->setEchoMode(QLineEdit::Password);
    e->setClearButtonEnabled(true);
    return e;
}

void setChip(QLabel *l, const QString &text, bool ok)
{
    l->setText(text);
    l->setObjectName(ok ? QStringLiteral("chipOk") : QStringLiteral("chip"));
    l->style()->unpolish(l);
    l->style()->polish(l);
}

QString fmtBytes(qint64 b)
{
    return b >= (1LL << 30) ? QStringLiteral("%1 GB").arg(b / double(1LL << 30), 0, 'f', 2)
                            : QStringLiteral("%1 MB").arg(b / double(1LL << 20), 0, 'f', 1);
}

} // namespace

SettingsDialog::SettingsDialog(LlamaManager *llama, QWidget *parent)
    : QDialog(parent)
    , m_llama(llama)
    , m_ai(new AiClient(this))
{
    setWindowTitle(tr("AI 管道設定"));
    resize(640, 600);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildGroqTab(), QStringLiteral("Groq"));
    tabs->addTab(buildCopilotTab(), QStringLiteral("GitHub Copilot"));
    tabs->addTab(buildLocalTab(), tr("本地 LLM"));
    tabs->addTab(buildRulesTab(), tr("提示詞規則"));

    auto *ok = new QPushButton(tr("儲存"));
    ok->setProperty("primary", true);
    auto *cancel = new QPushButton(tr("取消"));
    connect(ok, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(ok);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 16);
    root->addWidget(tabs, 1);
    root->addLayout(btns);

    connect(m_ai, &AiClient::modelsReady, this, &SettingsDialog::onGroqModels);
    connect(m_ai, &AiClient::modelsFailed, this, [this](const QString &msg) {
        if (!m_silentFetch) // 開啟設定時的自動更新失敗不打擾使用者，沿用上次的清單
            QMessageBox::warning(this, tr("取得模型清單失敗"), msg);
    });

    connect(m_llama, &LlamaManager::progress, this, [this](qint64 got, qint64 total, const QString &what) {
        m_bar->setVisible(true);
        m_bar->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            m_bar->setValue(int(got * 1000 / total));
        m_taskText->setText(tr("%1：%2 / %3").arg(what, fmtBytes(got), total > 0 ? fmtBytes(total) : QStringLiteral("?")));
    });
    connect(m_llama, &LlamaManager::status, this, [this](const QString &s) { m_taskText->setText(s); });
    connect(m_llama, &LlamaManager::taskFinished, this, [this](bool ok2, const QString &msg) {
        m_bar->setVisible(false);
        m_taskText->setText(ok2 ? tr("✔ %1").arg(msg) : tr("✘ %1").arg(msg));
        refreshLocalStatus();
    });
    connect(m_llama, &LlamaManager::stateChanged, this, [this] { refreshLocalStatus(); });
    refreshLocalStatus();
    refreshCopilotStatus();

    // 開啟設定就自動更新 Groq 模型清單（有金鑰時），讓「所有支援的模型」都在清單裡
    if (SecureStore::has(QStringLiteral("groq_key")))
        fetchGroqModels(true);
}

QWidget *SettingsDialog::buildGroqTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 12, 0, 0);

    QVBoxLayout *c;
    lay->addWidget(makeCard(&c));
    auto *t = new QLabel(tr("API 金鑰"));
    t->setObjectName(QStringLiteral("cardTitle"));
    c->addWidget(t);
    m_groqKey = secretEdit();
    m_groqKey->setPlaceholderText(tr("gsk_…（留空表示不變更）"));
    c->addWidget(m_groqKey);
    m_groqKeyState = new QLabel;
    auto *clear = new QPushButton(tr("清除已儲存的金鑰"));
    clear->setProperty("flat", true);
    connect(clear, &QPushButton::clicked, this, [this] {
        SecureStore::remove(QStringLiteral("groq_key"));
        setChip(m_groqKeyState, tr("尚未設定"), false);
    });
    auto *row = new QHBoxLayout;
    row->addWidget(m_groqKeyState);
    row->addStretch();
    row->addWidget(clear);
    c->addLayout(row);
    c->addWidget(muted(tr("取得金鑰：<a href=\"https://console.groq.com/keys\">console.groq.com/keys</a>。"
                          "金鑰以 Windows DPAPI 加密後存放，只有目前這個 Windows 帳號能解開。")));

    QVBoxLayout *c2;
    lay->addWidget(makeCard(&c2));
    auto *t2 = new QLabel(tr("模型"));
    t2->setObjectName(QStringLiteral("cardTitle"));
    c2->addWidget(t2);
    auto &st = App::settings();
    m_groqModel = new QComboBox;
    m_groqModel->setEditable(true);
    QStringList items = st.value(QStringLiteral("groq/modelCache")).toStringList();
    if (!items.contains(App::defaultGroqModel()))
        items.prepend(App::defaultGroqModel());
    m_groqModel->addItems(items);
    m_groqModel->setCurrentText(st.value(QStringLiteral("groq/model"), App::defaultGroqModel()).toString());
    auto *refresh = new QPushButton(tr("更新清單"));
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (m_groqKey->text().trimmed().isEmpty() && !SecureStore::has(QStringLiteral("groq_key"))) {
            QMessageBox::information(this, tr("需要金鑰"), tr("請先輸入 Groq API 金鑰。"));
            return;
        }
        fetchGroqModels(false);
    });
    auto *reset = new QPushButton(tr("還原預設"));
    connect(reset, &QPushButton::clicked, this, [this] { m_groqModel->setCurrentText(App::defaultGroqModel()); });
    auto *mr = new QHBoxLayout;
    mr->addWidget(m_groqModel, 1);
    mr->addWidget(refresh);
    mr->addWidget(reset);
    c2->addLayout(mr);
    c2->addWidget(muted(tr("預設：<b>%1</b>——Groq 目前正式（Production）等級中，推理能力最強且同時支援推理強度與內建網路搜尋的模型。"
                           "清單是你的金鑰可用的所有聊天模型，也可以直接輸入模型 ID。<br>"
                           "舊預設 llama-3.3-70b-versatile 已被 Groq 宣布關閉，所以改了。").arg(App::defaultGroqModel())));

    QVBoxLayout *c3;
    lay->addWidget(makeCard(&c3));
    auto *t3 = new QLabel(tr("推理與搜尋"));
    t3->setObjectName(QStringLiteral("cardTitle"));
    c3->addWidget(t3);
    m_groqReason = new QComboBox;
    m_groqReason->addItem(tr("高（預設，推理最深）"), QStringLiteral("high"));
    m_groqReason->addItem(tr("中"), QStringLiteral("medium"));
    m_groqReason->addItem(tr("低"), QStringLiteral("low"));
    m_groqReason->addItem(tr("關閉（不送參數）"), QStringLiteral("off"));
    m_groqReason->setCurrentIndex(qMax(0, m_groqReason->findData(st.value(QStringLiteral("groq/reasoning"), QStringLiteral("high")))));
    auto *form = new QFormLayout;
    form->addRow(tr("推理強度"), m_groqReason);
    c3->addLayout(form);
    m_groqSearch = new QCheckBox(tr("啟用網路搜尋（Groq 內建 browser_search）"));
    m_groqSearch->setChecked(st.value(QStringLiteral("groq/search"), true).toBool());
    c3->addWidget(m_groqSearch);
    c3->addWidget(muted(tr("推理強度只會送給支援的模型（gpt-oss、qwen3 系列、muse-spark）；網路搜尋由 Groq 伺服器端執行，"
                           "目前只有 gpt-oss 系列支援，其他模型會自動略過，提示詞也不會要求它搜尋。<br>"
                           "搜尋加高推理強度會消耗較多 token；免費方案若出現 413／429，請降低推理強度或關閉搜尋。")));
    lay->addStretch();

    setChip(m_groqKeyState, SecureStore::has(QStringLiteral("groq_key")) ? tr("✔ 已加密儲存") : tr("尚未設定"),
            SecureStore::has(QStringLiteral("groq_key")));
    return page;
}

QWidget *SettingsDialog::buildCopilotTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 12, 0, 0);
    auto &st = App::settings();

    // --- Copilot CLI ---
    QVBoxLayout *c;
    lay->addWidget(makeCard(&c));
    auto *t = new QLabel(tr("Copilot CLI"));
    t->setObjectName(QStringLiteral("cardTitle"));
    c->addWidget(t);
    m_cliState = new QLabel;
    m_cliPath = new QLineEdit(st.value(QStringLiteral("copilot/cliPath")).toString());
    m_cliPath->setPlaceholderText(tr("留空 = 自動尋找（PATH、npm 全域目錄、WinGet）"));
    m_cliPath->setClearButtonEnabled(true);
    auto *browse = new QPushButton(tr("瀏覽…"));
    auto *test = new QPushButton(tr("測試"));
    connect(m_cliPath, &QLineEdit::textChanged, this, [this] { refreshCopilotStatus(); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("選擇 copilot 執行檔"));
        if (!f.isEmpty())
            m_cliPath->setText(f);
    });
    connect(test, &QPushButton::clicked, this, [this] {
        const CopilotClient::Launcher l = CopilotClient::locate(m_cliPath->text());
        if (!l.ok()) {
            setChip(m_cliState, tr("找不到 Copilot CLI"), false);
            return;
        }
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        connect(p, &QProcess::errorOccurred, this, [this, p] {
            setChip(m_cliState, tr("無法執行"), false);
            p->deleteLater();
        });
        connect(p, &QProcess::finished, this, [this, p] {
            const QString line = QString::fromUtf8(p->readAll()).trimmed().split(QLatin1Char('\n')).value(0).trimmed();
            setChip(m_cliState, line.isEmpty() ? tr("✔ 可執行") : tr("✔ %1").arg(line), !line.isEmpty());
            p->deleteLater();
        });
        p->start(l.program, l.prefixArgs + QStringList{QStringLiteral("--version")});
    });
    auto *row = new QHBoxLayout;
    row->addWidget(m_cliPath, 1);
    row->addWidget(browse);
    row->addWidget(test);
    c->addLayout(row);
    c->addWidget(m_cliState);
    c->addWidget(muted(tr("此管道呼叫官方 <b>GitHub Copilot CLI</b>（非互動模式），需要有效的 Copilot 訂閱。"
                          "安裝：<code>npm install -g @github/copilot</code>。<br>"
                          "原本的 GitHub Models 推論 API 已於 2026-07-30 退役，所以不再使用。")));

    // --- 登入（copilot login：桌面環境會自動開瀏覽器授權，登入狀態由 CLI 存進系統憑證庫） ---
    QVBoxLayout *cl;
    lay->addWidget(makeCard(&cl));
    auto *tl = new QLabel(tr("登入 GitHub"));
    tl->setObjectName(QStringLiteral("cardTitle"));
    cl->addWidget(tl);
    auto *loginBtn = new QPushButton(tr("登入 GitHub（開啟瀏覽器）"));
    auto *loginState = new QLabel;
    loginState->setWordWrap(true);
    connect(loginBtn, &QPushButton::clicked, this, [this, loginBtn, loginState] {
        const CopilotClient::Launcher l = CopilotClient::locate(m_cliPath->text());
        if (!l.ok()) {
            setChip(loginState, tr("找不到 Copilot CLI，請先安裝或指定路徑"), false);
            return;
        }
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        // 環境變數裡的 Token 會被 CLI 優先採用、導致 login 走不到瀏覽器流程，所以登入時一律移除
        for (const char *k : {"COPILOT_GITHUB_TOKEN", "GH_TOKEN", "GITHUB_TOKEN"})
            env.remove(QString::fromLatin1(k));
        env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
        p->setProcessEnvironment(env);
        loginBtn->setEnabled(false);
        setChip(loginState, tr("請在瀏覽器中完成授權…（若沒有自動開啟，請看終端機視窗或稍後再試）"), true);
        connect(p, &QProcess::errorOccurred, this, [loginBtn, loginState, p, this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            loginBtn->setEnabled(true);
            setChip(loginState, tr("無法啟動 Copilot CLI：%1").arg(p->errorString()), false);
            p->deleteLater();
        });
        connect(p, &QProcess::finished, this, [loginBtn, loginState, p, this](int code, QProcess::ExitStatus) {
            loginBtn->setEnabled(true);
            const QString out = QString::fromUtf8(p->readAll()).trimmed();
            setChip(loginState, code == 0 ? tr("✔ 登入完成") : tr("登入失敗：%1").arg(out.left(400)), code == 0);
            p->deleteLater();
        });
        p->start(l.program, l.prefixArgs + QStringList{QStringLiteral("login")});
    });
    cl->addWidget(loginBtn);
    cl->addWidget(loginState);
    cl->addWidget(muted(tr("等同在終端機執行 <code>copilot login</code>：登入後不需要貼 Token，之後的分析會自動沿用這個登入。"
                           "需要有效的 Copilot 訂閱。")));

    // --- Token ---
    QVBoxLayout *ck;
    lay->addWidget(makeCard(&ck));
    auto *tk = new QLabel(tr("GitHub Token（進階，可選）"));
    tk->setObjectName(QStringLiteral("cardTitle"));
    ck->addWidget(tk);
    m_ghKey = secretEdit();
    m_ghKey->setPlaceholderText(tr("github_pat_…（留空表示不變更）"));
    ck->addWidget(m_ghKey);
    m_ghKeyState = new QLabel;
    auto *clear = new QPushButton(tr("清除已儲存的 Token"));
    clear->setProperty("flat", true);
    connect(clear, &QPushButton::clicked, this, [this] {
        SecureStore::remove(QStringLiteral("github_pat"));
        setChip(m_ghKeyState, tr("尚未設定"), false);
    });
    auto *krow = new QHBoxLayout;
    krow->addWidget(m_ghKeyState);
    krow->addStretch();
    krow->addWidget(clear);
    ck->addLayout(krow);
    ck->addWidget(muted(tr("留空就使用上面「登入 GitHub」的登入狀態。"
                           "有填的話，會以 DPAPI 加密儲存，執行時只透過環境變數 <code>COPILOT_GITHUB_TOKEN</code> 交給 CLI 子行程。"
                           "不支援傳統 PAT（<code>ghp_</code>）；所需權限請見 GitHub Copilot CLI 文件。")));

    // --- 模型與行為 ---
    QVBoxLayout *c2;
    lay->addWidget(makeCard(&c2));
    auto *t2 = new QLabel(tr("模型與行為"));
    t2->setObjectName(QStringLiteral("cardTitle"));
    c2->addWidget(t2);
    m_ghModel = new QComboBox;
    m_ghModel->setEditable(true);
    m_ghModel->addItems(ModelCatalog::copilotSuggested());
    m_ghModel->setCurrentText(st.value(QStringLiteral("copilot/model"), App::defaultCopilotModel()).toString());
    m_ghTier = new QComboBox;
    m_ghTier->addItem(tr("不指定（Copilot 預設）"), QString());
    m_ghTier->addItem(tr("Efficiency（成本優先）"), QStringLiteral("efficiency"));
    m_ghTier->addItem(tr("Balance（平衡）"), QStringLiteral("balance"));
    m_ghTier->addItem(tr("Intelligence（品質優先）"), QStringLiteral("intelligence"));
    m_ghTier->setCurrentIndex(qMax(0, m_ghTier->findData(st.value(QStringLiteral("copilot/autotier")))));
    m_ghTier->setToolTip(tr("只在模型為 auto 時有效"));
    m_ghReason = new QComboBox;
    m_ghReason->addItem(tr("不指定（預設）"), QString());
    m_ghReason->addItem(tr("低"), QStringLiteral("low"));
    m_ghReason->addItem(tr("中"), QStringLiteral("medium"));
    m_ghReason->addItem(tr("高"), QStringLiteral("high"));
    m_ghReason->addItem(tr("最高（xhigh）"), QStringLiteral("xhigh"));
    m_ghReason->setCurrentIndex(qMax(0, m_ghReason->findData(st.value(QStringLiteral("copilot/reasoning")))));
    m_ghReason->setToolTip(tr("可用的等級依模型而定；模型不支援時 CLI 會回報錯誤"));
    auto *form = new QFormLayout;
    form->addRow(tr("模型"), m_ghModel);
    form->addRow(tr("Auto 路由偏好"), m_ghTier);
    form->addRow(tr("推理強度"), m_ghReason);
    c2->addLayout(form);
    m_ghSearch = new QCheckBox(tr("允許 Copilot 搜尋網路、抓取網頁（唯讀）"));
    m_ghSearch->setChecked(st.value(QStringLiteral("copilot/search"), true).toBool());
    c2->addWidget(m_ghSearch);
    c2->addWidget(muted(tr("預設模型 <b>auto</b>：由 Copilot 依任務自動挑選。關閉搜尋後，提示詞也不會要求它搜尋。"
                           "shell 與寫檔工具一律封鎖。<br>"
                           "模型清單：官方文件只提供顯示名稱、沒有可程式化取得的 ID 清單，所以清單裡的 ID 是依命名規則推導、"
                           "<b>未逐一驗證</b>的；可自行輸入任何 ID。你的方案實際可用哪些，請在 Copilot CLI 內用 <code>/model</code> 查看。")));
    lay->addStretch();

    setChip(m_ghKeyState, SecureStore::has(QStringLiteral("github_pat")) ? tr("✔ 已加密儲存") : tr("未設定（改用 copilot login）"),
            SecureStore::has(QStringLiteral("github_pat")));
    return page;
}

QWidget *SettingsDialog::buildRulesTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 12, 0, 0);
    auto &st = App::settings();

    QVBoxLayout *c;
    lay->addWidget(makeCard(&c));
    auto *t = new QLabel(tr("加進提示詞的規則"));
    t->setObjectName(QStringLiteral("cardTitle"));
    c->addWidget(t);
    m_ruleSearch = new QCheckBox(tr("1. 搜尋網路——回答前先查版本、已知問題與官方來源，並列出實際用過的網址"));
    m_ruleNoGuess = new QCheckBox(tr("2. 不要猜測——不知道就明說「不知道」，並列出還需要什麼資訊"));
    m_ruleDeep = new QCheckBox(tr("3. 深度思考——先逐步推理、比較至少兩個可能原因，再用 log 內容複查結論"));
    m_ruleSearch->setChecked(st.value(QStringLiteral("rules/search"), true).toBool());
    m_ruleNoGuess->setChecked(st.value(QStringLiteral("rules/noguess"), true).toBool());
    m_ruleDeep->setChecked(st.value(QStringLiteral("rules/deepthink"), true).toBool());
    for (QCheckBox *b : {m_ruleSearch, m_ruleNoGuess, m_ruleDeep}) {
        b->setStyleSheet(QStringLiteral("QCheckBox { padding: 3px 0; }"));
        c->addWidget(b);
    }
    c->addWidget(muted(tr("這三條會加在 <b>Groq</b> 與 <b>GitHub Copilot</b> 的系統提示詞後面（本地 LLM 不受影響）。<br>"
                          "第 1 條只有在該管道＋模型「這次真的有搜尋工具」時才會加入——沒有工具卻要求搜尋，只會讓模型假裝搜尋過。"
                          "搜尋能力在各管道分頁開關。")));
    lay->addStretch();
    return page;
}

QWidget *SettingsDialog::buildLocalTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 12, 0, 0);

    QVBoxLayout *c;
    lay->addWidget(makeCard(&c));
    auto *t = new QLabel(tr("元件"));
    t->setObjectName(QStringLiteral("cardTitle"));
    c->addWidget(t);

    m_srvState = new QLabel;
    m_modelState = new QLabel;
    m_runState = new QLabel;
    m_backend = new QComboBox;
    m_backend->addItem(tr("Vulkan（NVIDIA/AMD/Intel 顯卡，推薦）"), int(LlamaManager::Backend::Vulkan));
    m_backend->addItem(tr("CPU（相容性最高，較慢）"), int(LlamaManager::Backend::Cpu));
    m_backend->addItem(QStringLiteral("CUDA 12.4（NVIDIA）"), int(LlamaManager::Backend::Cuda));
    m_backend->setCurrentIndex(qMax(0, m_backend->findData(App::settings().value(QStringLiteral("local/backend"), 0))));

    m_btnServer = new QPushButton(tr("下載 llama.cpp"));
    m_btnModel = new QPushButton(tr("下載模型（約 5.6 GB）"));
    m_btnCancel = new QPushButton(tr("取消"));
    m_btnRun = new QPushButton(tr("啟動伺服器"));
    connect(m_btnServer, &QPushButton::clicked, this, [this] {
        m_llama->installServer(LlamaManager::Backend(m_backend->currentData().toInt()));
        refreshLocalStatus();
    });
    connect(m_btnModel, &QPushButton::clicked, this, [this] {
        App::settings().setValue(QStringLiteral("local/modelUrl"), m_modelUrl->text().trimmed());
        m_llama->downloadModel();
        refreshLocalStatus();
    });
    connect(m_btnCancel, &QPushButton::clicked, m_llama, &LlamaManager::cancelTask);
    connect(m_btnRun, &QPushButton::clicked, this, [this] {
        if (m_llama->state() == LlamaManager::State::Stopped)
            m_llama->startServer(m_ctx->value(), m_ngl->value());
        else
            m_llama->stopServer();
    });

    auto addRow = [&](const QString &name, QWidget *chip, QWidget *btn) {
        auto *r = new QHBoxLayout;
        r->addWidget(new QLabel(name), 1);
        r->addWidget(chip);
        if (btn)
            r->addWidget(btn);
        c->addLayout(r);
    };
    addRow(QStringLiteral("llama.cpp"), m_srvState, m_btnServer);
    c->addWidget(m_backend);
    addRow(tr("模型 Qwythos-9B（Q4_K_M）"), m_modelState, m_btnModel);
    addRow(tr("本地伺服器"), m_runState, m_btnRun);

    m_bar = new QProgressBar;
    m_bar->setVisible(false);
    m_taskText = muted(QString());
    auto *pr = new QHBoxLayout;
    pr->addWidget(m_bar, 1);
    pr->addWidget(m_btnCancel);
    c->addLayout(pr);
    c->addWidget(m_taskText);

    QVBoxLayout *c2;
    lay->addWidget(makeCard(&c2));
    auto *t2 = new QLabel(tr("進階"));
    t2->setObjectName(QStringLiteral("cardTitle"));
    c2->addWidget(t2);
    auto *form = new QFormLayout;
    m_ctx = new QSpinBox;
    m_ctx->setRange(4096, 131072);
    m_ctx->setSingleStep(4096);
    m_ctx->setValue(App::settings().value(QStringLiteral("local/ctx"), 16384).toInt());
    m_ngl = new QSpinBox;
    m_ngl->setRange(0, 999);
    m_ngl->setValue(App::settings().value(QStringLiteral("local/ngl"), 99).toInt());
    m_modelUrl = new QLineEdit(App::settings().value(QStringLiteral("local/modelUrl"), QString::fromLatin1(LlamaManager::kDefaultModelUrl)).toString());
    form->addRow(tr("上下文長度 (-c)"), m_ctx);
    form->addRow(tr("GPU 層數 (-ngl)"), m_ngl);
    form->addRow(tr("模型下載網址"), m_modelUrl);
    c2->addLayout(form);
    c2->addWidget(muted(tr("模型為 empero-ai 的 <b>Qwythos-9B-Claude-Mythos-5-1M</b>（推理模型，回答前會先輸出 &lt;think&gt; 思考，程式會自動隱藏）。"
                           "沒有獨立顯卡時請選 CPU 並預期回應較慢；GPU 層數 0 = 全 CPU。")));
    lay->addStretch();
    return page;
}

void SettingsDialog::refreshLocalStatus()
{
    const bool hasSrv = !m_llama->serverExe().isEmpty();
    setChip(m_srvState, hasSrv ? tr("✔ 已安裝 %1").arg(m_llama->installedTag()) : tr("未安裝"), hasSrv);
    const bool hasModel = m_llama->modelInstalled();
    setChip(m_modelState, hasModel ? tr("✔ 已下載") : (m_llama->modelPartial() ? tr("未完成（可續傳）") : tr("未下載")), hasModel);
    const auto st = m_llama->state();
    setChip(m_runState, st == LlamaManager::State::Running ? tr("● 執行中")
                        : st == LlamaManager::State::Starting ? tr("載入中…") : tr("已停止"),
            st == LlamaManager::State::Running);
    m_btnRun->setText(st == LlamaManager::State::Stopped ? tr("啟動伺服器") : tr("停止"));
    const bool busy = m_llama->busy();
    m_btnServer->setEnabled(!busy);
    m_btnModel->setEnabled(!busy);
    m_btnCancel->setVisible(busy);
    m_btnRun->setEnabled(!busy && hasSrv && hasModel);
    m_backend->setEnabled(!busy);
}

void SettingsDialog::refreshCopilotStatus()
{
    const CopilotClient::Launcher l = CopilotClient::locate(m_cliPath->text());
    setChip(m_cliState, l.ok() ? tr("✔ 找到：%1").arg(l.display()) : tr("找不到 Copilot CLI（請安裝，或在上方指定路徑）"), l.ok());
}

void SettingsDialog::fetchGroqModels(bool silent)
{
    const QString k = m_groqKey->text().trimmed().isEmpty() ? SecureStore::load(QStringLiteral("groq_key"))
                                                            : m_groqKey->text().trimmed();
    if (k.isEmpty())
        return;
    m_silentFetch = silent;
    m_ai->fetchModels(k);
}

void SettingsDialog::onGroqModels(const QStringList &ids)
{
    const QString cur = m_groqModel->currentText().trimmed();
    m_groqModel->clear();
    m_groqModel->addItems(ids);
    // 已儲存的模型若已不在清單（例如被 Groq 下架），改選目前可用的最強推理模型；使用者自己輸入的 ID 一樣會被檢查
    m_groqModel->setCurrentText(ids.contains(cur) ? cur : ModelCatalog::pickGroqBest(ids, App::defaultGroqModel()));
    App::settings().setValue(QStringLiteral("groq/modelCache"), ids);
    if (!m_silentFetch)
        QMessageBox::information(this, tr("模型清單已更新"), tr("共 %1 個可用的聊天模型。").arg(ids.size()));
}

void SettingsDialog::accept()
{
    bool failed = false;
    auto saveSecret = [&](QLineEdit *edit, const QString &name) {
        const QString v = edit->text().trimmed();
        if (v.isEmpty())
            return;
        if (!SecureStore::save(name, v))
            failed = true;
    };
    saveSecret(m_groqKey, QStringLiteral("groq_key"));
    saveSecret(m_ghKey, QStringLiteral("github_pat"));
    if (failed) {
        QMessageBox::critical(this, tr("加密儲存失敗"),
                              tr("無法使用 Windows DPAPI 加密金鑰，因此沒有儲存（不會退回明文儲存）。"));
        return;
    }
    auto &s = App::settings();
    s.setValue(QStringLiteral("groq/model"), m_groqModel->currentText().trimmed().isEmpty()
                                                 ? App::defaultGroqModel() : m_groqModel->currentText().trimmed());
    s.setValue(QStringLiteral("groq/reasoning"), m_groqReason->currentData());
    s.setValue(QStringLiteral("groq/search"), m_groqSearch->isChecked());
    s.setValue(QStringLiteral("copilot/cliPath"), m_cliPath->text().trimmed());
    s.setValue(QStringLiteral("copilot/model"), m_ghModel->currentText().trimmed().isEmpty()
                                                    ? App::defaultCopilotModel() : m_ghModel->currentText().trimmed());
    s.setValue(QStringLiteral("copilot/autotier"), m_ghTier->currentData());
    s.setValue(QStringLiteral("copilot/reasoning"), m_ghReason->currentData());
    s.setValue(QStringLiteral("copilot/search"), m_ghSearch->isChecked());
    s.setValue(QStringLiteral("rules/search"), m_ruleSearch->isChecked());
    s.setValue(QStringLiteral("rules/noguess"), m_ruleNoGuess->isChecked());
    s.setValue(QStringLiteral("rules/deepthink"), m_ruleDeep->isChecked());
    s.setValue(QStringLiteral("local/ctx"), m_ctx->value());
    s.setValue(QStringLiteral("local/ngl"), m_ngl->value());
    s.setValue(QStringLiteral("local/modelUrl"), m_modelUrl->text().trimmed());
    s.sync();
    QDialog::accept();
}
