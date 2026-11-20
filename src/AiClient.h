#pragma once
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

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
};

// 單一請求的串流客戶端：三個管道都走 OpenAI 相容的 /chat/completions（SSE）
class AiClient : public QObject
{
    Q_OBJECT
public:
    explicit AiClient(QObject *parent = nullptr);

    static QUrl groqEndpoint();
    static QUrl githubEndpoint();
    static QString providerId(Provider p);

    void send(const ProviderConfig &cfg, const QJsonArray &messages);
    void cancel();
    bool busy() const { return !m_reply.isNull(); }

    // 取得模型清單（Groq: /openai/v1/models；GitHub: models.github.ai/catalog/models）
    void fetchModels(Provider p, const QString &apiKey);

signals:
    void contentDelta(const QString &text);
    void reasoningDelta(const QString &text);
    void finished();
    void failed(const QString &message);
    void modelsReady(Provider p, const QStringList &ids);
    void modelsFailed(Provider p, const QString &message);

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
};
