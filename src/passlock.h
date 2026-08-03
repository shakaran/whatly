#ifndef PASSLOCK_H
#define PASSLOCK_H

#include <QString>

// App Lock passcode storage (issue #42). The passcode used to be stored as plain
// Base64 of the plaintext (reversible with `base64 -d`) and even shown in
// Settings. This stores a salted PBKDF2-SHA256 hash instead and verifies by
// hashing, so the stored value cannot be turned back into the passcode.
//
// Stored format: "pbkdf2_sha256$<iterations>$<salt_b64>$<hash_b64>".
// A value in any other shape is treated as the old Base64 form, so existing
// users are verified (and then transparently upgraded) rather than locked out.
// All functions are pure and unit-tested.
namespace PassLock {

// A fresh salted hash of `passcode` (new random salt each call).
QString hash(const QString &passcode);

// True when `stored` matches `passcode`, for both the new hash format and the
// legacy Base64 form. Uses a length-independent comparison for the hash.
bool verify(const QString &passcode, const QString &stored);

// True when `stored` is already the salted-hash format (no upgrade needed).
// A stored value that verifies but is not hashed should be re-stored via hash().
bool isHashed(const QString &stored);

} // namespace PassLock

#endif // PASSLOCK_H
