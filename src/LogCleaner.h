#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>

struct CleanOptions
{
    int maxChars = 14000;       // 整理後總字元上限（約 4~5k tokens；Groq/GitHub 免費額度較小）
    bool includeCrashReport = true;
    bool includeHsErr = true;
    bool includeModList = true;
    bool redact = true;         // 遮蔽使用者名稱、token、UUID、IP:Port
};

struct SourceFile
{
    QString path;
    qint64 size = 0;
    QDateTime modified;
    bool exists() const { return !path.isEmpty(); }
};

struct GameFiles
{
    QString gameDir;
    SourceFile latestLog;
    SourceFile crashReport; // crash-reports/ 內最新的一份
    SourceFile hsErr;       // hs_err_pid*.log（JVM 原生崩潰）
};

struct CleanResult
{
    QString text;
    int originalLines = 0;
    int keptLines = 0;
    qsizetype originalChars = 0;
    GameFiles files;
    QStringList notes;
};

class LogCleaner
{
public:
    // 使用者可能選到 .minecraft、Prism 的實例資料夾、或 logs 資料夾本身，這裡統一解析成「遊戲目錄」
    static QString resolveGameDir(const QString &root);
    static GameFiles locate(const QString &root);
    static CleanResult clean(const QString &root, const CleanOptions &opt);
    static QString redactText(QString t);
};
