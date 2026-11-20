#pragma once
#include <QDialog>

#include "AiClient.h"
#include "LlamaManager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(LlamaManager *llama, QWidget *parent = nullptr);

private slots:
    void accept() override;

private:
    QWidget *buildGroqTab();
    QWidget *buildLocalTab();
    QWidget *buildCopilotTab();
    QWidget *buildRulesTab();
    void refreshLocalStatus();
    void refreshCopilotStatus();
    void fetchGroqModels(bool silent);
    void onGroqModels(const QStringList &ids);

    LlamaManager *m_llama;
    AiClient *m_ai;
    bool m_silentFetch = false;

    // 提示詞規則
    QCheckBox *m_ruleSearch = nullptr;
    QCheckBox *m_ruleNoGuess = nullptr;
    QCheckBox *m_ruleDeep = nullptr;
    // Groq
    QLineEdit *m_groqKey = nullptr;
    QComboBox *m_groqModel = nullptr;
    QComboBox *m_groqReason = nullptr;
    QCheckBox *m_groqSearch = nullptr;
    QLabel *m_groqKeyState = nullptr;
    // GitHub Copilot
    QLineEdit *m_ghKey = nullptr;
    QLabel *m_ghKeyState = nullptr;
    QLineEdit *m_cliPath = nullptr;
    QLabel *m_cliState = nullptr;
    QComboBox *m_ghModel = nullptr;
    QComboBox *m_ghTier = nullptr;
    QComboBox *m_ghReason = nullptr;
    QCheckBox *m_ghSearch = nullptr;
    // Local
    QLabel *m_srvState = nullptr;
    QLabel *m_modelState = nullptr;
    QLabel *m_runState = nullptr;
    QComboBox *m_backend = nullptr;
    QSpinBox *m_ctx = nullptr;
    QSpinBox *m_ngl = nullptr;
    QLineEdit *m_modelUrl = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_taskText = nullptr;
    QPushButton *m_btnServer = nullptr;
    QPushButton *m_btnModel = nullptr;
    QPushButton *m_btnCancel = nullptr;
    QPushButton *m_btnRun = nullptr;
};
