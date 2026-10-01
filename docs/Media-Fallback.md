# Media Fallback

The bundled rendering engine ships **no media pipeline and no codecs**. There is
no setting that turns this on, and no build flag: `<video>` and `<audio>`
elements render as empty boxes. HLS, DASH, MSE and DRM playback simply do not
exist in the engine.

That is a hard engine limitation, not a bug in this project. Rather than leave
users on a black rectangle, this browser can hand known video sites to the
platform's own webview — the same machinery already used for DRM, which is
WebView2 on Windows, WKWebView on macOS, and WebKit2GTK on Linux.

**Off by default.** Silently sending a page to a different engine is a
behaviour change the user should opt into.

## Enabling

Settings → DRM → **Media Fallback**, or `media_fallback_enabled` in
`settings.json`.

When it is on, navigating to a listed host opens it in the system webview
instead of the engine. DRM sites are checked first and keep their existing DRM
routing and prompt.

## The host list

`assets/media_sites.json`, one host per line:

```
youtube.com
vimeo.com
twitch.tv
...
```

- `#` starts a comment
- A pasted full URL is accepted; the scheme and path are stripped on load
- A leading `*.` is accepted and stripped, so store bare hosts
- Matching is case-insensitive

### Matching rules

Suffix matching happens **on a label boundary**. `youtube.com` therefore matches
`www.youtube.com`, `m.youtube.com` and `music.youtube.com`.

It does **not** match `notyoutube.com`, `myyoutube.com`,
`youtube.com.evil.test` or `evilyoutube.com`. A plain substring test would catch
all of those, which is the classic way a host allowlist turns into an open
redirect — so the boundary check is explicit and covered by tests.

A missing or empty list redirects nothing, which is the safe failure direction.

## What it does not do

- **It is not transcoding or a player.** The page runs in the OS webview, so it
  gets the platform's own codecs — which is the whole point.
- **It is not DRM.** DRM sites are routed by their own path, with their own
  prompt and their own Widevine-dependent runtime check.
- **It is not available everywhere.** Android has no embeddable system webview,
  so `CreatePlatformWebViewTab` returns null there and the tab stays an engine
  tab. Media therefore does not work on Android.

## Implementation

| Piece | Location |
|-------|----------|
| Host list and matching | `src/MediaFallback.{h,cpp}` |
| Hosted routing | `UI::MaybeOpenMediaTab()` |
| Shared webview creation | `UI::OpenSystemWebViewTab()` |

`MaybeOpenDrmTab` and `MaybeOpenMediaTab` differ only in how they decide to
hand off. Everything after that — checking the native runtime exists, creating
the tab, sizing it, driving navigation and keeping the URL bar and badge in sync
— is the single `OpenSystemWebViewTab` helper, so the two paths cannot drift.

## Testing

`tests/MediaFallbackTest.cpp` covers exact hosts, subdomain matching on a label
boundary, the lookalike hosts that must **not** match, scheme/port/credentials/case
handling, comment and wildcard handling, and the empty and missing list cases.

```bash
ctest --test-dir build -R MediaFallbackTest --output-on-failure
```

It needs no Ultralight libraries, so it builds and runs anywhere the toolchain
does. It is skipped for Android cross-builds.

## Adding a host

Append one host per line to `assets/media_sites.json`. No code change and no
catalog change is needed; the list is read once at startup.

Prefer a registrable domain over a specific subdomain, and check that the site is
not already covered by `assets/drm_sites.json` — if it is, DRM owns the routing
and the media entry will be skipped.
