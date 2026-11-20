#include "SecureStore.h"

#include "AppSettings.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

QString keyOf(const QString &name)
{
    return QStringLiteral("secrets/") + name;
}

#ifdef Q_OS_WIN
// 額外熵值：讓同一使用者的其他程式不能直接拿這段密文去 CryptUnprotectData
const QByteArray kEntropy = QByteArrayLiteral("MCDebugger.v1.secret");

QByteArray protectBytes(const QByteArray &plain)
{
    DATA_BLOB in{static_cast<DWORD>(plain.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB entropy{static_cast<DWORD>(kEntropy.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"MCDebugger", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotectBytes(const QByteArray &cipher)
{
    DATA_BLOB in{static_cast<DWORD>(cipher.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()))};
    DATA_BLOB entropy{static_cast<DWORD>(kEntropy.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#else
QByteArray protectBytes(const QByteArray &) { return {}; }
QByteArray unprotectBytes(const QByteArray &) { return {}; }
#endif

} // namespace

bool SecureStore::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool SecureStore::save(const QString &name, const QString &secret)
{
    if (secret.isEmpty()) {
        remove(name);
        return true;
    }
    const QByteArray cipher = protectBytes(secret.toUtf8());
    if (cipher.isEmpty())
        return false;
    App::settings().setValue(keyOf(name), QString::fromLatin1(cipher.toBase64()));
    App::settings().sync();
    return true;
}

QString SecureStore::load(const QString &name)
{
    const QByteArray b64 = App::settings().value(keyOf(name)).toString().toLatin1();
    if (b64.isEmpty())
        return {};
    const QByteArray plain = unprotectBytes(QByteArray::fromBase64(b64));
    return QString::fromUtf8(plain);
}

bool SecureStore::has(const QString &name)
{
    return App::settings().contains(keyOf(name));
}

void SecureStore::remove(const QString &name)
{
    App::settings().remove(keyOf(name));
    App::settings().sync();
}
