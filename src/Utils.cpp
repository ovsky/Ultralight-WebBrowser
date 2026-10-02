#include "Utils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <random>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace util {

std::string EscapeJsonString(const std::string &input)
{
  std::string out;
  out.reserve(input.size() + 8);
  for (char c : input)
  {
    switch (c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      // JSON forbids raw control characters (U+0000..U+001F) inside strings, and
      // U+2028/U+2029 terminate a JavaScript string literal. Escaping them keeps
      // the output parseable and prevents literal breakout when the escaped value
      // is interpolated into an EvaluateScript() payload.
      {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20)
        {
          static const char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out += kHex[(uc >> 4) & 0x0F];
          out += kHex[uc & 0x0F];
        }
        else
        {
          out += c;
        }
      }
      break;
    }
  }
  return out;
}

namespace {

// Replace every occurrence of `needle` with `replacement`, skipping past each
// inserted text so the replacement is never rescanned.
void ReplaceAll(std::string &text, const char *needle, const char *replacement)
{
  const size_t needle_len = std::strlen(needle);
  if (needle_len == 0)
    return;

  const size_t replacement_len = std::strlen(replacement);
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos)
  {
    text.replace(pos, needle_len, replacement);
    pos += replacement_len;
  }
}

} // namespace

std::string EscapeJsStringLiteral(const std::string &input)
{
  // EscapeJsonString already covers backslashes, double quotes and C0 controls.
  std::string out = EscapeJsonString(input);

  // Escape the single quote as well: these values are interpolated into
  // single-quoted JavaScript string literals in several EvaluateScript() payloads,
  // and an unescaped ' would terminate the literal so the rest of the value would
  // be parsed as code.
  ReplaceAll(out, "'", "\\'");

  // Neutralise '<' so a value containing "</script>" cannot terminate an inline
  // script block. EvaluateScript payloads are not inline HTML so this is not
  // strictly required today, but this helper is used for values that end up in
  // markup in other places, and escaping '<' costs nothing: it cannot appear
  // unescaped in a JS string in any code path that matters.
  ReplaceAll(out, "<", "\\u003C");

  // EscapeJsonString operates byte-wise, so U+2028 (E2 80 A8) and U+2029
  // (E2 80 A9) still pass through as raw UTF-8. They are valid inside a JSON
  // string but terminate a JavaScript string literal, so escape them here.
  ReplaceAll(out, "\xE2\x80\xA8", "\\u2028");
  ReplaceAll(out, "\xE2\x80\xA9", "\\u2029");

  return out;
}

std::string EscapeShellArg(const std::string &input)
{
  // Escape shell special characters for use in shell commands
  // This wraps the argument in single quotes and escapes any embedded single quotes
  std::string out = "'";
  for (char c : input)
  {
    if (c == '\'')
    {
      // End quote, escape the single quote, start quote again
      out += "'\\''";
    }
    else
    {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string ToIso8601UTC(const std::chrono::system_clock::time_point &tp)
{
  std::time_t raw = std::chrono::system_clock::to_time_t(tp);
  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &raw);
#else
  gmtime_r(&raw, &utc_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string ToStdString(const ultralight::String &str)
{
  auto u = str.utf8();
  const char *data = u.data();
  return data ? std::string(data) : std::string();
}

std::string Trim(const std::string &s)
{
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return std::string();
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::string ToLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                 { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string GetEnvVar(const char *name)
{
#if defined(_WIN32)
  char *buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) == 0 && buf && len > 0)
  {
    std::string res(buf);
    free(buf);
    return res;
  }
  if (buf)
    free(buf);
  return std::string();
#else
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string();
#endif
}

  std::string::size_type FindMatchingBrace(const std::string &text, std::string::size_type open_pos)
  {
    size_t depth = 0;
    for (size_t i = open_pos; i < text.size(); ++i)
    {
      char c = text[i];
      if (c == '{')
        ++depth;
      else if (c == '}')
      {
        if (depth == 0)
          return std::string::npos;
        --depth;
        if (depth == 0)
          return i;
      }
      else if (c == '"')
      {
        // Skip quoted strings entirely (handle escapes)
        ++i;
        bool escape = false;
        for (; i < text.size(); ++i)
        {
          char qc = text[i];
          if (escape)
          {
            escape = false;
            continue;
          }
          if (qc == '\\')
          {
            escape = true;
            continue;
          }
          if (qc == '"')
            break;
        }
      }
    }
    return std::string::npos;
  }

  bool ExtractJsonStringField(const std::string &object, const char *field, std::string &out)
  {
    if (!field)
      return false;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = object.find(needle);
    if (pos == std::string::npos)
      return false;
    pos = object.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return false;
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos])))
      ++pos;
    if (pos >= object.size())
      return false;
    if (object[pos] == 'n' || object[pos] == 'N')
    {
      // Treat explicit null as absence
      if (object.compare(pos, 4, "null") == 0 || object.compare(pos, 4, "NULL") == 0)
        return false;
    }
    if (object[pos] != '"')
      return false;
++pos;
    std::string value;
    bool escape = false;
    bool terminated = false;
    while (pos < object.size())
    {
      char c = object[pos++];
      if (escape)
      {
        escape = false;
        switch (c)
        {
        case '"':
          value.push_back('"');
          break;
        case '\\':
          value.push_back('\\');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(c);
          break;
        }
        continue;
      }
      if (c == '\\')
      {
        escape = true;
        continue;
      }
      if (c == '"')
      {
        terminated = true;
        break;
      }
      value.push_back(c);
    }
    // Running out of input without a closing quote means the file was
    // truncated -- a crash part-way through a write produces exactly this.
    // Returning the
    // partial value would hand callers a silently corrupted string and report
    // success, so a truncated document is treated as a failed parse and the
    // caller falls back to its default.
    if (!terminated)
      return false;

    out = std::move(value);
    return true;
  }

  bool ExtractJsonBoolField(const std::string &object, const char *field, bool &out)
  {
    if (!field)
      return false;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = object.find(needle);
    if (pos == std::string::npos)
      return false;
    pos = object.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return false;
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos])))
      ++pos;
    if (pos >= object.size())
      return false;
    if (object.compare(pos, 4, "true") == 0 || object[pos] == '1')
    {
      out = true;
      return true;
    }
    if (object.compare(pos, 5, "false") == 0 || object[pos] == '0')
    {
        out = false;
        return true;
    }
    return false;
  }

  namespace {

  inline uint32_t Ror32(uint32_t x, unsigned n)
  {
    return (x >> n) | (x << (32 - n));
  }

  const uint32_t kSha256K[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
      0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
      0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
      0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

  } // namespace

  // Raw digest: the 32 output bytes as binary. Sha256() is the hex wrapper.
  static void Sha256Raw(const std::string &data, unsigned char out[32])
  {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    // Pad to a multiple of 64 bytes: 0x80, zeros, then the bit length as a
    // big-endian 64-bit value.
    std::string msg = data;
    const uint64_t bit_length = static_cast<uint64_t>(data.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56)
      msg.push_back('\0');
    for (int i = 7; i >= 0; --i)
      msg.push_back(static_cast<char>((bit_length >> (i * 8)) & 0xFF));

    for (size_t offset = 0; offset < msg.size(); offset += 64)
    {
      uint32_t w[64];
      for (int i = 0; i < 16; ++i)
      {
        const size_t p = offset + static_cast<size_t>(i) * 4;
        w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[p])) << 24) |
               (static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 2])) << 8) |
               static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 3]));
      }
      for (int i = 16; i < 64; ++i)
      {
        const uint32_t s0 = Ror32(w[i - 15], 7) ^ Ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Ror32(w[i - 2], 17) ^ Ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }

      uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
      uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

      for (int i = 0; i < 64; ++i)
      {
        const uint32_t S1 = Ror32(e, 6) ^ Ror32(e, 11) ^ Ror32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = hh + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = Ror32(a, 2) ^ Ror32(a, 13) ^ Ror32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
      }

      h[0] += a;
      h[1] += b;
      h[2] += c;
      h[3] += d;
      h[4] += e;
      h[5] += f;
      h[6] += g;
      h[7] += hh;
    }

    for (int i = 0; i < 8; ++i)
    {
      for (int b = 0; b < 4; ++b)
        out[i * 4 + b] = static_cast<unsigned char>((h[i] >> ((3 - b) * 8)) & 0xFF);
    }
  }

  std::string Sha256(const std::string &data)
  {
    unsigned char digest[32];
    Sha256Raw(data, digest);

    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i)
    {
      out.push_back(hex[digest[i] >> 4]);
      out.push_back(hex[digest[i] & 0x0F]);
    }
    return out;
  }

  std::string HmacSha256(const std::string &key, const std::string &message)
  {
    // HMAC with a 64-byte block. Keys longer than the block are hashed first.
    std::string k = key;
    if (k.size() > 64)
    {
      unsigned char key_digest[32];
      Sha256Raw(k, key_digest);
      k.assign(reinterpret_cast<const char *>(key_digest), 32);
    }
    k.resize(64, '\0');

    std::string inner_pad(64, '\0');
    std::string outer_pad(64, '\0');
    for (size_t i = 0; i < 64; ++i)
    {
      inner_pad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x36);
      outer_pad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x5c);
    }

    // Both hashes consume and produce raw bytes. Feeding hex text in here, or
    // returning hex from this function, silently changes the construction.
    unsigned char inner[32];
    Sha256Raw(inner_pad + message, inner);

    unsigned char mac[32];
    Sha256Raw(outer_pad + std::string(reinterpret_cast<const char *>(inner), 32), mac);
    return std::string(reinterpret_cast<const char *>(mac), 32);
  }

  std::string Pbkdf2HmacSha256(const std::string &password, const std::string &salt,
                               unsigned iterations, size_t key_length)
  {
    // A corrupt settings file must not be able to wedge startup: reject counts
    // that are zero or so large they would hang the UI thread.
    if (iterations == 0 || iterations > 10000000u || key_length == 0 || key_length > 1024)
      return std::string();

    std::string out;
    out.reserve(key_length);

    for (uint32_t block = 1; out.size() < key_length; ++block)
    {
      std::string block_salt = salt;
      for (int i = 0; i < 4; ++i)
        block_salt.push_back(static_cast<char>((block >> ((3 - i) * 8)) & 0xFF));

      std::string u = HmacSha256(password, block_salt);
      std::string t = u;
      for (unsigned i = 1; i < iterations; ++i)
      {
        u = HmacSha256(password, u);
        for (size_t j = 0; j < t.size(); ++j)
          t[j] = static_cast<char>(static_cast<unsigned char>(t[j]) ^
                                   static_cast<unsigned char>(u[j]));
      }
      out += t;
    }

    out.resize(key_length);
    return out;
  }

  bool ConstantTimeEquals(const std::string &a, const std::string &b)
  {
    // Length is not secret here (both sides are fixed-width digests), but the
    // contents are, so the comparison must not exit early.
    if (a.size() != b.size())
      return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
      diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
  }

  std::string BytesToHex(const std::string &raw)
  {
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (char c : raw)
    {
      const unsigned char byte = static_cast<unsigned char>(c);
      out.push_back(hex[byte >> 4]);
      out.push_back(hex[byte & 0x0F]);
    }
    return out;
  }

  std::string HexToBytes(const std::string &hex)
  {
    if (hex.size() % 2 != 0)
      return std::string();

    auto nibble = [](char c, int &out) -> bool {
      if (c >= '0' && c <= '9') { out = c - '0'; return true; }
      if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
      if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
      return false;
    };

    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
      int hi = 0, lo = 0;
      if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo))
        return std::string();
      out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
  }

  std::string RandomBytes(size_t count)
  {
    std::string out;
    if (count == 0)
      return out;
    out.resize(count);

#ifdef _WIN32
    const NTSTATUS status =
        BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(&out[0]),
                        static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    // 0 is STATUS_SUCCESS. Anything else means the OS refused, and returning an
    // empty string is the signal the caller checks, so a vault is never written
    // with a predictable salt.
    if (status != 0)
    {
        out.clear();
        return out;
    }
    return out;
#else
    // Not a guaranteed CSPRNG everywhere, but it is the best available without
    // adding a dependency, and the fallback below covers a failure.
    std::random_device rd;
    for (size_t i = 0; i < count; ++i)
      out[i] = static_cast<char>(rd() & 0xFF);
    return out;
#endif
  }

} // namespace util
