#pragma once
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QStringDecoder>
#include <QTimer>

#include "AiClient.h"

class QProcess;

// GitHub Copilot 管道：以子行程呼叫官方 Copilot CLI 的非互動模式（copilot -p … -s）。
//
// 為什麼不是 HTTP：舊的 GitHub Models 推論 API 已於 2026-07-30 退役；Copilot 沒有文件化、
// 可直接用 PAT 呼叫的 chat/completions 端點，官方提供的程式化入口是 Copilot CLI / Copilot SDK。
//
// 安全：提示詞以「參數」傳給子行程，不經過任何 shell（不使用 cmd.exe）；
// 即使 log 內容含引號、換行或 shell 特殊字元也不會被解析。
// CLI 的 shell 與寫檔工具一律以 --deny-tool 封鎖（deny 規則優先於所有 allow）。
class CopilotClient : public QObject
{
    Q_OBJECT
public:
    struct Launcher
    {
        QString program;
        QStringList prefixArgs; // 例如以 node 執行 npm-loader.js 時的腳本路徑
        bool ok() const { return !program.isEmpty(); }
        QString display() const { return prefixArgs.isEmpty() ? program : program + QLatin1Char(' ') + prefixArgs.join(QLatin1Char(' ')); }
    };

    explicit CopilotClient(QObject *parent = nullptr);

    // userPath 為空就自動尋找（PATH、npm 全域目錄、WinGet 連結目錄）
    static Launcher locate(const QString &userPath = QString());
    static QString workDir();

    void send(const ProviderConfig &cfg, const QJsonArray &messages);
    void cancel();
    bool busy() const;

signals:
    void contentDelta(const QString &text);
    void reasoningDelta(const QString &text); // 目前 CLI 的純文字輸出不含推理過程，保留以便與 AiClient 介面一致
    void finished();
    void failed(const QString &message);

private:
    static QString composePrompt(const QJsonArray &messages);
    void killTree();
    void armIdleTimer();

    QPointer<QProcess> m_proc;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QByteArray m_err;
    QTimer m_idle;
    bool m_canceled = false;
    bool m_timedOut = false;
    bool m_gotOutput = false;
};
