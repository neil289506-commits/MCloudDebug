#include "LogCleaner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace {

enum Level { Trace = 0, Debug, Info, Warn, Error, Fatal, NoLevel };

struct Entry
{
    Level level = Info;
    QString tag;
    QString msg;
    QStringList cont; // 堆疊追蹤等延續行
    bool skipCont = false;
};

Level levelOf(const QString &s)
{
    if (s == QLatin1String("TRACE")) return Trace;
    if (s == QLatin1String("DEBUG")) return Debug;
    if (s == QLatin1String("INFO")) return Info;
    if (s == QLatin1String("WARN")) return Warn;
    if (s == QLatin1String("ERROR")) return Error;
    if (s == QLatin1String("FATAL")) return Fatal;
    return NoLevel;
}

const char *levelName(Level l)
{
    static const char *n[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "?"};
    return n[l];
}

// 支援三種常見格式：
//  vanilla: [12:00:00] [Render thread/INFO]: msg
//  Forge  : [12Nov2026 12:00:00.123] [main/INFO] [net.minecraftforge.Foo/CORE]: msg
//  Fabric : [12:00:00] [main/INFO] (FabricLoader) msg
const QRegularExpression &headerRe()
{
    static const QRegularExpression re(QStringLiteral(
        R"(^\[[^\]]+\]\s*\[([^\]/]+)/(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\]\s*(?:(\[[^\]]*\]|\([^)]*\)))?\s*:?\s?(.*)$)"));
    return re;
}

// INFO 等級中「幾乎不會跟崩潰有關」的雜訊
const QRegularExpression &noiseRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\[CHAT\]|\[System\] \[CHAT\]|Reloading ResourceManager|Narrator library|OpenAL initialized|Sound engine started|Created: \d+x\d+|Loaded \d+ (recipes|advancements|sound)|Backend (library|API)|Preparing (spawn area|level)|Time elapsed|Saving (chunks|world)|<[^>]{1,32}> |\S+ (joined|left) the game|Starting minecraft server|Done \(|Registered \d+|Finished mapping)|(?:^|\s)\[STDOUT\]: <)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// 看起來跟問題有關的關鍵字（INFO/WARN 命中就保留）
const QRegularExpression &signalRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(\b(crash(ed)?|exception|errors?|fail(ed|ure|s)?|fatal|unable|cannot|can't|could not|couldn't|missing|incompatible|conflict(s|ing)?|mixin|requires?|depends|outofmemory|out of memory|opengl|glfw|driver|stopping|shutting down|exit code|unsupported|invalid|corrupt(ed)?|denied|timed? ?out|duplicate|not found|unresolved|invalid mod)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QStringList readLines(const QString &path, qint64 maxBytes = 24 * 1024 * 1024)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    bool partial = false;
    if (f.size() > maxBytes) { // 超大 log 只取尾端（崩潰通常在最後）
        f.seek(f.size() - maxBytes);
        partial = true;
    }
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    if (partial && !lines.isEmpty())
        lines.removeFirst(); // 第一行可能被截斷
    for (QString &l : lines)
        if (l.endsWith(QLatin1Char('\r')))
            l.chop(1);
    return lines;
}

QString trunc(const QString &s, int n)
{
    return s.size() <= n ? s : s.left(n) + QStringLiteral("…(+%1 chars)").arg(s.size() - n);
}

// 把數字與十六進位串正規化，用來判斷「同一種訊息」重複出現
QString dedupeKey(const QString &s)
{
    static const QRegularExpression num(QStringLiteral(R"(0x[0-9a-fA-F]+|\d+)"));
    QString k = s;
    k.replace(num, QStringLiteral("#"));
    return k.left(240);
}

SourceFile fileInfoOf(const QFileInfo &fi)
{
    SourceFile s;
    s.path = fi.absoluteFilePath();
    s.size = fi.size();
    s.modified = fi.lastModified();
    return s;
}

SourceFile newestIn(const QString &dir, const QStringList &filters)
{
    const QFileInfoList list = QDir(dir).entryInfoList(filters, QDir::Files, QDir::Time); // 新→舊
    return list.isEmpty() ? SourceFile{} : fileInfoOf(list.first());
}

// 堆疊追蹤格式化：每個區塊最多保留 maxFrames 個 frame，其餘折疊
QStringList formatCont(const QStringList &cont, int maxFrames)
{
    QStringList out;
    int frames = 0, skipped = 0;
    auto flush = [&] {
        if (skipped > 0)
            out << QStringLiteral("    … %1 more frames").arg(skipped);
        skipped = 0;
        frames = 0;
    };
    for (const QString &raw : cont) {
        const QString t = raw.trimmed();
        if (t.isEmpty())
            continue;
        if (t.startsWith(QLatin1String("at "))) {
            if (frames < maxFrames) {
                out << QStringLiteral("    ") + trunc(t, 200);
                ++frames;
            } else {
                ++skipped;
            }
        } else {
            flush();
            out << QStringLiteral("  ") + trunc(t, 400);
        }
        if (out.size() >= 80)
            break;
    }
    flush();
    return out;
}

struct Item
{
    int prio = 4;      // 0 最重要 … 4 最先被丟棄
    QString head;
    QString body;
    int count = 1;
    qsizetype chars() const { return head.size() + body.size() + 8; }
    QString render() const
    {
        QString s = head;
        if (count > 1)
            s += QStringLiteral("  (×%1)").arg(count);
        if (!body.isEmpty())
            s += QLatin1Char('\n') + body;
        return s;
    }
};

QString hardTrim(const QString &t, qsizetype maxChars, const QString &marker)
{
    if (t.size() <= maxChars)
        return t;
    const qsizetype headLen = maxChars * 35 / 100;
    const qsizetype tailLen = maxChars - headLen;
    return t.left(headLen) + QLatin1Char('\n') + marker + QLatin1Char('\n') + t.right(tailLen);
}

// ---- 各區段萃取 -------------------------------------------------------

QString extractLog(const QStringList &lines, const CleanOptions &opt, qsizetype budget,
                   QString *modsOut, int *keptLines, QStringList *notes)
{
    // 1. 切成 entries（沒有時間戳的行視為前一筆的延續，如堆疊追蹤）
    std::vector<Entry> es;
    es.reserve(static_cast<size_t>(lines.size() / 2));
    for (const QString &line : lines) {
        const auto m = headerRe().match(line);
        if (m.hasMatch()) {
            Entry e;
            e.level = levelOf(m.captured(2));
            e.tag = m.captured(3);
            if (e.tag.size() > 2)
                e.tag = e.tag.mid(1, e.tag.size() - 2);
            e.msg = m.captured(4);
            es.push_back(std::move(e));
        } else if (!es.empty()) {
            es.back().cont << line;
        } else if (!line.trimmed().isEmpty()) {
            Entry e;
            e.msg = line;
            es.push_back(std::move(e));
        }
    }
    const int n = static_cast<int>(es.size());

    // 2. Mod 清單（Fabric/Quilt：「Loading N mods:」後面接 "- id version"）
    static const QRegularExpression modsHead(QStringLiteral(R"(^Loading \d+ mods?:?\s*$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    for (Entry &e : es) {
        if (!modsHead.match(e.msg).hasMatch())
            continue;
        QStringList mods;
        for (const QString &c : std::as_const(e.cont)) {
            const QString t = c.trimmed();
            if (t.startsWith(QLatin1String("- ")))
                mods << t.mid(2);
        }
        if (!mods.isEmpty() && opt.includeModList)
            *modsOut = mods.join(QStringLiteral(", "));
        e.skipCont = true; // 清單已另外處理，不重複放進 log 區段
    }

    // 3. 決定每筆的優先級
    std::vector<int> prio(static_cast<size_t>(n), 99);
    auto lower = [&](int i, int p) {
        if (i >= 0 && i < n)
            prio[static_cast<size_t>(i)] = std::min(prio[static_cast<size_t>(i)], p);
    };
    for (int i = 0; i < n; ++i) {
        const Entry &e = es[static_cast<size_t>(i)];
        if (e.level <= Debug)
            continue; // DEBUG/TRACE 一律丟棄
        const bool noise = e.level == Info && noiseRe().match(e.msg).hasMatch();
        const bool sig = signalRe().match(e.msg).hasMatch();
        if (e.level >= Error) {
            lower(i, 0);
            for (int j = i - 1; j >= std::max(0, i - 3); --j) // 錯誤前的 3 筆上下文
                if (es[static_cast<size_t>(j)].level >= Info
                    && !noiseRe().match(es[static_cast<size_t>(j)].msg).hasMatch())
                    lower(j, 4);
        } else if (e.level == Warn) {
            lower(i, (!e.cont.isEmpty() || sig) ? 1 : 3);
        } else if (!noise && sig) {
            lower(i, 2);
        }
        if (!noise && i < 12)
            lower(i, 4); // 開頭：版本、載入器等環境資訊
        if (!noise && i >= n - 12)
            lower(i, 2); // 結尾：崩潰前最後在做什麼
    }

    // 4. 格式化 + 去重
    std::vector<Item> items;
    QHash<QString, int> seen;
    int dupDropped = 0;
    for (int i = 0; i < n; ++i) {
        const int p = prio[static_cast<size_t>(i)];
        if (p == 99)
            continue;
        const Entry &e = es[static_cast<size_t>(i)];
        Item it;
        it.prio = p;
        it.head = QStringLiteral("[%1] ").arg(QLatin1String(levelName(e.level)));
        if (!e.tag.isEmpty())
            it.head += e.tag + QStringLiteral(": ");
        it.head += trunc(e.msg, 500);
        if (!e.skipCont)
            it.body = formatCont(e.cont, 12).join(QLatin1Char('\n'));
        const QString key = dedupeKey(it.head + it.body.left(160));
        const auto f = seen.constFind(key);
        if (f != seen.constEnd()) {
            Item &first = items[static_cast<size_t>(f.value())];
            ++first.count;
            first.prio = std::min(first.prio, it.prio);
            ++dupDropped;
            continue;
        }
        seen.insert(key, static_cast<int>(items.size()));
        items.push_back(std::move(it));
    }
    if (dupDropped > 0)
        notes->append(QObject::tr("合併重複訊息 %1 筆").arg(dupDropped));

    // 5. 預算控制：依優先級 4→1 由舊到新丟棄，P0（ERROR/FATAL）最後才動
    std::vector<bool> dropped(items.size(), false);
    qsizetype total = 0;
    for (const Item &it : items)
        total += it.chars();
    for (int p = 4; p >= 1 && total > budget; --p)
        for (size_t i = 0; i < items.size() && total > budget; ++i)
            if (!dropped[i] && items[i].prio == p) {
                dropped[i] = true;
                total -= items[i].chars();
            }

    QStringList out;
    int kept = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (dropped[i])
            continue;
        out << items[i].render();
        ++kept;
    }
    *keptLines = kept;
    QString text = out.join(QLatin1Char('\n'));
    if (text.size() > budget) {
        text = hardTrim(text, budget, QStringLiteral("[… 中段因長度限制省略 …]"));
        notes->append(QObject::tr("僅剩 ERROR/FATAL 仍超過上限，已截去中段"));
    }
    return text;
}

QString extractCrashReport(const QString &path, qsizetype budget, QString *modsFallback)
{
    const QStringList lines = readLines(path, 4 * 1024 * 1024);
    static const QRegularExpression keys(QStringLiteral(
        R"(^\s*(Minecraft Version|Operating System|Java Version|Java VM Version|JVM Flags|Memory|Graphics card|GL (Caps|Renderer|Vendor|Version)|Renderer|Is Modded|Type|Launched Version|Suspected Mods?|Fabric Loader|Loader|CPU|Window size|Backend|Mod Version)\b)"));
    QStringList out;
    enum Sec { Main, Head, Details, ModList, Other } sec = Main;
    int mainFrames = 0, headLines = 0, modLines = 0;
    QStringList mods;
    for (const QString &raw : lines) {
        const QString t = raw.trimmed();
        if (t.startsWith(QLatin1String("A detailed walkthrough"))) {
            sec = Other;
            continue;
        }
        if (t.startsWith(QLatin1String("-- ")) && t.endsWith(QLatin1String(" --"))) {
            sec = t == QLatin1String("-- Head --")       ? Head
                  : t == QLatin1String("-- System Details --") ? Details
                                                               : Other;
            if (sec != Other)
                out << t;
            continue;
        }
        if (t.startsWith(QLatin1String("Suspected Mod"))) {
            out << t;
            continue;
        }
        if (t.startsWith(QLatin1String("Fabric Mods:")) || t.startsWith(QLatin1String("Mod List:"))
            || t.startsWith(QLatin1String("Loaded Mods:"))) {
            sec = ModList;
            continue;
        }
        switch (sec) {
        case Main:
            if (t.startsWith(QLatin1String("at "))) {
                if (++mainFrames > 25)
                    break;
            }
            if (!t.isEmpty() && !t.startsWith(QLatin1String("//")))
                out << trunc(raw, 400);
            break;
        case Head:
            if (!t.isEmpty() && ++headLines <= 30)
                out << trunc(raw, 300);
            break;
        case Details:
            if (keys.match(raw).hasMatch())
                out << trunc(raw.trimmed(), 300);
            break;
        case ModList:
            if (t.isEmpty()) {
                sec = Other;
            } else if (++modLines <= 80) {
                mods << trunc(t.simplified(), 120);
            }
            break;
        case Other:
            break;
        }
    }
    if (modsFallback && !mods.isEmpty())
        *modsFallback = mods.join(QStringLiteral("; "));
    return hardTrim(out.join(QLatin1Char('\n')), budget, QStringLiteral("[… 已省略 …]"));
}

QString extractHsErr(const QString &path, qsizetype budget)
{
    const QStringList lines = readLines(path, 4 * 1024 * 1024);
    QStringList out;
    int hash = 0, native = 0;
    bool inNative = false;
    static const QRegularExpression meta(QStringLiteral(R"(^(Host:|OS:|vm_info:|Command Line:|Time:))"));
    for (const QString &l : lines) {
        if (l.startsWith(QLatin1Char('#'))) {
            if (++hash <= 45)
                out << trunc(l, 300);
        } else if (l.startsWith(QLatin1String("Native frames:")) || l.startsWith(QLatin1String("Java frames:"))) {
            inNative = true;
            native = 0;
            out << l.trimmed();
        } else if (inNative) {
            if (l.trimmed().isEmpty() || ++native > 22)
                inNative = false;
            else
                out << trunc(l.trimmed(), 200);
        } else if (meta.match(l).hasMatch()) {
            out << trunc(l, 300);
        }
    }
    return hardTrim(out.join(QLatin1Char('\n')), budget, QStringLiteral("[… 已省略 …]"));
}

} // namespace

// ---------------------------------------------------------------------------

QString LogCleaner::resolveGameDir(const QString &root)
{
    QDir r(root);
    if (r.dirName() == QLatin1String("logs") && r.exists(QStringLiteral("latest.log"))) {
        r.cdUp();
        return r.absolutePath();
    }
    const QStringList cands = {r.absolutePath(), r.absoluteFilePath(QStringLiteral("minecraft")),
                               r.absoluteFilePath(QStringLiteral(".minecraft"))};
    for (const QString &c : cands)
        if (QFileInfo::exists(c + QStringLiteral("/logs/latest.log")))
            return c;
    for (const QString &c : cands)
        if (QFileInfo(c + QStringLiteral("/crash-reports")).isDir() || QFileInfo(c + QStringLiteral("/mods")).isDir()
            || QFileInfo(c + QStringLiteral("/logs")).isDir())
            return c;
    return r.absolutePath();
}

GameFiles LogCleaner::locate(const QString &root)
{
    GameFiles g;
    g.gameDir = resolveGameDir(root);
    const QFileInfo lg(g.gameDir + QStringLiteral("/logs/latest.log"));
    if (lg.exists())
        g.latestLog = fileInfoOf(lg);
    g.crashReport = newestIn(g.gameDir + QStringLiteral("/crash-reports"),
                             {QStringLiteral("crash-*.txt"), QStringLiteral("*.txt")});
    g.hsErr = newestIn(g.gameDir, {QStringLiteral("hs_err_pid*.log")});
    return g;
}

QString LogCleaner::redactText(QString t)
{
    struct Rule { QRegularExpression re; QString rep; };
    static const std::vector<Rule> rules = {
        {QRegularExpression(QStringLiteral(R"((?i)([A-Z]:[\\/]+Users[\\/]+)[^\\/\s"']+)")), QStringLiteral("\\1<user>")},
        {QRegularExpression(QStringLiteral(R"((/(?:Users|home)/)[^/\s"']+)")), QStringLiteral("\\1<user>")},
        {QRegularExpression(QStringLiteral(R"((?i)(--accessToken\s+|accessToken[=:]\s*|Session ID is |token:)[A-Za-z0-9._\-:]{8,})")),
         QStringLiteral("\\1<redacted>")},
        {QRegularExpression(QStringLiteral(R"((?i)(--uuid\s+|Setting user:\s*)\S+)")), QStringLiteral("\\1<player>")},
        {QRegularExpression(QStringLiteral(R"((?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b)")),
         QStringLiteral("<uuid>")},
        {QRegularExpression(QStringLiteral(R"(\b(?:\d{1,3}\.){3}\d{1,3}:\d{2,5}\b)")), QStringLiteral("<ip:port>")},
    };
    for (const Rule &r : rules)
        t.replace(r.re, r.rep);
    return t;
}

CleanResult LogCleaner::clean(const QString &root, const CleanOptions &opt)
{
    CleanResult r;
    r.files = locate(root);

    const qsizetype maxC = std::max(3000, opt.maxChars);
    const qsizetype modsCap = maxC * 15 / 100;
    const qsizetype crashCap = r.files.crashReport.exists() && opt.includeCrashReport ? maxC * 30 / 100 : 0;
    const qsizetype hsCap = r.files.hsErr.exists() && opt.includeHsErr ? maxC * 12 / 100 : 0;

    QString mods, crashText, hsText;
    if (opt.includeCrashReport && r.files.crashReport.exists())
        crashText = extractCrashReport(r.files.crashReport.path, crashCap, &mods);
    if (opt.includeHsErr && r.files.hsErr.exists())
        hsText = extractHsErr(r.files.hsErr.path, hsCap);

    QString logText;
    if (r.files.latestLog.exists()) {
        const QStringList lines = readLines(r.files.latestLog.path);
        r.originalLines = static_cast<int>(lines.size());
        r.originalChars = r.files.latestLog.size;
        QString logMods;
        const qsizetype others = std::min(modsCap, qsizetype(2500)) + crashCap + hsCap;
        const qsizetype logBudget = std::max<qsizetype>(2000, maxC - others);
        logText = extractLog(lines, opt, logBudget, &logMods, &r.keptLines, &r.notes);
        if (!logMods.isEmpty())
            mods = logMods; // 優先採用 latest.log 的清單
    } else {
        r.notes << QObject::tr("找不到 logs/latest.log");
    }

    QStringList sections;
    if (!mods.isEmpty() && opt.includeModList)
        sections << QStringLiteral("## MODS\n") + hardTrim(mods, modsCap, QStringLiteral("[… 已省略 …]"));
    if (!logText.isEmpty())
        sections << QStringLiteral("## LATEST.LOG (filtered)\n") + logText;
    if (!crashText.isEmpty())
        sections << QStringLiteral("## CRASH REPORT (%1)\n").arg(QFileInfo(r.files.crashReport.path).fileName()) + crashText;
    if (!hsText.isEmpty())
        sections << QStringLiteral("## JVM CRASH hs_err (%1)\n").arg(QFileInfo(r.files.hsErr.path).fileName()) + hsText;

    r.text = sections.join(QStringLiteral("\n\n"));
    if (opt.redact)
        r.text = redactText(r.text);
    return r;
}
