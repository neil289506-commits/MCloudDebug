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

} // namespace App
