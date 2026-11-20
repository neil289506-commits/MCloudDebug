#pragma once
#include <QString>
#include <QStringList>

#include "AppSettings.h"

// 加在系統提示詞後面的三條規則（Groq 與 GitHub Copilot 共用）。
// 每一條都可以在「設定 → 提示詞規則」關掉。
namespace PromptRules {

inline bool searchEnabled() { return App::settings().value(QStringLiteral("rules/search"), true).toBool(); }
inline bool noGuessEnabled() { return App::settings().value(QStringLiteral("rules/noguess"), true).toBool(); }
inline bool deepThinkEnabled() { return App::settings().value(QStringLiteral("rules/deepthink"), true).toBool(); }

// canSearch：這個管道＋模型這次請求「真的有」網路搜尋工具時才為 true。
// 沒有工具卻叫模型去搜尋，只會讓它假裝搜尋過，所以那種情況不加第 1 條。
inline QString block(bool canSearch)
{
    QStringList rules;
    if (searchEnabled() && canSearch)
        rules << QStringLiteral(
            "Search the web before you answer: look up the exact mod, loader and Java versions, known issues and fixes "
            "that appear in the log, and prefer official sources (mod pages, GitHub issues, Mojang/Minecraft docs). "
            "List the URLs you actually used. Never cite a page you did not open.");
    if (noGuessEnabled())
        rules << QStringLiteral(
            "Do not guess. If you do not know, or the evidence is neither in the log nor in sources you found, say "
            "\"I don't know\" plainly and list what extra information would settle it. Never invent log lines, "
            "version numbers, mod names, URLs or fixes.");
    if (deepThinkEnabled())
        rules << QStringLiteral(
            "Think deeply: before answering, reason step by step, weigh at least two alternative causes against the "
            "evidence, and re-check your conclusion against the log lines before you state it.");
    if (rules.isEmpty())
        return {};

    QString out = QStringLiteral("\nAdditional rules (always follow them):\n");
    for (int i = 0; i < rules.size(); ++i)
        out += QStringLiteral("%1. %2\n").arg(i + 1).arg(rules.at(i));
    return out;
}

} // namespace PromptRules
