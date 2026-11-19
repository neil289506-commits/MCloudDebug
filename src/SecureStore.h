#pragma once
#include <QString>

// 以 Windows DPAPI（CryptProtectData）加密儲存 API 金鑰。
// 密文綁定「目前 Windows 使用者帳號」，複製 settings.ini 到別台電腦／別的帳號無法解密。
// 非 Windows 平台一律拒絕儲存（fail closed），不會退回明文。
class SecureStore
{
public:
    static bool isSupported();
    static bool save(const QString &name, const QString &secret);
    static QString load(const QString &name);
    static bool has(const QString &name);
    static void remove(const QString &name);
};
