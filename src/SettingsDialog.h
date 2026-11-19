#pragma once
#include <QDialog>

#include "AiClient.h"
#include "LlamaManager.h"

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

    static QUrl githubTokenPrefillUrl();

private slots:
    void accept() override;

private:
    QWidget *buildGroqTab();
    QWidget *buildLocalTab();
    QWidget *buildGitHubTab();
    void refreshLocalStatus();
    void onModels(Provider p, const QStringList &ids);

    LlamaManager *m_llama;
    AiClient *m_ai;

    // Groq
    QLineEdit *m_groqKey = nullptr;
    QComboBox *m_groqModel = nullptr;
    QLabel *m_groqKeyState = nullptr;
    // GitHub
    QLineEdit *m_ghKey = nullptr;
    QComboBox *m_ghModel = nullptr;
    QLabel *m_ghKeyState = nullptr;
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
