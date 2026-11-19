#pragma once
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

// 可續傳的檔案下載器（先寫 .part，完成後改名）。HuggingFace 與 GitHub 皆支援 Range。
class Downloader : public QObject
{
    Q_OBJECT
public:
    explicit Downloader(QObject *parent = nullptr);

    void start(const QUrl &url, const QString &destFile, bool resume);
    void cancel();
    bool isRunning() const { return !m_reply.isNull(); }

signals:
    void progress(qint64 received, qint64 total);
    void done(bool ok, const QString &error);

private:
    void onMeta();
    void onReadyRead();
    void onFinished();
    void fail(const QString &msg);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QString m_dest;
    QString m_failMsg;
    qint64 m_offset = 0;
    qint64 m_total = 0;
    bool m_metaHandled = false;
    bool m_canceled = false;
};
