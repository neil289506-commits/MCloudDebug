#pragma once
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

// GitHub = GitHub Copilot（透過官方 Copilot CLI）。舊版的 GitHub Models 已於 2026-07-30 退役。
enum class Provider { Groq, Local, GitHub };

struct ProviderConfig
{
    Provider type = Provider::Groq;
    QUrl endpoint;      // chat/completions 完整網址
    QString apiKey;
    QString model;
    double temperature = -1; // <0 代表不送（GitHub Models 上的推理模型只接受預設值）
    double topP = -1;
    int maxTokens = 0;       // 0 代表不送
    int timeoutMs = 60000;   // 「多久沒收到任何資料」就視為逾時（本地 CPU 推理需要很長）

    // 推理與搜尋
    QString reasoningEffort;    // Groq：low / medium / high；空字串代表不送
    bool browserSearch = false; // Groq：啟用伺服器端內建工具 browser_search
    bool canSearch = false;     // 這次請求是否真的有網路搜尋能力（決定提示詞要不要加「搜尋網路」）

    // GitHub Copilot CLI
    QString cliPath;            // 使用者指定的 copilot 路徑；空字串代表自動尋找
    QString autoTier;           // efficiency / balance / intelligence；空字串代表不指定
    bool cliAllowTools = true;  // 允許 CLI 使用唯讀工具（網路搜尋／抓網頁）；shell 與寫檔一律封鎖
};

// 單一請求的串流客戶端：Groq 與本地 LLM 走 OpenAI 相容的 /chat/completions（SSE）
// GitHub Copilot 另走 CopilotClient（Copilot CLI 子行程）
class AiClient : public QObject
{
    Q_OBJECT
public:
    explicit AiClient(QObject *parent = nullptr);

    static QUrl groqEndpoint();
    static QString providerId(Provider p);

    void send(const ProviderConfig &cfg, const QJsonArray &messages);
    void cancel();
    bool busy() const { return !m_reply.isNull(); }

    // 取得 Groq 模型清單（/openai/v1/models）
    void fetchModels(const QString &apiKey);

signals:
    void contentDelta(const QString &text);
    void reasoningDelta(const QString &text);
    void finished();
    void failed(const QString &message);
    void modelsReady(const QStringList &ids);
    void modelsFailed(const QString &message);

private:
    void onReadyRead();
    void onFinished();
    void consumeBuffer(bool flush);
    void handleDataLine(const QByteArray &payload);
    static QString errorFromBody(const QByteArray &body);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buf;
    QByteArray m_raw;
    QString m_streamError;
    bool m_canceled = false;
    bool m_usedTools = false;
};
