#pragma once

#include <string>
#include <chrono>
#include <Ultralight/Ultralight.h>

namespace util {

std::string EscapeJsonString(const std::string &input);
std::string EscapeJsStringLiteral(const std::string &input);
std::string EscapeShellArg(const std::string &input);
std::string ToIso8601UTC(const std::chrono::system_clock::time_point &tp);
std::string ToStdString(const ultralight::String &str);
std::string Trim(const std::string &s);
std::string ToLower(std::string s);
std::string GetEnvVar(const char *name);

// Minimal JSON scanning helpers for the machine-written files this project
// persists (settings, history, session, closed tabs). These are deliberately
// not a general parser: they find one field in one object and stop. They are
// memory-safe -- every scan is bounded by the length of the input, and a
// malformed or truncated document yields "not found" rather than a partial
// result.
//
// A field name is located by searching for the quoted name, which cannot match
// inside another string value because EscapeJsonString escapes the quotes
// there.
std::string::size_type FindMatchingBrace(const std::string &text, std::string::size_type open_pos);
bool ExtractJsonStringField(const std::string &object, const char *field, std::string &out);
bool ExtractJsonBoolField(const std::string &object, const char *field, bool &out);

// Password hashing for the master password.
//
// The vault used to store FNV-1a over "UltralightBrowser_" + password +
// "_Salt2024". FNV is a non-cryptographic hash, it is 64-bit, and the salt is a
// fixed string compiled into the binary, so anyone holding the settings file
// could recover a weak master password by brute force almost instantly. These
// helpers exist to replace it.
//
// The project links no crypto library, so SHA-256 and PBKDF2 are implemented
// here. They are covered by the RFC 6234 / RFC 6070 test vectors in
// tests/UtilsTest.cpp, so the implementations are pinned to the published
// values rather than to themselves.
std::string Sha256(const std::string &data);
std::string HmacSha256(const std::string &key, const std::string &message);

// PBKDF2-HMAC-SHA256. Returns an empty string if iterations is zero or absurdly
// large, so a corrupt settings file cannot turn unlock into a hang.
std::string Pbkdf2HmacSha256(const std::string &password, const std::string &salt,
                             unsigned iterations, size_t key_length);

// Constant-time comparison, for comparing secrets.
bool ConstantTimeEquals(const std::string &a, const std::string &b);

// Binary <-> hex, for persisting salts and derived keys. HexToBytes returns an
// empty string if the input is not valid hex of even length, so a corrupt
// settings file is rejected rather than silently half-decoded.
std::string BytesToHex(const std::string &raw);
std::string HexToBytes(const std::string &hex);

// Cryptographically-seeded random bytes. Uses BCryptGenRandom on Windows and
// std::random_device elsewhere; the latter is not guaranteed to be a CSPRNG on
// every platform, so callers that need real entropy should prefer this only as
// a fallback.
std::string RandomBytes(size_t count);

} // namespace util
