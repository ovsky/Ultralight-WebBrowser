#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// Ensure C++ types are available after Obj-C imports.
#include "drm/DRMWebViewTab.h"
#include <string>
#include <memory>

// Forward-declare the C++ DRMWebViewTabMac for use in Obj-C
// and define Objective-C types at true global scope.
namespace drm { class DRMWebViewTabMac; }

@interface DRMWebViewObserver : NSObject <WKNavigationDelegate>
@property(nonatomic, assign) drm::DRMWebViewTabMac *owner;
@end

namespace drm
{

// Helper functions for converting between Objective-C and C++ types
static std::string ToStdString(NSString *str)
{
  if (!str)
    return {};
  const char *cstr = [str UTF8String];
  return cstr ? std::string(cstr) : std::string();
}

static NSURLRequest *RequestForURL(const std::string &url)
{
  NSString *nsUrl = [NSString stringWithUTF8String:url.c_str()];
  NSURL *nsURL = [NSURL URLWithString:nsUrl];
  if (!nsURL)
    return nil;
  return [NSURLRequest requestWithURL:nsURL];
}

class DRMWebViewTabMac : public DRMWebViewTab
{
public:
  DRMWebViewTabMac(uint64_t id, const DRMWebViewConfig &config, const DRMWebViewCallbacks &callbacks)
      : DRMWebViewTab(id, config, callbacks)
  {
    ns_window_ = (__bridge NSWindow *)config.parent_window;
    CreateView();
  }

  ~DRMWebViewTabMac() override
  {
    Close();
  }

  void LoadURL(const std::string &url) override
  {
    current_url_ = url;
    if (!web_view_)
      return;
    NSURLRequest *req = RequestForURL(url);
    if (req)
      [web_view_ loadRequest:req];
  }

  void GoBack() override
  {
    if (web_view_ && [web_view_ canGoBack])
      [web_view_ goBack];
  }

  void GoForward() override
  {
    if (web_view_ && [web_view_ canGoForward])
      [web_view_ goForward];
  }

  void Reload() override
  {
    if (web_view_)
      [web_view_ reload];
  }

  void Stop() override
  {
    if (web_view_)
      [web_view_ stopLoading];
  }

  void Focus() override
  {
    if (web_view_)
      [[web_view_ window] makeFirstResponder:web_view_];
  }

  void Blur() override
  {
    // Remove focus from WebView by making window first responder
    if (ns_window_)
      [ns_window_ makeFirstResponder:nil];
    // Also resign first responder from web view
    if (web_view_)
      [[web_view_ window] makeFirstResponder:nil];
  }

  void Resize(uint32_t width, uint32_t height, uint32_t offset_x, uint32_t offset_y) override
  {
    // Store config for Show() to use
    config_.width = width;
    config_.height = height;
    config_.offset_x = offset_x;
    config_.offset_y = offset_y;
    
    if (!host_view_)
      return;
    // Only update if visible
    if (![host_view_ isHidden])
    {
      NSRect frame = NSMakeRect(offset_x, offset_y, width, height);
      [host_view_ setFrame:frame];
      [web_view_ setFrame:[host_view_ bounds]];
    }
  }

  void Show() override
  {
    if (host_view_)
    {
      // Restore proper position before showing
      NSRect frame = NSMakeRect(config_.offset_x, config_.offset_y, config_.width, config_.height);
      [host_view_ setFrame:frame];
      [web_view_ setFrame:[host_view_ bounds]];
      [host_view_ setHidden:NO];
    }
  }

  void Hide() override
  {
    // First blur to release focus
    Blur();
    if (host_view_)
    {
      [host_view_ setHidden:YES];
      // Move off-screen to prevent any input capture
      NSRect offscreen = NSMakeRect(-10000, -10000, 100, 100);
      [host_view_ setFrame:offscreen];
    }
  }

  void Close() override
  {
    if (observing_title_ && web_view_)
    {
      [web_view_ removeObserver:observer_ forKeyPath:@"title" context:nil];
      [web_view_ removeObserver:observer_ forKeyPath:@"URL" context:nil];
      observing_title_ = false;
    }
    if (web_view_)
      [web_view_ setNavigationDelegate:nil];
    if (host_view_)
    {
      [host_view_ removeFromSuperview];
    }
    host_view_ = nil;
    web_view_ = nil;
    observer_ = nil;
  }

  void DetachFromParent() override
  {
    // On macOS/WKWebView, hiding and moving off-screen is sufficient
    // WKWebView doesn't intercept keyboard like WebView2 does on Windows
    Hide();
  }

  void ReattachToParent() override
  {
    // Restore visibility if it was visible before
    // This is a no-op on macOS since Hide/Show handle everything
  }

  std::string GetTitle() const override { return current_title_; }
  std::string GetURL() const override { return current_url_; }
  bool CanGoBack() const override { return web_view_ ? [web_view_ canGoBack] : false; }
  bool CanGoForward() const override { return web_view_ ? [web_view_ canGoForward] : false; }

  void HandleTitleChange(NSString *title)
  {
    current_title_ = ToStdString(title);
    if (callbacks_.on_title_changed)
      callbacks_.on_title_changed(id_, current_title_);
  }

  void HandleURLChange(NSURL *url)
  {
    current_url_ = url ? ToStdString(url.absoluteString) : std::string();
    if (callbacks_.on_url_changed)
      callbacks_.on_url_changed(id_, current_url_);
    if (callbacks_.on_navigation_state)
      callbacks_.on_navigation_state(id_, CanGoBack(), CanGoForward());
  }

  void HandleLoadingState(bool loading)
  {
    if (callbacks_.on_loading_state)
      callbacks_.on_loading_state(id_, loading);
  }

private:
  void CreateView()
  {
    if (!ns_window_)
      return;
    NSView *content = [ns_window_ contentView];
    if (!content)
      return;
    host_view_ = [[NSView alloc] initWithFrame:content.bounds];
    [host_view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content addSubview:host_view_ positioned:NSWindowAbove relativeTo:nil];

    WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
    web_view_ = [[WKWebView alloc] initWithFrame:[host_view_ bounds] configuration:cfg];
    [web_view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [host_view_ addSubview:web_view_];

    observer_ = [[DRMWebViewObserver alloc] init];
    observer_.owner = this;
    [web_view_ setNavigationDelegate:observer_];
    [web_view_ addObserver:observer_ forKeyPath:@"title" options:NSKeyValueObservingOptionNew context:nil];
    [web_view_ addObserver:observer_ forKeyPath:@"URL" options:NSKeyValueObservingOptionNew context:nil];
    observing_title_ = true;
  }

  NSWindow *ns_window_ = nil;
  NSView *host_view_ = nil;
  WKWebView *web_view_ = nil;
  DRMWebViewObserver *observer_ = nil;
  bool observing_title_ = false;
  std::string current_title_ = "DRM WebView";
  std::string current_url_;
};

std::unique_ptr<DRMWebViewTab> CreatePlatformWebViewTab(uint64_t id,
                                                        const DRMWebViewConfig &config,
                                                        DRMWebViewCallbacks callbacks)
{
  return std::make_unique<DRMWebViewTabMac>(id, config, callbacks);
}

void PrewarmWebViewEnvironment()
{
  // WKWebView on macOS doesn't need pre-warming - it's fast to initialize
  // The WebKit process pool is managed by the system
}

} // namespace drm

// Objective-C implementation must be at global scope
@implementation DRMWebViewObserver

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation
{
  if (self.owner)
    self.owner->HandleLoadingState(true);
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
  if (self.owner)
  {
    self.owner->HandleLoadingState(false);
    self.owner->HandleURLChange(webView.URL);
  }
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error
{
  if (self.owner)
    self.owner->HandleLoadingState(false);
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error
{
  if (self.owner)
    self.owner->HandleLoadingState(false);
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id> *)change context:(void *)context
{
  if (!self.owner)
    return;
  id newValue = change[NSKeyValueChangeNewKey];
  if (!newValue || newValue == [NSNull null])
    return;
  if ([keyPath isEqualToString:@"title"])
  {
    if ([newValue isKindOfClass:[NSString class]])
      self.owner->HandleTitleChange((NSString *)newValue);
  }
  else if ([keyPath isEqualToString:@"URL"])
  {
    if ([newValue isKindOfClass:[NSURL class]])
      self.owner->HandleURLChange((NSURL *)newValue);
  }
}

@end

#endif
