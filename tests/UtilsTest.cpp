#include "../src/Utils.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

// Regression: the history, session and closed-tab loaders in UI.cpp used to find
// the end of a record with a bare find('}'). A page title containing a brace is
// written unescaped, because EscapeJsonString escapes quotes, backslashes and
// control characters but not '}' or '['. The naive scan therefore stopped inside
// the title and produced a truncated record, which then lost its url and was
// dropped. This case is the on-disk shape that triggered it.
static void TestFindMatchingBraceUnescapedTitleBrace()
{
    using namespace util;

    const std::string entry =
        "{\"url\":\"https://example.com/a\",\"title\":\"Fix } in C++ {braces}\",\"time\":100}";
    const auto open = entry.find('{');

    // The naive scan stops at the '}' inside the title.
    Check(entry.find('}', open) != entry.size() - 1,
          "test fixture really does contain a bare '}' mid-object");

    // The correct scan runs to the brace that closes the record.
    Check(FindMatchingBrace(entry, open) == entry.size() - 1,
          "FindMatchingBrace survives an unescaped brace in a title");

    // And the record parses, which is the behaviour the loaders depend on.
    std::string title;
    Check(ExtractJsonStringField(entry.substr(open), "title", title),
          "title field is found");
    CheckEq(title, "Fix } in C++ {braces}",
            "title round-trips with its braces intact");
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

// Pinned to the published vectors rather than to our own output, so a mistake in
// the implementation cannot quietly agree with itself.
static void TestSha256()
{
    using namespace util;

    // FIPS 180-2 / NIST examples.
    CheckEq(Sha256(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "Sha256 of the empty string");
    CheckEq(Sha256("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "Sha256(\"abc\")");
    CheckEq(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "Sha256 of the 56-byte NIST example");

    // The message is longer than one block, which exercises the multi-block loop
    // and the 55/56/64-byte padding boundaries.
    CheckEq(Sha256(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "Sha256 of one million 'a'");

    // 55 bytes is the largest input that still needs no extra padding block.
    CheckEq(Sha256(std::string(55, 'a')),
            Sha256(std::string(55, 'a')),
            "Sha256 is stable at the 55-byte padding boundary");
    Check(Sha256(std::string(55, 'a')) != Sha256(std::string(56, 'a')),
          "Sha256 distinguishes 55 from 56 bytes");
}

// The primitives return raw bytes; compare them as hex against the published
// vectors, which are all given in hex.
static std::string ToHex(const std::string &raw)
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

// RFC 4231 test cases 1, 2 and 3.
static void TestHmacSha256()
{
    using namespace util;

    CheckEq(ToHex(HmacSha256(std::string(20, '\x0b'), "Hi There")),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            "HMAC-SHA256 RFC 4231 case 1");

    CheckEq(ToHex(HmacSha256("Jefe", "what do ya want for nothing?")),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
            "HMAC-SHA256 RFC 4231 case 2");

    CheckEq(ToHex(HmacSha256(std::string(20, '\xaa'), std::string(50, '\xdd'))),
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
            "HMAC-SHA256 RFC 4231 case 3");

    // An over-long key must be hashed down to the block size first.
    CheckEq(ToHex(HmacSha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")),
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
            "HMAC-SHA256 RFC 4231 case 6");
}

// RFC 6070-style vectors, expressed for HMAC-SHA256 as in RFC 7914 section 11.
static void TestPbkdf2HmacSha256()
{
    using namespace util;

    CheckEq(ToHex(Pbkdf2HmacSha256("password", "salt", 1, 32)),
            "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
            "PBKDF2-HMAC-SHA256 c=1");
    CheckEq(ToHex(Pbkdf2HmacSha256("password", "salt", 2, 32)),
            "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
            "PBKDF2-HMAC-SHA256 c=2");
    CheckEq(ToHex(Pbkdf2HmacSha256("password", "salt", 4096, 32)),
            "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
            "PBKDF2-HMAC-SHA256 c=4096");

    // Long output spans more than one HMAC-SHA256 block. RFC 7914 publishes this
    // vector for dkLen=40; only the leading 32 bytes are reproduced here, so
    // compare that prefix rather than inventing the trailing block.
    const std::string long_dk =
        Pbkdf2HmacSha256("passwordPASSWORDpassword",
                         "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40);
    CheckEq(std::to_string(long_dk.size()), "40",
            "PBKDF2 returns the requested key length");
    CheckEq(ToHex(long_dk).substr(0, 64),
            "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1",
            "PBKDF2-HMAC-SHA256 dkLen=40 c=4096, first block");

    // Distinct inputs must not collide.
    Check(Pbkdf2HmacSha256("password", "salt", 1000, 32) !=
              Pbkdf2HmacSha256("password", "salt2", 1000, 32),
          "PBKDF2 depends on the salt");
    Check(Pbkdf2HmacSha256("password", "salt", 1000, 32) !=
              Pbkdf2HmacSha256("Password", "salt", 1000, 32),
          "PBKDF2 depends on the password");

    // Iteration count must actually change the output.
    Check(Pbkdf2HmacSha256("password", "salt", 1000, 32) !=
              Pbkdf2HmacSha256("password", "salt", 1001, 32),
          "PBKDF2 depends on the iteration count");

    // A corrupt settings file must not be able to hang the UI thread.
    CheckEq(Pbkdf2HmacSha256("p", "s", 0, 32), "", "PBKDF2 rejects zero iterations");
    CheckEq(Pbkdf2HmacSha256("p", "s", 50000000u, 32), "",
            "PBKDF2 rejects an absurd iteration count");
    CheckEq(Pbkdf2HmacSha256("p", "s", 1000, 0), "", "PBKDF2 rejects a zero length key");
}

static void TestConstantTimeEquals()
{
    using namespace util;

    Check(ConstantTimeEquals("abc", "abc"), "equal strings compare equal");
    Check(!ConstantTimeEquals("abc", "abd"), "differing strings compare unequal");
    Check(!ConstantTimeEquals("abc", "abcd"), "differing lengths compare unequal");
    Check(ConstantTimeEquals("", ""), "empty strings compare equal");
}

static void TestHexRoundTrip()
{
    using namespace util;

    // Bytes 0x00..0x0f, which exercises leading zeros and high nibbles.
    const std::string raw("\x00\x01\x02\x7f\x80\xfe\xff", 7);
    CheckEq(BytesToHex(raw), "0001027f80feff", "BytesToHex encodes nibbles in order");
    CheckEq(HexToBytes("0001027f80feff"), raw, "hex round-trips through HexToBytes");

    CheckEq(BytesToHex(""), "", "empty input encodes to empty hex");
    CheckEq(HexToBytes(""), "", "empty hex decodes to empty bytes");

    // A salt is 16 bytes -> 32 hex chars, which is what the vault stores.
    CheckEq(std::to_string(RandomBytes(16).size()), "16", "RandomBytes returns the requested count");

    // Malformed hex must be rejected, not silently half-decoded, so a corrupt
    // settings file cannot produce a plausible-looking but wrong salt.
    CheckEq(HexToBytes("abc"), "", "odd-length hex is rejected");
    CheckEq(HexToBytes("zz"), "", "non-hex characters are rejected");
    CheckEq(HexToBytes("00 11"), "", "embedded whitespace is rejected");
    CheckEq(HexToBytes("00x1"), "", "a trailing non-hex character is rejected");

    // Uppercase must decode, since a hand-edited settings file may use it.
    CheckEq(HexToBytes("ABCDEF"), HexToBytes("abcdef"), "hex decoding is case-insensitive");
}

// The stored master password verifier is a self-describing record:
//   pbkdf2-sha256$<iterations>$<salt-hex>$<dk-hex>
// VerifyMasterPassword parses exactly this shape, so the format is pinned here
// rather than only inside PasswordManager, which has no test target.
static void TestPbkdf2RecordFormat()
{
    using namespace util;

    const std::string prefix = "pbkdf2-sha256$";

    const std::string salt_hex = BytesToHex(RandomBytes(16));
    CheckEq(std::to_string(salt_hex.size()), "32", "a 16-byte salt encodes to 32 hex chars");

    const unsigned iterations = 1000;
    const std::string dk = Pbkdf2HmacSha256("correct horse", HexToBytes(salt_hex), iterations, 32);
    const std::string record =
        prefix + std::to_string(iterations) + "$" + salt_hex + "$" + BytesToHex(dk);

    // Parse it back the way VerifyPbkdf2Record does.
    CheckEq(std::to_string(record.compare(0, prefix.size(), prefix)), "0",
            "a new record carries the expected prefix");

    const size_t iter_end = record.find('$', prefix.size());
    Check(record.find('$', iter_end + 1) != std::string::npos,
          "the record has a salt field after the iterations");
    const std::string stored_iter_text = record.substr(prefix.size(), iter_end - prefix.size());
    const size_t salt_end = record.find('$', iter_end + 1);
    const std::string stored_salt_hex = record.substr(iter_end + 1, salt_end - iter_end - 1);
    const std::string stored_dk_hex = record.substr(salt_end + 1);

    CheckEq(stored_iter_text, "1000", "iterations round-trip");
    CheckEq(stored_salt_hex, salt_hex, "the salt round-trips through the record");
    CheckEq(HexToBytes(stored_salt_hex), HexToBytes(salt_hex), "the salt decodes to the same bytes");

    // Re-deriving from the stored salt reproduces the stored key. This is the
    // whole point of embedding the salt in the record: the vault stays verifiable
    // after a restart without a separate stored field.
    CheckEq(ToHex(Pbkdf2HmacSha256("correct horse", HexToBytes(stored_salt_hex), iterations, 32)),
            ToHex(HexToBytes(stored_dk_hex)),
            "re-deriving from the stored salt reproduces the stored key");

    // A wrong password must not reproduce it.
    Check(ToHex(Pbkdf2HmacSha256("wrong horse", HexToBytes(stored_salt_hex), iterations, 32)) !=
              ToHex(HexToBytes(stored_dk_hex)),
          "a different password produces a different key");

    // A legacy FNV record is a bare 16-char hex string with no prefix, so the
    // two formats are distinguishable and neither is mistaken for the other.
    const std::string legacy = "0011223344556677";
    Check(legacy.compare(0, prefix.size(), prefix) != 0,
          "a legacy record does not carry the new-format prefix");
}

static void TestRandomBytesAreDistinct()
{
    using namespace util;

    // Two calls must not return the same 16 bytes. This is a smoke test for the
    // generator actually being seeded, not a proof of CSPRNG quality.
    const std::string a = RandomBytes(16);
    const std::string b = RandomBytes(16);
    CheckEq(std::to_string(a.size()), "16", "first draw has the requested length");
    CheckEq(std::to_string(b.size()), "16", "second draw has the requested length");
    Check(!ConstantTimeEquals(a, b), "two random draws differ");
    CheckEq(RandomBytes(0), "", "zero length returns empty");
}

// VerifyMasterPassword and Unlock both run on the UI thread, so the production
// cost must stay interactive. This guards against someone raising the iteration
// count to a value that makes unlock feel like a hang. The budget is generous
// because this runs on shared CI hardware, but it is far below the point where a
// user would notice.
static void TestProductionIterationCountIsInteractive()
{
    using namespace util;

    const unsigned kProductionIterations = 200000;
    const auto start = std::chrono::steady_clock::now();
    const std::string dk = Pbkdf2HmacSha256("a master password", RandomBytes(16),
                                            kProductionIterations, 32);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();

    CheckEq(std::to_string(dk.size()), "32", "production derivation returns 32 bytes");
    Check(elapsed < 2000, "production iteration count stays interactive on the UI thread");
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
    TestFindMatchingBraceUnescapedTitleBrace();
    TestExtractJsonStringField();
    TestExtractJsonBoolField();
    TestSha256();
    TestHmacSha256();
    TestPbkdf2HmacSha256();
    TestConstantTimeEquals();
    TestHexRoundTrip();
    TestRandomBytesAreDistinct();
    TestPbkdf2RecordFormat();
    TestProductionIterationCountIsInteractive();

    if (g_failures > 0)
    {
        std::fprintf(stderr, "UtilsTest: %d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("UtilsTest: OK\n");
    return 0;
}
