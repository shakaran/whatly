#include "passlock.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QStringList>
#include <QPasswordDigestor>
#include <QRandomGenerator>

namespace {
const char kPrefix[] = "pbkdf2_sha256$";
constexpr int kIterations = 210000; // OWASP-ish floor for PBKDF2-HMAC-SHA256
constexpr int kSaltBytes = 16;
constexpr int kKeyBytes = 32;

// Length-independent byte comparison, so verifying does not leak how many
// leading bytes matched via timing.
bool constantTimeEquals(const QByteArray &a, const QByteArray &b) {
  if (a.size() != b.size())
    return false;
  int diff = 0;
  for (int i = 0; i < a.size(); ++i)
    diff |= (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
  return diff == 0;
}
} // namespace

namespace PassLock {

QString hash(const QString &passcode) {
  QByteArray salt(kSaltBytes, '\0');
  QRandomGenerator::system()->generate(salt.begin(), salt.end());
  const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
      QCryptographicHash::Sha256, passcode.toUtf8(), salt, kIterations,
      kKeyBytes);
  return QString::fromLatin1(kPrefix) + QString::number(kIterations) +
         QLatin1Char('$') + QString::fromLatin1(salt.toBase64()) +
         QLatin1Char('$') + QString::fromLatin1(key.toBase64());
}

bool isHashed(const QString &stored) {
  return stored.startsWith(QLatin1String(kPrefix));
}

bool verify(const QString &passcode, const QString &stored) {
  if (isHashed(stored)) {
    const QStringList parts = stored.split(QLatin1Char('$'));
    // "pbkdf2_sha256", iterations, salt, hash
    if (parts.size() != 4)
      return false;
    bool ok = false;
    const int iters = parts.at(1).toInt(&ok);
    if (!ok || iters <= 0)
      return false;
    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
    if (salt.isEmpty() || expected.isEmpty())
      return false;
    const QByteArray got = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, passcode.toUtf8(), salt, iters,
        expected.size());
    return constantTimeEquals(got, expected);
  }
  // Legacy: Base64 of the plaintext passcode.
  const QByteArray legacy = QByteArray::fromBase64(stored.toUtf8());
  return legacy == passcode.toUtf8();
}

} // namespace PassLock
