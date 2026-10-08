#include "Utils.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

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
      out += c;
      break;
    }
  }
  return out;
}

std::string EscapeJsStringLiteral(const std::string &input)
{
  // For simplicity reuse same escaping as JSON for string literals
  return EscapeJsonString(input);
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

} // namespace util
