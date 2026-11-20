#include "AiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
void applyCommonHeaders(QNetworkRequest &req, const QString &key)
{
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MCDebugger/1.0"));
    if (!key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
}
} // namespace

AiClient::AiClient(QObject *parent)
    : QObject(parent)
{
}

QUrl AiClient::groqEndpoint() { return QUrl(QStringLiteral("https://api.groq.com/openai/v1/chat/completions")); }

QString AiClient::providerId(Provider p)
{
    switch (p) {
    case Provider::Groq: return QStringLiteral("groq");
    case Provider::Local: return QStringLiteral("local");
    case Provider::GitHub: return QStringLiteral("github");
    }
    return {};
}

void AiClient::cancel()
{
    if (m_reply) {
        m_canceled = true;
        m_reply->abort();
    }
}

void AiClient::send(const ProviderConfig &cfg, const QJsonArray &messages)
{
    cancel();
    m_canceled = false;
    m_buf.clear();
    m_raw.clear();
    m_streamError.clear();
    m_usedTools = cfg.browserSearch;

    QJsonObject body;
    body.insert(QStringLiteral("model"), cfg.model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);
    if (cfg.temperature >= 0)
        body.insert(QStringLiteral("temperature"), cfg.temperature);
    if (cfg.topP > 0)
        body.insert(QStringLiteral("top_p"), cfg.topP);
    if (cfg.maxTokens > 0)
        body.insert(QStringLiteral("max_tokens"), cfg.maxTokens);
    if (!cfg.reasoningEffort.isEmpty())
        body.insert(QStringLiteral("reasoning_effort"), cfg.reasoningEffort);
    if (cfg.browserSearch) {
        // Groq 內建工具：由伺服器端執行搜尋，不需要我們處理 tool call
        body.insert(QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("browser_search")}}});
        body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    QNetworkRequest req(cfg.endpoint);
    applyCommonHeaders(req, cfg.apiKey);
    req.setTransferTimeout(cfg.timeoutMs);

    m_reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AiClient::onFinished);
}

void AiClient::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (m_raw.size() < 65536)
        m_raw += chunk;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
        return; // 錯誤本文不是 SSE，等 finished 時整包解析
    m_buf += chunk;
    consumeBuffer(false);
}

void AiClient::consumeBuffer(bool flush)
{
    int idx;
    while ((idx = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(idx).trimmed();
        m_buf.remove(0, idx + 1);
        if (line.startsWith("data:"))
            handleDataLine(line.mid(5).trimmed());
    }
    if (flush && !m_buf.isEmpty()) {
        const QByteArray line = m_buf.trimmed();
        m_buf.clear();
        if (line.startsWith("data:"))
            handleDataLine(line.mid(5).trimmed());
    }
}

void AiClient::handleDataLine(const QByteArray &payload)
{
    if (payload.isEmpty() || payload == "[DONE]")
        return;
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    if (root.contains(QStringLiteral("error"))) {
        m_streamError = errorFromBody(payload);
        return;
    }
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
    QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
    if (reasoning.isEmpty())
        reasoning = delta.value(QStringLiteral("reasoning")).toString();
    if (!reasoning.isEmpty())
        emit reasoningDelta(reasoning);
    const QString content = delta.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
        emit contentDelta(content);
}

QString AiClient::errorFromBody(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QJsonValue e = o.value(QStringLiteral("error"));
    if (e.isObject())
        return e.toObject().value(QStringLiteral("message")).toString();
    if (e.isString())
        return e.toString();
    const QString m = o.value(QStringLiteral("message")).toString();
    return m.isEmpty() ? QString::fromUtf8(body.left(300)) : m;
}

void AiClient::onFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    reply->deleteLater();
    m_reply.clear();
    if (m_canceled)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400 || reply->error() != QNetworkReply::NoError) {
        QString msg = errorFromBody(m_raw);
        if (msg.isEmpty())
            msg = reply->errorString();
        QString hint;
        if (status == 401 || status == 403)
            hint = tr("（金鑰無效、已過期，或權限不足）");
        else if (status == 413 || status == 429)
            hint = tr("（超過用量或單次 token 上限，請在左側調低「最大字元數」，或到設定降低推理強度／關閉網路搜尋後重試）");
        else if (status == 400 && m_usedTools)
            hint = tr("（若與網路搜尋工具有關，可到「設定 → Groq」關閉網路搜尋後重試）");
        else if (status == 0)
            hint = tr("（網路中斷或逾時）");
        emit failed(tr("HTTP %1：%2 %3").arg(status).arg(msg, hint).trimmed());
        return;
    }
    consumeBuffer(true);
    if (!m_streamError.isEmpty()) {
        emit failed(m_streamError);
        return;
    }
    emit finished();
}

void AiClient::fetchModels(const QString &apiKey)
{
    QNetworkRequest req(QUrl(QStringLiteral("https://api.groq.com/openai/v1/models")));
    applyCommonHeaders(req, apiKey);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status >= 400 || reply->error() != QNetworkReply::NoError) {
            const QString err = errorFromBody(body);
            emit modelsFailed(tr("HTTP %1：%2").arg(status).arg(err.isEmpty() ? reply->errorString() : err));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("data")).toArray();
        // 排除語音、內容審查／防護、已停用的 compound 等非一般聊天模型
        static const QStringList skip = {QStringLiteral("whisper"), QStringLiteral("tts"),      QStringLiteral("guard"),
                                         QStringLiteral("safeguard"), QStringLiteral("playai"), QStringLiteral("orpheus"),
                                         QStringLiteral("compound")};
        QStringList ids;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            if (o.contains(QStringLiteral("active")) && !o.value(QStringLiteral("active")).toBool(true))
                continue; // 已下架的模型
            bool bad = false;
            for (const QString &s : skip)
                bad = bad || id.contains(s, Qt::CaseInsensitive);
            if (!bad)
                ids << id;
        }
        ids.sort();
        if (ids.isEmpty())
            emit modelsFailed(tr("回應中沒有可用的模型"));
        else
            emit modelsReady(ids);
    });
}
