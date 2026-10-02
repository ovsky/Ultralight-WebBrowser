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

} // namespace util
