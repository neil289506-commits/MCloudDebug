#include "SettingsDialog.h"

#include "AppSettings.h"
#include "SecureStore.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QUrlQuery>
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

QUrl SettingsDialog::githubTokenPrefillUrl()
{
    // 參數出處：GitHub 文件「Pre-filling fine-grained personal access token details using URL parameters」
    //  name / description / expires_in / <permission>=<level>；Models 的帳號權限參數為 user_models=read
    // 注意：GitHub 在 2026-05 的文件更新（github/docs#61353）把這段說明標為 "broken" 而移除，
    //       所以介面上同時提供手動設定步驟當備援。
    QUrl url(QStringLiteral("https://github.com/settings/personal-access-tokens/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), QStringLiteral("MC Debugger"));
    q.addQueryItem(QStringLiteral("description"),
                   QStringLiteral("Used by MC Debugger to send a redacted Minecraft crash log to GitHub Models (read-only)."));
    q.addQueryItem(QStringLiteral("expires_in"), QStringLiteral("30"));
    q.addQueryItem(QStringLiteral("user_models"), QStringLiteral("read"));
    url.setQuery(q);
    return url;
}

SettingsDialog::SettingsDialog(LlamaManager *llama, QWidget *parent)
    : QDialog(parent)
    , m_llama(llama)
    , m_ai(new AiClient(this))
{
    setWindowTitle(tr("AI 管道設定"));
    resize(640, 600);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildGroqTab(), QStringLiteral("Groq"));
    tabs->addTab(buildLocalTab(), tr("本地 LLM"));
    tabs->addTab(buildGitHubTab(), QStringLiteral("GitHub"));

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

    connect(m_ai, &AiClient::modelsReady, this, &SettingsDialog::onModels);
    connect(m_ai, &AiClient::modelsFailed, this, [this](Provider, const QString &msg) {
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
    m_groqModel = new QComboBox;
    m_groqModel->setEditable(true);
    m_groqModel->setCurrentText(App::settings().value(QStringLiteral("groq/model"), QStringLiteral("llama-3.3-70b-versatile")).toString());
    auto *refresh = new QPushButton(tr("更新清單"));
    connect(refresh, &QPushButton::clicked, this, [this] {
        const QString k = m_groqKey->text().trimmed().isEmpty() ? SecureStore::load(QStringLiteral("groq_key"))
                                                                : m_groqKey->text().trimmed();
        if (k.isEmpty()) {
            QMessageBox::information(this, tr("需要金鑰"), tr("請先輸入 Groq API 金鑰。"));
            return;
        }
        m_ai->fetchModels(Provider::Groq, k);
    });
    auto *mr = new QHBoxLayout;
    mr->addWidget(m_groqModel, 1);
    mr->addWidget(refresh);
    c2->addLayout(mr);
    c2->addWidget(muted(tr("免費方案有每分鐘 token 上限；若出現 413/429，請在主視窗調低「最大字元數」。")));
    lay->addStretch();

    setChip(m_groqKeyState, SecureStore::has(QStringLiteral("groq_key")) ? tr("✔ 已加密儲存") : tr("尚未設定"),
            SecureStore::has(QStringLiteral("groq_key")));
    return page;
}

QWidget *SettingsDialog::buildGitHubTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 12, 0, 0);

    QVBoxLayout *c;
    lay->addWidget(makeCard(&c));
    auto *t = new QLabel(tr("Personal Access Token（PAT）"));
    t->setObjectName(QStringLiteral("cardTitle"));
    c->addWidget(t);
    m_ghKey = secretEdit();
    m_ghKey->setPlaceholderText(tr("github_pat_…（留空表示不變更）"));
    c->addWidget(m_ghKey);

    m_ghKeyState = new QLabel;
    auto *clear = new QPushButton(tr("清除已儲存的 Token"));
    clear->setProperty("flat", true);
    connect(clear, &QPushButton::clicked, this, [this] {
        SecureStore::remove(QStringLiteral("github_pat"));
        setChip(m_ghKeyState, tr("尚未設定"), false);
    });
    auto *row = new QHBoxLayout;
    row->addWidget(m_ghKeyState);
    row->addStretch();
    row->addWidget(clear);
    c->addLayout(row);

    auto *create = new QPushButton(tr("建立 Token（開啟已預填的 GitHub 頁面）"));
    create->setProperty("primary", true);
    connect(create, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(githubTokenPrefillUrl()); });
    c->addWidget(create);
    c->addWidget(muted(tr("連結會預填：名稱 <b>MC Debugger</b>、30 天到期、帳號權限 <b>Models: Read-only</b>。<br>"
                          "若 GitHub 沒有自動帶入（GitHub 已把此功能的文件標為 broken），請手動設定："
                          "<i>Account permissions → Models → Read-only</i>，其餘權限都不需要。")));

    QVBoxLayout *c2;
    lay->addWidget(makeCard(&c2));
    auto *t2 = new QLabel(tr("模型（GitHub Models）"));
    t2->setObjectName(QStringLiteral("cardTitle"));
    c2->addWidget(t2);
    m_ghModel = new QComboBox;
    m_ghModel->setEditable(true);
    m_ghModel->setCurrentText(App::settings().value(QStringLiteral("github/model"), QStringLiteral("openai/gpt-4.1")).toString());
    auto *refresh = new QPushButton(tr("更新清單"));
    connect(refresh, &QPushButton::clicked, this, [this] {
        const QString k = m_ghKey->text().trimmed().isEmpty() ? SecureStore::load(QStringLiteral("github_pat"))
                                                              : m_ghKey->text().trimmed();
        m_ai->fetchModels(Provider::GitHub, k);
    });
    auto *mr = new QHBoxLayout;
    mr->addWidget(m_ghModel, 1);
    mr->addWidget(refresh);
    c2->addLayout(mr);
    c2->addWidget(muted(tr("此管道呼叫 <b>GitHub Models</b> 的推論 API（<code>models.github.ai</code>），PAT 只需 <code>models: read</code>。"
                           "它與 Copilot Chat 是不同的服務，用量與限額依你的 GitHub 帳號方案而定。")));
    lay->addStretch();

    setChip(m_ghKeyState, SecureStore::has(QStringLiteral("github_pat")) ? tr("✔ 已加密儲存") : tr("尚未設定"),
            SecureStore::has(QStringLiteral("github_pat")));
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

void SettingsDialog::onModels(Provider p, const QStringList &ids)
{
    QComboBox *box = p == Provider::Groq ? m_groqModel : m_ghModel;
    const QString cur = box->currentText();
    box->clear();
    box->addItems(ids);
    box->setCurrentText(cur);
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
    s.setValue(QStringLiteral("groq/model"), m_groqModel->currentText().trimmed());
    s.setValue(QStringLiteral("github/model"), m_ghModel->currentText().trimmed());
    s.setValue(QStringLiteral("local/ctx"), m_ctx->value());
    s.setValue(QStringLiteral("local/ngl"), m_ngl->value());
    s.setValue(QStringLiteral("local/modelUrl"), m_modelUrl->text().trimmed());
    s.sync();
    QDialog::accept();
}
