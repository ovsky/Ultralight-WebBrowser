#include "Utils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

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

} // namespace util
