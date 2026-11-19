#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include "Downloader.h"

// 管理「本地 LLM」管道：下載 llama.cpp（Windows 預編譯版）與 Qwythos 模型、啟動/停止 llama-server
class LlamaManager : public QObject
{
    Q_OBJECT
public:
    enum class Backend { Vulkan, Cpu, Cuda };
    enum class State { Stopped, Starting, Running };

    static constexpr const char *kDefaultModelUrl =
        "https://huggingface.co/empero-ai/Qwythos-9B-Claude-Mythos-5-1M-GGUF/resolve/main/"
        "Qwythos-9B-Claude-Mythos-5-1M-Q4_K_M.gguf";

    explicit LlamaManager(QObject *parent = nullptr);
    ~LlamaManager() override;

    static QString rootDir();
    QString serverExe() const;      // 空字串＝尚未安裝
    QString installedTag() const;
    QUrl modelUrl() const;
    QString modelPath() const;
    bool modelInstalled() const;
    bool modelPartial() const;      // 有 .part（可續傳）
    State state() const { return m_state; }
    QUrl chatEndpoint() const;
    bool busy() const { return m_busy; }

    void installServer(Backend backend);
    void downloadModel();
    void cancelTask();
    void startServer(int contextSize, int gpuLayers);
    void stopServer();

signals:
    void status(const QString &message);
    void progress(qint64 received, qint64 total, const QString &what);
    void taskFinished(bool ok, const QString &message);
    void stateChanged(LlamaManager::State state);
    void serverLog(const QString &line);

private:
    void setState(State s);
    void setBusy(bool b);
    void finishTask(bool ok, const QString &msg);
    void resolveTag();
    void downloadNext();
    void extractAll();
    void pollHealth();
    static QString powershellQuote(const QString &s);

    struct Job { QUrl url; QString file; QString label; };

    QNetworkAccessManager m_nam;
    Downloader m_dl;
    QProcess m_proc;
    QTimer m_healthTimer;
    QPointer<QNetworkReply> m_tagReply;
    State m_state = State::Stopped;
    bool m_busy = false;
    bool m_stopRequested = false;
    Backend m_backend = Backend::Vulkan;
    QString m_tag;
    QList<Job> m_jobs;
    QStringList m_zips;
    int m_port = 0;
    int m_healthTries = 0;
    QString m_lastLog;
};
