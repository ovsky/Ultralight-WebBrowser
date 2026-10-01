#include "../src/Utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_failures = 0;

static void Check(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

static void CheckEq(const std::string &actual, const std::string &expected, const char *what)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
                     what, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

static bool Contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

static void TestEscapeJsonString()
{
    using namespace util;

    // Backslash, quote and the three originally handled whitespace escapes.
    std::string s = "Line\nQuote\"\\Back";
    std::string e = EscapeJsonString(s);
    Check(Contains(e, "\\n"), "EscapeJsonString escapes newline");
    Check(Contains(e, "\\\""), "EscapeJsonString escapes double quote");
    Check(Contains(e, "\\\\"), "EscapeJsonString escapes backslash");
    Check(Contains(e, "\\r"), "EscapeJsonString escapes carriage return");
    Check(Contains(e, "\\t"), "EscapeJsonString escapes tab");

    // JSON forbids raw control characters inside strings. Before this was fixed,
    // a NUL or 0x01 byte in a download filename produced a JSON document that
    // JSON.parse() rejects.
    CheckEq(EscapeJsonString(std::string("a\0b", 3)), "a\\u0000b",
            "EscapeJsonString escapes NUL as \\u0000");
    CheckEq(EscapeJsonString(std::string("\x01\x1f")), "\\u0001\\u001f",
            "EscapeJsonString escapes other C0 controls as \\uXXXX");
    CheckEq(EscapeJsonString(std::string("\x7f")), "\x7f",
            "EscapeJsonString leaves DEL alone (valid in JSON)");

    // Short forms.
    CheckEq(EscapeJsonString("\b\f"), "\\b\\f", "EscapeJsonString escapes backspace and form feed");

    // U+2028 / U+2029 are legal inside a JSON string but terminate a JS string
    // literal, so they must be escaped for the JS path.
    CheckEq(EscapeJsonString("\xE2\x80\xA8"), "\xE2\x80\xA8",
            "EscapeJsonString leaves U+2028 raw (valid JSON)");

    CheckEq(EscapeJsonString(""), "", "EscapeJsonString handles empty input");
    CheckEq(EscapeJsonString("plain text"), "plain text",
            "EscapeJsonString leaves plain text untouched");
}

static void TestEscapeJsStringLiteral()
{
    using namespace util;

    // Values are interpolated into single-quoted JS literals in several
    // EvaluateScript() payloads, so an unescaped quote is a script-injection vector.
    CheckEq(EscapeJsStringLiteral("it's"), "it\\'s",
            "EscapeJsStringLiteral escapes single quote");
    CheckEq(EscapeJsStringLiteral("say \"hi\""), "say \\\"hi\\\"",
            "EscapeJsStringLiteral escapes double quote");
    CheckEq(EscapeJsStringLiteral("a\\b"), "a\\\\b",
            "EscapeJsStringLiteral escapes backslash");
    CheckEq(EscapeJsStringLiteral("line\nbreak"), "line\\nbreak",
            "EscapeJsStringLiteral escapes newline");

    // U+2028 (E2 80 A8) and U+2029 (E2 80 A9) must not survive raw.
    CheckEq(EscapeJsStringLiteral("\xE2\x80\xA8"), "\\u2028",
            "EscapeJsStringLiteral escapes U+2028");
    CheckEq(EscapeJsStringLiteral("\xE2\x80\xA9"), "\\u2029",
            "EscapeJsStringLiteral escapes U+2029");
    CheckEq(EscapeJsStringLiteral("a\xE2\x80\xA8b"), "a\\u2028b",
            "EscapeJsStringLiteral escapes U+2028 in context");

    // Escaping must be applied everywhere, not just to the first occurrence.
    CheckEq(EscapeJsStringLiteral("'x'"), "\\'x\\'",
            "EscapeJsStringLiteral escapes every single quote");
    CheckEq(EscapeJsStringLiteral("\xE2\x80\xA8\xE2\x80\xA9"), "\\u2028\\u2029",
            "EscapeJsStringLiteral escapes repeated line terminators");

    // A backslash immediately before a quote: EscapeJsonString already turned the
    // quote into \", so no raw quote may survive and nothing may be double-escaped.
    CheckEq(EscapeJsStringLiteral("\\'"), "\\\\\\\"",
            "EscapeJsStringLiteral handles backslash-then-quote");
}

static void TestEscapeShellArg()
{
    using namespace util;

    // Used to build `xdg-open '...'` / `open -R '...'` command lines, so a single
    // quote in a user-controlled path must not be able to break out.
    std::string quoted = EscapeShellArg("/tmp/it's here");
    CheckEq(quoted, "'/tmp/it'\\''s here'",
            "EscapeShellArg closes and reopens around an embedded quote");
    CheckEq(EscapeShellArg("plain"), "'plain'", "EscapeShellArg quotes simple input");
}

static void TestToIso8601UTC()
{
    auto now = std::chrono::system_clock::now();
    std::string t = ToIso8601UTC(now);
    Check(t.size() == 20, "ToIso8601UTC returns YYYY-MM-DDTHH:MM:SSZ");
    Check(t.size() > 4 && t[4] == '-', "ToIso8601UTC year separator");
    Check(t.size() > 10 && t[10] == 'T', "ToIso8601UTC date/time separator");
    Check(t.size() > 19 && t[19] == 'Z', "ToIso8601UTC UTC designator");
}

static void TestTrim()
{
    using namespace util;

    CheckEq(Trim("  hello  "), "hello", "Trim strips surrounding whitespace");
    CheckEq(Trim("hello"), "hello", "Trim leaves clean input alone");
    CheckEq(Trim("   "), "", "Trim returns empty for all-whitespace");
    CheckEq(Trim("\t\r\nvalue\n"), "value", "Trim strips tabs, CR and LF");
    CheckEq(Trim(""), "", "Trim handles empty input");
    CheckEq(Trim("a b"), "a b", "Trim preserves interior spaces");
}

static void TestToLower()
{
    using namespace util;

    CheckEq(ToLower("MiXeD"), "mixed", "ToLower lowercases ASCII");
    CheckEq(ToLower("already"), "already", "ToLower leaves lowercase alone");
    CheckEq(ToLower("123-_."), "123-_.", "ToLower leaves digits and punctuation alone");
    CheckEq(ToLower(""), "", "ToLower handles empty input");

    // High-bit bytes must not be treated as negative and mis-classified.
    std::string high = "\xC3\x84"; // U+00C4 in UTF-8
    CheckEq(ToLower(high), high, "ToLower leaves non-ASCII bytes unchanged");
}

static void TestGetEnvVar()
{
#if defined(_WIN32)
    _putenv_s("UITESTENV", "testval");
#else
    setenv("UITESTENV", "testval", 1);
#endif
    std::string v = GetEnvVar("UITESTENV");
    Check(v == "testval", "GetEnvVar reads a known environment variable");

    std::string missing = GetEnvVar("UITEST_DEFINITELY_UNSET_VAR");
    Check(missing.empty(), "GetEnvVar returns empty for an unset variable");
}

int main()
{
    TestEscapeJsonString();
    TestEscapeJsStringLiteral();
    TestEscapeShellArg();
    TestToIso8601UTC();
    TestTrim();
    TestToLower();
    TestGetEnvVar();

    if (g_failures > 0)
    {
        std::fprintf(stderr, "UtilsTest: %d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("UtilsTest: OK\n");
    return 0;
}
