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

} // namespace util
