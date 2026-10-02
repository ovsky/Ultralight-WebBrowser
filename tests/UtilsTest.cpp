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
    std::string s = "Line\nReturn\rTab\tQuote\"\\Back";
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
    // The hex escapes must be closed off before the 'b': a C++ hex escape is
    // greedy, so "\xA8b" would parse as the single value 0xA8B and overflow.
    CheckEq(EscapeJsStringLiteral("a\xE2\x80\xA8" "b"), "a\\u2028b",
            "EscapeJsStringLiteral escapes U+2028 in context");

    // Escaping must be applied everywhere, not just to the first occurrence.
    CheckEq(EscapeJsStringLiteral("'x'"), "\\'x\\'",
            "EscapeJsStringLiteral escapes every single quote");
    CheckEq(EscapeJsStringLiteral("\xE2\x80\xA8\xE2\x80\xA9"), "\\u2028\\u2029",
            "EscapeJsStringLiteral escapes repeated line terminators");

    // A backslash immediately before a single quote. EscapeJsonString doubles the
    // backslash, then the quote is escaped as \' (these values land inside
    // single-quoted JS literals), so nothing may be double-escaped.
    CheckEq(EscapeJsStringLiteral("\\'"), "\\\\\\'", "EscapeJsStringLiteral handles backslash-then-quote");
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
    using namespace util;

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
    using namespace util;

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

// The tab switcher (Ctrl+Shift+A) injects a tab list into the page. Page titles
// and URLs are attacker-controlled, so a title containing a quote, a closing
// script tag or a backslash must not be able to break out of the JSON string
// that carries it. The payload is built by UI::BuildTabSearchJSON from these
// escapers, so exercising them here covers the injection surface.
static void TestTabSearchPayloadEscaping()
{
    using namespace util;

    // A hostile page title: JSON metacharacters plus the sequence that would
    // close a script context if the value were ever interpolated unescaped.
    const std::string hostile = "\",\"__proto__\":{\"x\":\"\\ </script><img src=x onerror=alert(1)>";
    const std::string escaped = EscapeJsonString(hostile);

    // After escaping, the only quotes present are the ones JSON itself requires
    // as delimiters, and there are none at all inside the value, so it cannot
    // terminate the string early.
    Check(escaped.find("\\\"") != std::string::npos,
          "tab search payload escapes embedded quotes");
    Check(escaped.find("\\\\") != std::string::npos,
          "tab search payload escapes embedded backslashes");

    // The payload is handed to EvaluateScript as a JS string literal, so the JS
    // escaper has to neutralise both quote styles and the tag-terminator.
    const std::string js = EscapeJsStringLiteral(hostile);
    Check(js.find("\\'") != std::string::npos || js.find("\\\"") != std::string::npos,
          "tab search payload escapes quotes for the JS literal");
    Check(js.find("</script>") == std::string::npos,
          "tab search payload neutralises a closing script tag");

    // A URL containing a query string is the common case; it must survive intact
    // so the switcher shows the real address.
    CheckEq(EscapeJsonString("https://example.com/?a=1&b=2"),
            "https://example.com/?a=1&b=2",
            "tab search payload leaves ordinary URLs untouched");
}

// Regression cover for the '<' escaping and for the quote-breakout shape that
// was actually exploitable.
//
// '<' is escaped so a value interpolated into markup cannot close an enclosing
// element. The single-quote case matters most: a stored credential username is
// site-controlled (a page can autofill one containing a quote, and it is saved
// verbatim when the user accepts the save bar), and escaping only " & < while
// leaving ' raw let the value terminate a JS string literal and run as code.
static void TestAttributeBreakoutEscaping()
{
    using namespace util;

    CheckEq(EscapeJsStringLiteral("<"), "\\u003C",
            "EscapeJsStringLiteral escapes '<' so it cannot close markup");
    CheckEq(EscapeJsStringLiteral("a<b>c"), "a\\u003Cb>c",
            "EscapeJsStringLiteral escapes '<' but leaves '>' (it cannot open a tag)");

    // The tag terminator must not survive in any form.
    Check(EscapeJsStringLiteral("</script>").find("</script>") == std::string::npos,
          "EscapeJsStringLiteral neutralises </script>");
    Check(EscapeJsStringLiteral("<img src=x onerror=alert(1)>").find("<img") == std::string::npos,
          "EscapeJsStringLiteral neutralises an injected tag");

    // The realistic credential payload: a quote tries to end the JS literal and
    // the remainder tries to define a handler.
    const std::string hostile = "x');alert(document.cookie);//";
    const std::string escaped = EscapeJsStringLiteral(hostile);
    CheckEq(escaped, "x\\');alert(document.cookie);//",
            "EscapeJsStringLiteral keeps a quote from ending the literal");
    // After escaping, the value contains no raw quote that could terminate it.
    Check(escaped.find("x');") == std::string::npos,
          "EscapeJsStringLiteral leaves no unescaped quote terminator");

    // EscapeJsonString is the weaker helper and deliberately leaves ' and <
    // alone; assert that so a future change to it is deliberate.
    CheckEq(EscapeJsonString("it's <b>"), "it's <b>",
            "EscapeJsonString leaves quotes and angle brackets for the JS path");
}

// The JSON scanners parse every persisted file (settings, history, session,
// closed tabs). They were private to UI.cpp with no coverage until this
// extraction; these cases pin the behaviour the callers depend on.
static void TestFindMatchingBrace()
{
    using namespace util;

    const std::string doc = "{\"a\":{\"b\":1},\"c\":2}";
    const auto open = doc.find('{');
    Check(FindMatchingBrace(doc, open) == doc.size() - 1,
          "FindMatchingBrace balances nested objects");

    const std::string nested = "{\"a\":{\"b\":{\"c\":1}},\"d\":2}";
    const auto nested_open = nested.find('{');
    CheckEq(std::to_string(FindMatchingBrace(nested, nested_open)), "24",
            "FindMatchingBrace balances nested objects");

    // Braces inside a string must not affect the depth count.
    const std::string in_string = "{\"a\":\"}{\",\"b\":1}";
    Check(FindMatchingBrace(in_string, 0) == in_string.size() - 1,
          "FindMatchingBrace ignores braces inside a string");

    // Escaped quote inside a string must not end the string early.
    const std::string escaped = "{\"a\":\"\\\"}\",\"b\":1}";
    Check(FindMatchingBrace(escaped, 0) == escaped.size() - 1,
          "FindMatchingBrace handles an escaped quote");

    Check(FindMatchingBrace("{", 0) == std::string::npos,
          "FindMatchingBrace reports an unterminated object");
    Check(FindMatchingBrace("no braces here", 0) == std::string::npos,
          "FindMatchingBrace reports a missing opener");
}

static void TestExtractJsonStringField()
{
    using namespace util;

    std::string out;
    Check(ExtractJsonStringField("{\"url\":\"https://x/\"}", "url", out) &&
              out == "https://x/",
          "ExtractJsonStringField reads a string field");

    // The documents these parse are written by EscapeJsonString, so the escape
    // sequences have to be decoded rather than returned verbatim.
    out.clear();
    Check(ExtractJsonStringField("{\"t\":\"a\\\"b\\\\c\\nd\"}", "t", out) &&
              out == "a\"b\\c\nd",
          "ExtractJsonStringField decodes escapes");

    Check(!ExtractJsonStringField("{\"url\":\"x\"}", "title", out),
          "ExtractJsonStringField reports a missing field");
    Check(!ExtractJsonStringField("{\"url\":null}", "url", out),
          "ExtractJsonStringField treats null as absent");
    Check(!ExtractJsonStringField("{\"url\":42}", "url", out),
          "ExtractJsonStringField rejects a non-string value");
    Check(!ExtractJsonStringField("{\"url\":\"unterminated", "url", out),
          "ExtractJsonStringField survives a truncated document");
    Check(!ExtractJsonStringField("{\"url\":\"x\"}", nullptr, out),
          "ExtractJsonStringField rejects a null field name");

    // A quoted field name cannot appear inside a value, because EscapeJsonString
    // escapes the quotes there. This is why searching for the quoted name is
    // safe; assert the property the search relies on.
    Check(EscapeJsonString("he said \"url\": \"evil\"") ==
              "he said \\\"url\\\": \\\"evil\\\"",
          "EscapeJsonString prevents a value from looking like a field");
}

static void TestExtractJsonBoolField()
{
    using namespace util;

    bool out = false;
    Check(ExtractJsonBoolField("{\"default\":true}", "default", out) && out,
          "ExtractJsonBoolField reads true");

    out = true;
    Check(ExtractJsonBoolField("{\"default\":false}", "default", out) && !out,
          "ExtractJsonBoolField reads false");

    // Compact form with no space after the colon.
    out = false;
    Check(ExtractJsonBoolField("{\"x\":true}", "x", out) && out,
          "ExtractJsonBoolField handles no space after the colon");

    Check(!ExtractJsonBoolField("{\"x\":null}", "x", out),
          "ExtractJsonBoolField reports null as absent");
    Check(!ExtractJsonBoolField("{\"x\":\"maybe\"}", "x", out),
          "ExtractJsonBoolField rejects a non-boolean value");
    // A truncated document whose boolean value is still complete parses fine -- the
    // value itself was not cut off, unlike the string case above.
    Check(ExtractJsonBoolField("{\"x\":true", "x", out),
          "ExtractJsonBoolField accepts a complete value in a truncated document");
    // A value cut off mid-token is not a boolean.
    Check(!ExtractJsonBoolField("{\"x\":tru", "x", out),
          "ExtractJsonBoolField rejects a partial boolean");
    Check(!ExtractJsonBoolField("{\"x\":true}", nullptr, out),
          "ExtractJsonBoolField rejects a null field name");
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
    TestTabSearchPayloadEscaping();
    TestAttributeBreakoutEscaping();
    TestFindMatchingBrace();
    TestExtractJsonStringField();
    TestExtractJsonBoolField();

    if (g_failures > 0)
    {
        std::fprintf(stderr, "UtilsTest: %d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("UtilsTest: OK\n");
    return 0;
}
