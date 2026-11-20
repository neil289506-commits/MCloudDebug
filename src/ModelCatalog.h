#pragma once
#include <QString>
#include <QStringList>

// 模型相關的小型查表。這裡的每一條都附上依據；沒有依據的不寫。
namespace ModelCatalog {

// ---- Groq -----------------------------------------------------------------
// 自動挑「最強推理模型」時的優先順序（只在已儲存的模型已不存在時使用）。
// meta-models/muse-spark-open 與 minimaxai/minimax-m2.7 在 Groq 模型頁被標為 Enterprise／Preview，
// 一般金鑰不一定能用，所以不放進自動挑選；仍可在清單中手動選。
inline QStringList groqPreference()
{
    return {QStringLiteral("openai/gpt-oss-120b"), QStringLiteral("qwen/qwen3.8-27b"),
            QStringLiteral("openai/gpt-oss-20b")};
}

inline QString pickGroqBest(const QStringList &available, const QString &fallback)
{
    for (const QString &p : groqPreference())
        if (available.contains(p))
            return p;
    return available.isEmpty() ? fallback : available.first();
}

// Groq 文件（/docs/reasoning）列出支援 reasoning_effort 的模型：gpt-oss 系列、qwen3.x、muse-spark-open
inline bool groqSupportsReasoningEffort(const QString &id)
{
    return id.contains(QLatin1String("gpt-oss")) || id.contains(QLatin1String("qwen3"))
           || id.contains(QLatin1String("muse-spark"));
}

// Groq 文件（browser_search）列出的內建網路搜尋模型：gpt-oss-20b / gpt-oss-120b / gpt-oss-safeguard-20b
inline bool groqSupportsBrowserSearch(const QString &id)
{
    return id.contains(QLatin1String("gpt-oss"));
}

// ---- GitHub Copilot -------------------------------------------------------
// 官方「Supported AI models in GitHub Copilot」頁面列出的是顯示名稱，沒有 CLI 用的模型 ID。
// 下面的 ID 是依命名規則推導的（小寫、空白換成連字號）：這條規則對文件與 CLI 說明中出現過的
// 4 個 ID（gpt-5.3-codex、claude-haiku-4.5、gpt-6-astra、gpt-5.6-sol）都成立，但「沒有逐一驗證」。
// 設定頁的欄位可自行輸入任何 ID；你的方案實際能用哪些，以 Copilot CLI 的 /model 為準。
inline QStringList copilotSuggested()
{
    return {QStringLiteral("auto"),
            QStringLiteral("gpt-5.3-codex"),   QStringLiteral("gpt-5.6-luna"),     QStringLiteral("gpt-5.6-sol"),
            QStringLiteral("gpt-5.6-terra"),   QStringLiteral("gpt-6-astra"),      QStringLiteral("gpt-6-luna"),
            QStringLiteral("gpt-6-sol"),       QStringLiteral("gpt-6.1-sol"),      QStringLiteral("gpt-6.1-terra"),
            QStringLiteral("claude-fable-5"),  QStringLiteral("claude-fable-5.1"), QStringLiteral("claude-haiku-4.5"),
            QStringLiteral("claude-haiku-5.5"), QStringLiteral("claude-opus-4.8"), QStringLiteral("claude-opus-5"),
            QStringLiteral("claude-opus-5.5"), QStringLiteral("claude-sonnet-4.6"), QStringLiteral("claude-sonnet-5"),
            QStringLiteral("claude-sonnet-5.5"), QStringLiteral("gemini-3.8-flash"), QStringLiteral("gemini-4-argon"),
            QStringLiteral("mai-code-1.1-flash"), QStringLiteral("kimi-k3"),       QStringLiteral("grok-4.5"),
            QStringLiteral("grok-4.6"),        QStringLiteral("grok-4.7")};
}

} // namespace ModelCatalog
