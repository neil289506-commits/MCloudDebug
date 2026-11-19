#include "MainWindow.h"

#include "AppSettings.h"
#include "SecureStore.h"
#include "SettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString ago(const QDateTime &t)
{
    const qint64 s = t.secsTo(QDateTime::currentDateTime());
    if (s < 90) return QObject::tr("剛剛");
    if (s < 3600) return QObject::tr("%1 分鐘前").arg(s / 60);
    if (s < 86400) return QObject::tr("%1 小時前").arg(s / 3600);
    return QObject::tr("%1 天前").arg(s / 86400);
}

QString sizeText(qint64 b)
{
    if (b >= (1 << 20)) return QStringLiteral("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, b / 1024));
}

void setProp(QWidget *w, const char *prop, bool v)
{
    w->setProperty(prop, v);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QLabel *cardTitle(const QString &t)
{
    auto *l = new QLabel(t.toUpper());
    l->setObjectName(QStringLiteral("cardTitle"));
    return l;
}

} // namespace

MainWindow::MainWindow()
    : m_llama(new LlamaManager(this))
    , m_ai(new AiClient(this))
{
    setWindowTitle(tr("MC Debugger — Minecraft 崩潰診斷"));
    resize(1240, 800);
    setMinimumSize(980, 640);
    setAcceptDrops(true);
    buildUi();

    m_watchDebounce.setSingleShot(true);
    m_watchDebounce.setInterval(1200);
    connect(&m_watchDebounce, &QTimer::timeout, this, &MainWindow::checkNewCrash);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_watchDebounce.start(); });

    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(90);
    connect(&m_renderTimer, &QTimer::timeout, this, &MainWindow::renderAnswer);

    connect(m_ai, &AiClient::contentDelta, this, [this](const QString &t) { m_raw += t; scheduleRender(); });
    connect(m_ai, &AiClient::reasoningDelta, this, [this](const QString &) { m_reasoningSeen = true; scheduleRender(); });
    connect(m_ai, &AiClient::finished, this, [this] {
        const QString text = visibleAnswer();
        m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), text}});
        m_transcript += text;
        m_raw.clear();
        m_reasoningSeen = false;
        m_hasAnswer = true;
        renderAnswer();
        setBusy(false, tr("完成"));
    });
    connect(m_ai, &AiClient::failed, this, [this](const QString &msg) {
        m_transcript += visibleAnswer();
        m_transcript += tr("\n\n> ⚠️ **請求失敗**：%1\n").arg(msg);
        m_raw.clear();
        m_reasoningSeen = false;
        renderAnswer();
        setBusy(false, tr("失敗"));
    });

    connect(m_llama, &LlamaManager::stateChanged, this, [this](LlamaManager::State st) {
        updateProviderChip();
        if (st == LlamaManager::State::Running && m_pendingAnalyze) {
            m_pendingAnalyze = false;
            runAnalyze();
        } else if (st == LlamaManager::State::Stopped && m_pendingAnalyze) {
            m_pendingAnalyze = false;
            setBusy(false, tr("本地伺服器啟動失敗"));
            QMessageBox::warning(this, tr("本地模型啟動失敗"), tr("llama-server 已結束。請到設定檢查 llama.cpp 與模型是否完整，或改選 CPU 後端／降低 GPU 層數。"));
        }
    });
    connect(m_llama, &LlamaManager::status, this, [this](const QString &s) { statusBar()->showMessage(s, 8000); });

    loadInstances();
    updateProviderChip();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(18, 16, 18, 12);
    root->setSpacing(16);

    // ---- 左側欄 ----------------------------------------------------------
    auto *side = new QVBoxLayout;
    side->setSpacing(12);
    auto *title = new QLabel(tr("🪲 MC Debugger"));
    title->setObjectName(QStringLiteral("appTitle"));
    auto *sub = new QLabel(tr("崩潰了？整理 log，交給 AI 找原因。"));
    sub->setObjectName(QStringLiteral("muted"));
    side->addWidget(title);
    side->addWidget(sub);

    // 實例
    m_instanceBox = new QComboBox;
    auto *add = new QPushButton(tr("＋ 新增"));
    auto *scan = new QPushButton(tr("掃描啟動器"));
    auto *del = new QPushButton(tr("移除"));
    del->setProperty("flat", true);
    m_fileInfo = new QLabel;
    m_fileInfo->setObjectName(QStringLiteral("muted"));
    m_fileInfo->setTextFormat(Qt::RichText);
    m_fileInfo->setWordWrap(true);
    auto *irow = new QHBoxLayout;
    irow->addWidget(add);
    irow->addWidget(scan);
    irow->addStretch();
    irow->addWidget(del);
    auto *ilay = new QVBoxLayout;
    ilay->addWidget(m_instanceBox);
    ilay->addLayout(irow);
    ilay->addWidget(m_fileInfo);
    side->addWidget(makeCard(tr("遊戲實例"), ilay));

    connect(add, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("選擇實例資料夾（.minecraft 或啟動器的實例目錄）"));
        if (!d.isEmpty())
            addInstance(d);
    });
    connect(scan, &QPushButton::clicked, this, &MainWindow::scanLaunchers);
    connect(del, &QPushButton::clicked, this, [this] {
        const int i = m_instanceBox->currentIndex();
        if (i < 0 || i >= m_inst.size())
            return;
        m_inst.removeAt(i);
        saveInstances();
        m_instanceBox->removeItem(i);
        refreshFiles();
    });
    connect(m_instanceBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshFiles(); });

    // 整理選項
    m_optCrash = new QCheckBox(tr("包含 crash-report"));
    m_optHs = new QCheckBox(tr("包含 hs_err（JVM 崩潰）"));
    m_optMods = new QCheckBox(tr("附上 Mod 清單"));
    m_optRedact = new QCheckBox(tr("遮蔽個資（使用者名、Token、UUID）"));
    m_optWatch = new QCheckBox(tr("偵測到新崩潰時提醒我"));
    auto &st = App::settings();
    m_optCrash->setChecked(st.value(QStringLiteral("opt/crash"), true).toBool());
    m_optHs->setChecked(st.value(QStringLiteral("opt/hs"), true).toBool());
    m_optMods->setChecked(st.value(QStringLiteral("opt/mods"), true).toBool());
    m_optRedact->setChecked(st.value(QStringLiteral("opt/redact"), true).toBool());
    m_optWatch->setChecked(st.value(QStringLiteral("opt/watch"), true).toBool());
    m_maxChars = new QSpinBox;
    m_maxChars->setRange(3000, 120000);
    m_maxChars->setSingleStep(2000);
    m_maxChars->setSuffix(tr(" 字元"));
    m_maxChars->setValue(st.value(QStringLiteral("opt/maxChars"), 14000).toInt());
    m_lang = new QComboBox;
    m_lang->addItems({QStringLiteral("繁體中文"), QStringLiteral("简体中文"), QStringLiteral("English"), QStringLiteral("日本語")});
    m_lang->setCurrentText(st.value(QStringLiteral("opt/lang"), QStringLiteral("繁體中文")).toString());
    auto *olay = new QVBoxLayout;
    olay->setSpacing(8);
    for (auto *c : {m_optCrash, m_optHs, m_optMods, m_optRedact, m_optWatch})
        olay->addWidget(c);
    auto *mrow = new QHBoxLayout;
    mrow->addWidget(new QLabel(tr("上限")));
    mrow->addWidget(m_maxChars, 1);
    olay->addLayout(mrow);
    auto *lrow = new QHBoxLayout;
    lrow->addWidget(new QLabel(tr("回答語言")));
    lrow->addWidget(m_lang, 1);
    olay->addLayout(lrow);
    side->addWidget(makeCard(tr("整理選項"), olay));
    connect(m_optWatch, &QCheckBox::toggled, this, [this] { rewatch(); });

    // AI 管道
    m_provider = new QComboBox;
    m_provider->addItem(QStringLiteral("Groq"), int(Provider::Groq));
    m_provider->addItem(tr("本地 LLM（llama.cpp + Qwythos）"), int(Provider::Local));
    m_provider->addItem(QStringLiteral("GitHub Models"), int(Provider::GitHub));
    m_provider->setCurrentIndex(qMax(0, m_provider->findData(st.value(QStringLiteral("opt/provider"), int(Provider::Groq)))));
    m_providerChip = new QLabel;
    auto *cfg = new QPushButton(tr("⚙ 設定"));
    auto *prow = new QHBoxLayout;
    prow->addWidget(m_providerChip, 1);
    prow->addWidget(cfg);
    auto *play = new QVBoxLayout;
    play->addWidget(m_provider);
    play->addLayout(prow);
    side->addWidget(makeCard(tr("AI 管道"), play));
    connect(cfg, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(m_provider, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateProviderChip(); });

    // 動作
    m_note = new QLineEdit;
    m_note->setPlaceholderText(tr("補充說明（可選）：例如「剛裝了 Sodium 就崩潰」"));
    m_btnClean = new QPushButton(tr("🧹 整理 Log"));
    m_btnAnalyze = new QPushButton(tr("🤖 整理並請 AI 診斷"));
    m_btnAnalyze->setProperty("primary", true);
    m_btnCancel = new QPushButton(tr("■ 取消"));
    m_btnCancel->setVisible(false);
    side->addStretch();
    connect(m_btnClean, &QPushButton::clicked, this, &MainWindow::runClean);
    connect(m_btnAnalyze, &QPushButton::clicked, this, [this] {
        runClean();
        if (!m_cleaned->toPlainText().trimmed().isEmpty())
            runAnalyze();
    });
    connect(m_btnCancel, &QPushButton::clicked, this, [this] {
        m_pendingAnalyze = false;
        m_ai->cancel();
        m_transcript += visibleAnswer() + tr("\n\n> ⏹ 已取消\n");
        m_raw.clear();
        renderAnswer();
        setBusy(false, tr("已取消"));
    });

    auto *sideW = new QWidget;
    sideW->setLayout(side);
    auto *sideScroll = new QScrollArea;
    sideScroll->setWidget(sideW);
    sideScroll->setWidgetResizable(true); // 視窗太矮時側欄可捲動，卡片不會被壓扁
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->viewport()->setAutoFillBackground(false);
    sideW->setAutoFillBackground(false);
    // 動作按鈕固定在側欄底部，不隨捲動消失
    auto *leftCol = new QVBoxLayout;
    leftCol->setSpacing(8);
    leftCol->addWidget(sideScroll, 1);
    leftCol->addWidget(m_note);
    leftCol->addWidget(m_btnClean);
    leftCol->addWidget(m_btnAnalyze);
    leftCol->addWidget(m_btnCancel);
    auto *leftW = new QWidget;
    leftW->setLayout(leftCol);
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftW->setFixedWidth(356);
    root->addWidget(leftW);

    // ---- 右側主區 --------------------------------------------------------
    auto *main = new QVBoxLayout;
    main->setSpacing(10);

    m_banner = new QFrame;
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setVisible(false);
    auto *bl = new QHBoxLayout(m_banner);
    bl->setContentsMargins(14, 8, 10, 8);
    m_bannerText = new QLabel;
    auto *bGo = new QPushButton(tr("整理並診斷"));
    bGo->setProperty("primary", true);
    auto *bX = new QPushButton(QStringLiteral("✕"));
    bX->setProperty("flat", true);
    bl->addWidget(m_bannerText, 1);
    bl->addWidget(bGo);
    bl->addWidget(bX);
    main->addWidget(m_banner);
    connect(bX, &QPushButton::clicked, m_banner, &QWidget::hide);
    connect(bGo, &QPushButton::clicked, this, [this] {
        m_banner->hide();
        m_btnAnalyze->click();
    });

    m_tabs = new QTabWidget;
    m_cleaned = new QPlainTextEdit;
    m_cleaned->setPlaceholderText(tr("按「整理 Log」後，精簡過的內容會顯示在這裡。\n你可以先檢查、手動刪改，再送給 AI —— 送出的就是這裡的文字。"));
    m_cleaned->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_answer = new QTextBrowser;
    m_answer->setOpenExternalLinks(true);
    m_answer->setPlaceholderText(tr("AI 的診斷結果會即時顯示在這裡。"));
    m_tabs->addTab(m_cleaned, tr("整理後的 Log"));
    m_tabs->addTab(m_answer, tr("AI 診斷"));
    main->addWidget(m_tabs, 1);

    m_followUp = new QLineEdit;
    m_followUp->setPlaceholderText(tr("追問（例如：「如果我不想移除這個 Mod，有別的辦法嗎？」）"));
    m_btnFollow = new QPushButton(tr("送出"));
    m_btnCopy = new QPushButton(tr("複製"));
    m_btnSave = new QPushButton(tr("匯出 .md"));
    auto *frow = new QHBoxLayout;
    frow->addWidget(m_followUp, 1);
    frow->addWidget(m_btnFollow);
    frow->addWidget(m_btnCopy);
    frow->addWidget(m_btnSave);
    main->addLayout(frow);
    m_followUp->setEnabled(false);
    m_btnFollow->setEnabled(false);
    connect(m_followUp, &QLineEdit::returnPressed, this, &MainWindow::sendFollowUp);
    connect(m_btnFollow, &QPushButton::clicked, this, &MainWindow::sendFollowUp);
    connect(m_btnCopy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_tabs->currentWidget() == m_answer ? m_answer->toPlainText() : m_cleaned->toPlainText());
        statusBar()->showMessage(tr("已複製"), 3000);
    });
    connect(m_btnSave, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("匯出診斷結果"), QStringLiteral("mc-debugger-report.md"), tr("Markdown (*.md)"));
        if (f.isEmpty())
            return;
        QFile file(f);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write((QStringLiteral("# MC Debugger 診斷\n\n") + m_transcript + QStringLiteral("\n\n---\n\n## 整理後的 Log\n\n```\n")
                        + m_cleaned->toPlainText() + QStringLiteral("\n```\n")).toUtf8());
    });

    auto *mainW = new QWidget;
    mainW->setLayout(main);
    root->addWidget(mainW, 1);

    m_stats = new QLabel(tr("就緒"));
    statusBar()->addPermanentWidget(m_stats);
}

QFrame *MainWindow::makeCard(const QString &title, QLayout *content)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("card"));
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(14, 12, 14, 14);
    l->setSpacing(8);
    l->addWidget(cardTitle(title));
    l->addLayout(content);
    return f;
}

// ---------------------------------------------------------------------------
// 實例管理
// ---------------------------------------------------------------------------
void MainWindow::loadInstances()
{
    auto &s = App::settings();
    const int n = s.beginReadArray(QStringLiteral("instances"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        m_inst.append({s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("path")).toString()});
    }
    s.endArray();
    if (m_inst.isEmpty()) {
        const QString mc = qEnvironmentVariable("APPDATA") + QStringLiteral("/.minecraft");
        if (QFileInfo(mc).isDir())
            m_inst.append({tr("官方啟動器 (.minecraft)"), mc});
    }
    m_instanceBox->blockSignals(true);
    for (const Instance &i : std::as_const(m_inst))
        m_instanceBox->addItem(i.name, i.path);
    m_instanceBox->setCurrentIndex(qBound(-1, s.value(QStringLiteral("opt/instance"), 0).toInt(), int(m_inst.size()) - 1));
    m_instanceBox->blockSignals(false);
    refreshFiles();
}

void MainWindow::saveInstances()
{
    auto &s = App::settings();
    s.beginWriteArray(QStringLiteral("instances"), int(m_inst.size()));
    for (int i = 0; i < m_inst.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), m_inst[i].name);
        s.setValue(QStringLiteral("path"), m_inst[i].path);
    }
    s.endArray();
    s.sync();
}

void MainWindow::addInstance(const QString &path, QString name)
{
    for (int i = 0; i < m_inst.size(); ++i)
        if (QDir::cleanPath(m_inst[i].path) == QDir::cleanPath(path)) {
            m_instanceBox->setCurrentIndex(i);
            return;
        }
    if (name.isEmpty()) {
        QDir d(path);
        // Prism/MultiMC 的實例名稱在上一層（.../instances/<名稱>/minecraft）
        if (d.dirName() == QLatin1String("minecraft") || d.dirName() == QLatin1String(".minecraft")) {
            QDir up(d);
            up.cdUp();
            name = up.dirName();
        } else {
            name = d.dirName();
        }
        bool ok = false;
        name = QInputDialog::getText(this, tr("實例名稱"), tr("幫這個實例取個名字："), QLineEdit::Normal, name, &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
    }
    m_inst.append({name.trimmed(), path});
    saveInstances();
    m_instanceBox->addItem(name.trimmed(), path);
    m_instanceBox->setCurrentIndex(m_instanceBox->count() - 1);
}

void MainWindow::scanLaunchers()
{
    const QString appdata = qEnvironmentVariable("APPDATA");
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    struct Root { QString label, dir; bool children; };
    const QList<Root> roots = {
        {tr("官方"), appdata + QStringLiteral("/.minecraft"), false},
        {QStringLiteral("Prism"), appdata + QStringLiteral("/PrismLauncher/instances"), true},
        {QStringLiteral("PolyMC"), appdata + QStringLiteral("/PolyMC/instances"), true},
        {QStringLiteral("MultiMC"), appdata + QStringLiteral("/MultiMC/instances"), true},
        {QStringLiteral("Modrinth"), appdata + QStringLiteral("/ModrinthApp/profiles"), true},
        {QStringLiteral("CurseForge"), docs + QStringLiteral("/curseforge/minecraft/Instances"), true},
    };
    int added = 0;
    for (const Root &r : roots) {
        if (!QFileInfo(r.dir).isDir())
            continue;
        QStringList paths;
        if (r.children) {
            for (const QFileInfo &fi : QDir(r.dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (!fi.fileName().startsWith(QLatin1Char('_')))
                    paths << fi.absoluteFilePath();
        } else {
            paths << r.dir;
        }
        for (const QString &p : std::as_const(paths)) {
            bool dup = false;
            for (const Instance &i : std::as_const(m_inst))
                dup = dup || QDir::cleanPath(i.path) == QDir::cleanPath(p);
            if (dup)
                continue;
            const QString nm = r.children ? QStringLiteral("%1 · %2").arg(r.label, QFileInfo(p).fileName()) : r.label;
            m_inst.append({nm, p});
            m_instanceBox->addItem(nm, p);
            ++added;
        }
    }
    saveInstances();
    QMessageBox::information(this, tr("掃描完成"),
                             added > 0 ? tr("新增了 %1 個實例。").arg(added)
                                       : tr("沒有找到新的實例。其他啟動器請用「＋ 新增」，或直接把資料夾拖進視窗。"));
    refreshFiles();
}

QString MainWindow::currentPath() const
{
    const int i = m_instanceBox->currentIndex();
    return (i >= 0 && i < m_inst.size()) ? m_inst[i].path : QString();
}

void MainWindow::refreshFiles()
{
    const QString p = currentPath();
    if (p.isEmpty()) {
        m_fileInfo->setText(tr("尚未選擇實例。按「＋ 新增」或把資料夾拖進視窗。"));
        return;
    }
    const GameFiles g = LogCleaner::locate(p);
    auto line = [&](const QString &label, const SourceFile &f) {
        return f.exists() ? QStringLiteral("<span style='color:#34d399'>●</span> %1　%2 · %3")
                                .arg(label, sizeText(f.size), ago(f.modified))
                          : QStringLiteral("<span style='color:#566070'>○</span> %1　—").arg(label);
    };
    m_fileInfo->setText(QStringLiteral("<span style='font-size:11px'>%1</span><br>%2<br>%3<br>%4")
                            .arg(g.gameDir.toHtmlEscaped(), line(QStringLiteral("latest.log"), g.latestLog),
                                 line(QStringLiteral("crash-report"), g.crashReport),
                                 line(QStringLiteral("hs_err"), g.hsErr)));
    m_seenCrashStamp = std::max(g.crashReport.modified, g.hsErr.modified);
    rewatch();
}

void MainWindow::rewatch()
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!m_optWatch->isChecked() || currentPath().isEmpty())
        return;
    const QString gd = LogCleaner::resolveGameDir(currentPath());
    for (const QString &d : {gd, gd + QStringLiteral("/crash-reports")})
        if (QFileInfo(d).isDir())
            m_watcher.addPath(d);
}

void MainWindow::checkNewCrash()
{
    const GameFiles g = LogCleaner::locate(currentPath());
    const QDateTime stamp = std::max(g.crashReport.modified, g.hsErr.modified);
    if (!stamp.isValid() || (m_seenCrashStamp.isValid() && stamp <= m_seenCrashStamp))
        return;
    m_seenCrashStamp = stamp;
    const SourceFile &f = g.hsErr.modified >= g.crashReport.modified ? g.hsErr : g.crashReport;
    m_bannerText->setText(tr("⚠️ 偵測到新的崩潰紀錄：<b>%1</b>").arg(QFileInfo(f.path).fileName().toHtmlEscaped()));
    m_banner->setVisible(true);
    refreshFiles();
    QApplication::alert(this, 0);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && QFileInfo(u.toLocalFile()).isDir())
            addInstance(u.toLocalFile());
}

// ---------------------------------------------------------------------------
// 整理
// ---------------------------------------------------------------------------
void MainWindow::runClean()
{
    if (currentPath().isEmpty()) {
        QMessageBox::information(this, tr("請先選擇實例"), tr("按「＋ 新增」選擇實例資料夾，或按「掃描啟動器」。"));
        return;
    }
    CleanOptions o;
    o.maxChars = m_maxChars->value();
    o.includeCrashReport = m_optCrash->isChecked();
    o.includeHsErr = m_optHs->isChecked();
    o.includeModList = m_optMods->isChecked();
    o.redact = m_optRedact->isChecked();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const CleanResult r = LogCleaner::clean(currentPath(), o);
    QApplication::restoreOverrideCursor();

    m_cleaned->setPlainText(r.text);
    m_tabs->setCurrentWidget(m_cleaned);
    if (r.text.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("沒有可分析的內容"),
                             tr("在「%1」找不到 logs/latest.log、crash-reports 或 hs_err。\n請確認選到的是遊戲目錄（裡面有 logs 資料夾）。").arg(r.files.gameDir));
        return;
    }
    QString s = tr("latest.log：%1 行 → 保留 %2 筆｜整理後 %3 字元（約 %4 tokens）")
                    .arg(r.originalLines).arg(r.keptLines).arg(r.text.size()).arg(int(r.text.size() / 3.2));
    if (!r.notes.isEmpty())
        s += QStringLiteral("｜") + r.notes.join(QStringLiteral("、"));
    m_stats->setText(s);
    refreshFiles();
}

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------
QString MainWindow::systemPrompt() const
{
    return QStringLiteral(
               "You are an expert Minecraft: Java Edition crash diagnostician. You know Fabric, Quilt, Forge, NeoForge, "
               "Mixin, JVM options, Java version requirements and GPU driver problems.\n"
               "The user sends a FILTERED log bundle: sections ## MODS, ## LATEST.LOG (filtered), ## CRASH REPORT, "
               "## JVM CRASH hs_err. Timestamps were removed, repeated messages are merged as (×N), long stack traces are "
               "folded, and personal data is redacted as <user>/<uuid>/<redacted>.\n"
               "Respond with:\n"
               "1. **Most likely cause** — one sentence.\n"
               "2. **Evidence** — quote the key log lines. Never invent log content that is not in the bundle.\n"
               "3. **Fix steps** — concrete, ordered (which mod to update/remove, Java version, memory, drivers...).\n"
               "4. **If uncertain** — say exactly what extra information is needed.\n"
               "Be concise. Write the answer in %1.")
        .arg(m_lang->currentText());
}

bool MainWindow::buildConfig(ProviderConfig *cfg)
{
    const Provider p = Provider(m_provider->currentData().toInt());
    auto &s = App::settings();
    cfg->type = p;
    switch (p) {
    case Provider::Groq:
        cfg->apiKey = SecureStore::load(QStringLiteral("groq_key"));
        cfg->endpoint = AiClient::groqEndpoint();
        cfg->model = s.value(QStringLiteral("groq/model"), QStringLiteral("llama-3.3-70b-versatile")).toString();
        cfg->temperature = 0.3;
        cfg->timeoutMs = 60000;
        if (cfg->apiKey.isEmpty()) {
            QMessageBox::information(this, tr("需要 Groq 金鑰"), tr("請先到「⚙ 設定」輸入 Groq API 金鑰。"));
            openSettings();
            return false;
        }
        return true;
    case Provider::GitHub:
        cfg->apiKey = SecureStore::load(QStringLiteral("github_pat"));
        cfg->endpoint = AiClient::githubEndpoint();
        cfg->model = s.value(QStringLiteral("github/model"), QStringLiteral("openai/gpt-4.1")).toString();
        cfg->timeoutMs = 90000; // 推理模型不送 temperature
        if (cfg->apiKey.isEmpty()) {
            QMessageBox::information(this, tr("需要 GitHub Token"), tr("請先到「⚙ 設定」建立並輸入 PAT（Models: read）。"));
            openSettings();
            return false;
        }
        return true;
    case Provider::Local:
        if (m_llama->serverExe().isEmpty() || !m_llama->modelInstalled()) {
            QMessageBox::information(this, tr("本地 LLM 尚未就緒"), tr("請先到「⚙ 設定 → 本地 LLM」下載 llama.cpp 與模型。"));
            openSettings();
            return false;
        }
        if (m_llama->state() != LlamaManager::State::Running) {
            if (m_llama->state() == LlamaManager::State::Stopped)
                m_llama->startServer(s.value(QStringLiteral("local/ctx"), 16384).toInt(),
                                     s.value(QStringLiteral("local/ngl"), 99).toInt());
            m_pendingAnalyze = true;
            setBusy(true, tr("載入本地模型中…"));
            return false;
        }
        cfg->endpoint = m_llama->chatEndpoint();
        cfg->model = QStringLiteral("local");
        cfg->temperature = 0.6; // 模型卡建議的取樣參數
        cfg->topP = 0.95;
        cfg->maxTokens = 6000;
        cfg->timeoutMs = 15 * 60 * 1000; // CPU 推理在第一個 token 前可能很久
        return true;
    }
    return false;
}

void MainWindow::runAnalyze()
{
    if (m_busy && !m_pendingAnalyze)
        return;
    if (m_cleaned->toPlainText().trimmed().isEmpty()) {
        runClean();
        if (m_cleaned->toPlainText().trimmed().isEmpty())
            return;
    }
    ProviderConfig cfg;
    if (!buildConfig(&cfg))
        return;
    m_cfg = cfg;
    m_pendingAnalyze = false;

    const QString note = m_note->text().trimmed();
    const int idx = m_instanceBox->currentIndex();
    const QString user = QStringLiteral("Instance: %1\nUser note: %2\n\nFiltered log bundle:\n```\n%3\n```")
                             .arg(idx >= 0 ? m_inst[idx].name : QString(), note.isEmpty() ? QStringLiteral("(none)") : note,
                                  m_cleaned->toPlainText());
    m_history = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), systemPrompt()}},
                           QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}};
    m_transcript.clear();
    m_raw.clear();
    m_reasoningSeen = false;
    m_hasAnswer = false;
    m_tabs->setCurrentWidget(m_answer);
    setBusy(true, tr("AI 分析中…"));
    renderAnswer();
    m_ai->send(m_cfg, m_history);
}

void MainWindow::sendFollowUp()
{
    const QString q = m_followUp->text().trimmed();
    if (q.isEmpty() || m_busy || !m_hasAnswer)
        return;
    if (m_cfg.type == Provider::Local && m_llama->state() != LlamaManager::State::Running) {
        QMessageBox::information(this, tr("本地伺服器已停止"), tr("請重新按「整理並請 AI 診斷」以啟動模型。"));
        return;
    }
    m_followUp->clear();
    m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), q}});
    m_transcript += QStringLiteral("\n\n---\n\n**🙋 %1**\n\n").arg(q.toHtmlEscaped());
    m_raw.clear();
    m_reasoningSeen = false;
    setBusy(true, tr("AI 分析中…"));
    renderAnswer();
    m_ai->send(m_cfg, m_history);
}

void MainWindow::setBusy(bool busy, const QString &msg)
{
    m_busy = busy;
    m_btnClean->setEnabled(!busy);
    m_btnAnalyze->setEnabled(!busy);
    m_btnCancel->setVisible(busy);
    m_provider->setEnabled(!busy);
    m_followUp->setEnabled(!busy && m_hasAnswer);
    m_btnFollow->setEnabled(!busy && m_hasAnswer);
    if (!msg.isEmpty())
        statusBar()->showMessage(msg, busy ? 0 : 6000);
}

// 隱藏推理模型的 <think> 區塊；也處理「模板已預先開啟 <think>、輸出只有 </think>」的情形
QString MainWindow::visibleAnswer() const
{
    static const QRegularExpression closed(QStringLiteral("<think>.*?</think>\\s*"),
                                           QRegularExpression::DotMatchesEverythingOption);
    QString t = m_raw;
    t.remove(closed);
    const int open = t.indexOf(QStringLiteral("<think>"));
    if (open >= 0)
        t.truncate(open);
    const int orphan = t.indexOf(QStringLiteral("</think>"));
    if (orphan >= 0)
        t = t.mid(orphan + 8);
    return t.trimmed().isEmpty() ? QString() : t;
}

void MainWindow::scheduleRender()
{
    if (!m_renderTimer.isActive())
        m_renderTimer.start();
}

void MainWindow::renderAnswer()
{
    QString md = m_transcript + visibleAnswer();
    if (m_busy && visibleAnswer().isEmpty())
        md += m_reasoningSeen || m_raw.contains(QStringLiteral("<think>")) || !m_raw.isEmpty()
                  ? tr("\n\n_🧠 模型思考中…_") : tr("\n\n_⏳ 等待回應…_");
    QScrollBar *bar = m_answer->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    m_answer->setMarkdown(md);
    if (atBottom)
        bar->setValue(bar->maximum());
}

// ---------------------------------------------------------------------------
void MainWindow::updateProviderChip()
{
    const Provider p = Provider(m_provider->currentData().toInt());
    QString text;
    bool ok = false;
    switch (p) {
    case Provider::Groq:
        ok = SecureStore::has(QStringLiteral("groq_key"));
        text = ok ? tr("✔ 金鑰已設定") : tr("尚未設定金鑰");
        break;
    case Provider::GitHub:
        ok = SecureStore::has(QStringLiteral("github_pat"));
        text = ok ? tr("✔ Token 已設定") : tr("尚未設定 Token");
        break;
    case Provider::Local: {
        const bool ready = !m_llama->serverExe().isEmpty() && m_llama->modelInstalled();
        ok = ready;
        text = m_llama->state() == LlamaManager::State::Running ? tr("● 伺服器執行中")
               : m_llama->state() == LlamaManager::State::Starting ? tr("載入中…")
               : ready ? tr("✔ 已安裝（按診斷時自動啟動）") : tr("尚未安裝 llama.cpp / 模型");
        break;
    }
    }
    m_providerChip->setText(text);
    m_providerChip->setObjectName(ok ? QStringLiteral("chipOk") : QStringLiteral("chip"));
    setProp(m_providerChip, "dummy", ok);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_llama, this);
    dlg.exec();
    updateProviderChip();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_ai->cancel();
    m_llama->cancelTask();
    m_llama->stopServer();
    auto &s = App::settings();
    s.setValue(QStringLiteral("opt/crash"), m_optCrash->isChecked());
    s.setValue(QStringLiteral("opt/hs"), m_optHs->isChecked());
    s.setValue(QStringLiteral("opt/mods"), m_optMods->isChecked());
    s.setValue(QStringLiteral("opt/redact"), m_optRedact->isChecked());
    s.setValue(QStringLiteral("opt/watch"), m_optWatch->isChecked());
    s.setValue(QStringLiteral("opt/maxChars"), m_maxChars->value());
    s.setValue(QStringLiteral("opt/lang"), m_lang->currentText());
    s.setValue(QStringLiteral("opt/provider"), m_provider->currentData());
    s.setValue(QStringLiteral("opt/instance"), m_instanceBox->currentIndex());
    s.sync();
    QMainWindow::closeEvent(e);
}
