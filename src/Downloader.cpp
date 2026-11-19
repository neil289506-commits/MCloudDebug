#include "Downloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

Downloader::Downloader(QObject *parent)
    : QObject(parent)
{
}

void Downloader::start(const QUrl &url, const QString &destFile, bool resume)
{
    cancel();
    m_canceled = false;
    m_failMsg.clear();
    m_metaHandled = false;
    m_dest = destFile;
    m_total = 0;
    QDir().mkpath(QFileInfo(destFile).absolutePath());

    const QString part = destFile + QStringLiteral(".part");
    m_offset = resume ? QFileInfo(part).size() : 0;
    if (!resume)
        QFile::remove(part);

    m_file.setFileName(part);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        emit done(false, tr("無法寫入檔案：%1").arg(part));
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MCDebugger/1.0"));
    req.setTransferTimeout(60000); // 60 秒沒有任何資料就視為中斷（可再按一次續傳）
    if (m_offset > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_offset) + "-");

    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, &Downloader::onMeta);
    connect(m_reply, &QNetworkReply::readyRead, this, &Downloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &Downloader::onFinished);
}

void Downloader::cancel()
{
    if (m_reply) {
        m_canceled = true;
        m_reply->abort();
    }
}

void Downloader::onMeta()
{
    if (m_metaHandled || !m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0 || (status >= 300 && status < 400))
        return; // 轉址中的中繼回應，等最終回應
    m_metaHandled = true;

    if (status == 206) {
        // Content-Range: bytes 123-456/789
        static const QRegularExpression re(QStringLiteral(R"(/(\d+)\s*$)"));
        const auto m = re.match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
        if (m.hasMatch())
            m_total = m.captured(1).toLongLong();
    } else if (status == 200) {
        if (m_offset > 0) { // 伺服器忽略 Range：從頭來
            m_file.resize(0);
            m_offset = 0;
        }
        m_total = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    }
}

void Downloader::onReadyRead()
{
    if (!m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        m_reply->readAll(); // 錯誤頁面不寫入檔案
        return;
    }
    if (!m_metaHandled)
        onMeta();
    const QByteArray data = m_reply->readAll();
    if (m_file.write(data) != data.size()) {
        fail(tr("磁碟寫入失敗（空間不足？）"));
        return;
    }
    emit progress(m_file.size(), m_total); // 以實際檔案大小為準（含續傳前已有的部分）
}

void Downloader::onFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    reply->deleteLater();
    m_reply.clear();
    m_file.flush();
    m_file.close();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_failMsg.isEmpty()) {
        const QString msg = m_failMsg;
        m_failMsg.clear();
        emit done(false, msg);
        return;
    }
    if (m_canceled) {
        emit done(false, tr("已取消（已下載的部分會保留，可續傳）"));
        return;
    }
    if (status == 416) { // 已經下載完整，Range 超界
        // 落入下方的「完成」流程
    } else if (reply->error() != QNetworkReply::NoError || status >= 400) {
        emit done(false, tr("下載失敗（HTTP %1）：%2").arg(status).arg(reply->errorString()));
        return;
    }

    const QString part = m_dest + QStringLiteral(".part");
    const qint64 size = QFileInfo(part).size();
    if (m_total > 0 && size != m_total) {
        emit done(false, tr("檔案大小不符（%1 / %2），請重新按下載以續傳").arg(size).arg(m_total));
        return;
    }
    QFile::remove(m_dest);
    if (!QFile::rename(part, m_dest)) {
        emit done(false, tr("無法將暫存檔改名為：%1").arg(m_dest));
        return;
    }
    emit progress(size, size);
    emit done(true, QString());
}

void Downloader::fail(const QString &msg)
{
    m_failMsg = msg;
    if (m_reply)
        m_reply->abort(); // 會觸發 onFinished，由它統一送出 done()
}
