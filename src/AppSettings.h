#pragma once
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace App {

// INI 設定檔：%APPDATA%/MCDebugger/settings.ini（金鑰以 DPAPI 密文儲存，見 SecureStore）
inline QSettings &settings()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("MCDebugger"), QStringLiteral("settings"));
    return s;
}

// 大型檔案（llama.cpp、模型）放這裡：%LOCALAPPDATA%/MCDebugger
inline QString dataDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                      + QStringLiteral("/MCDebugger");
    QDir().mkpath(d);
    return d;
}

// ---- AI 預設值 --------------------------------------------------------------
// Groq：推理能力最強且為正式（Production）等級、同時支援推理強度與內建網路搜尋的模型。
// 依據：console.groq.com/docs/models、/docs/reasoning、/docs/tool-use/built-in-tools/browser-search
inline QString defaultGroqModel() { return QStringLiteral("openai/gpt-oss-120b"); }
// GitHub Copilot：預設交給 Copilot 自動挑選（CLI 的 --model auto）
inline QString defaultCopilotModel() { return QStringLiteral("auto"); }

inline constexpr int kAiSettingsVersion = 2;

// 舊版（v1）遺留的設定：
//  - Groq 預設模型 llama-3.3-70b-versatile 已被 Groq 宣布關閉
//  - 舊的 GitHub 管道用的是 GitHub Models，該服務已於 2026-07-30 退役
inline void migrateAiSettings()
{
    auto &s = settings();
    if (s.value(QStringLiteral("ai/ver"), 1).toInt() >= kAiSettingsVersion)
        return;
    s.setValue(QStringLiteral("groq/model"), defaultGroqModel());
    s.remove(QStringLiteral("github/model"));
    s.setValue(QStringLiteral("ai/ver"), kAiSettingsVersion);
    s.sync();
}

} // namespace App
