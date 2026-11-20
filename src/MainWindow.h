#pragma once
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QMainWindow>
#include <QTimer>

#include "AiClient.h"
#include "CopilotClient.h"
#include "LlamaManager.h"
#include "LogCleaner.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct Instance { QString name; QString path; };

    void buildUi();
    QFrame *makeCard(const QString &title, QLayout *content);
    void loadInstances();
    void saveInstances();
    void addInstance(const QString &path, QString name = QString());
    void scanLaunchers();
    QString currentPath() const;
    void refreshFiles();
    void rewatch();
    void checkNewCrash();

    void runClean();
    void runAnalyze();
    void sendFollowUp();
    bool buildConfig(ProviderConfig *cfg);
    QString systemPrompt(const ProviderConfig &cfg) const;
    void dispatchSend();
    void setBusy(bool busy, const QString &msg = QString());
    void openSettings();
    void updateProviderChip();

    QString visibleAnswer() const;
    void scheduleRender();
    void renderAnswer();

    // 資料
    QList<Instance> m_inst;
    LlamaManager *m_llama;
    AiClient *m_ai;
    CopilotClient *m_copilot;
    QFileSystemWatcher m_watcher;
    QTimer m_watchDebounce;
    QTimer m_renderTimer;
    QDateTime m_seenCrashStamp;

    QJsonArray m_history;
    ProviderConfig m_cfg;
    QString m_raw;          // 目前這一輪的原始串流輸出
    QString m_transcript;   // 先前輪次（已定稿）
    bool m_reasoningSeen = false;
    bool m_busy = false;
    bool m_pendingAnalyze = false;
    bool m_hasAnswer = false;

    // 介面
    QComboBox *m_instanceBox = nullptr;
    QLabel *m_fileInfo = nullptr;
    QCheckBox *m_optCrash = nullptr, *m_optHs = nullptr, *m_optMods = nullptr, *m_optRedact = nullptr, *m_optWatch = nullptr;
    QSpinBox *m_maxChars = nullptr;
    QComboBox *m_lang = nullptr;
    QComboBox *m_provider = nullptr;
    QLabel *m_providerChip = nullptr;
    QLineEdit *m_note = nullptr;
    QPushButton *m_btnClean = nullptr, *m_btnAnalyze = nullptr, *m_btnCancel = nullptr;
    QFrame *m_banner = nullptr;
    QLabel *m_bannerText = nullptr;
    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_cleaned = nullptr;
    QTextBrowser *m_answer = nullptr;
    QLineEdit *m_followUp = nullptr;
    QPushButton *m_btnFollow = nullptr, *m_btnCopy = nullptr, *m_btnSave = nullptr;
    QLabel *m_stats = nullptr;
};
