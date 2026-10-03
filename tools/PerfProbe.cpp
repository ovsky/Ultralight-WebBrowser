// Offscreen paint-cost probe for the browser's internal pages.
//
// The browser's own pages are rendered by the same engine as web content, so a
// page that feels laggy to scroll can be measured without a window: create a
// View, load the page, and time the Renderer::Update + Renderer::Render pair
// for each simulated wheel tick. The probe also reports how much of the
// viewport the engine actually repaints per frame, via the Surface dirty
// bounds. Repaint area is the diagnostic that separates "the handler is slow"
// from "the engine is redrawing the whole page for a small scroll", and it is
// the number that decides whether compositing is doing its job.
//
// Usage (from the build directory, where assets/ and the SDK resources live):
//   PerfProbe [--page settings.html] [--width 1920] [--height 1080]
//             [--scale 1.0] [--accelerated] [--compositor]
//             [--ticks 60] [--warmup 10]
//
// Without --compositor the View is created with compositing off, which is the
// default every ViewConfig in src/ currently uses. Passing it flips the single
// flag so the two paths can be compared on identical content.

#include <AppCore/AppCore.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace ultralight;

namespace {

struct Options {
  std::string page = "settings.html";
  uint32_t width = 1920;
  uint32_t height = 1080;
  double scale = 1.0;
  bool accelerated = false;
  bool compositor = false;
  int ticks = 60;
  int warmup = 10;
  bool trace = false;
  bool js_scroll = false;
};

struct Stats {
  double mean = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double max = 0.0;
  uint64_t total_painted_px = 0;
  uint64_t max_painted_px = 0;
  int samples = 0;
};

double percentile(std::vector<double> v, double frac) {
  if (v.empty())
    return 0.0;
  size_t idx = static_cast<size_t>(frac * static_cast<double>(v.size() - 1) + 0.5);
  if (idx >= v.size())
    idx = v.size() - 1;
  return v[idx];
}

Stats summarize(std::vector<double> samples, std::vector<uint64_t> painted) {
  Stats s;
  s.samples = static_cast<int>(samples.size());
  if (samples.empty())
    return s;

  std::sort(samples.begin(), samples.end());
  double sum = 0.0;
  for (double d : samples)
    sum += d;

  s.mean = sum / static_cast<double>(samples.size());
  s.p50 = percentile(samples, 0.50);
  s.p95 = percentile(samples, 0.95);
  s.max = samples.back();

  for (uint64_t p : painted) {
    s.total_painted_px += p;
    if (p > s.max_painted_px)
      s.max_painted_px = p;
  }
  return s;
}

// Renderer::Render() only *schedules* work: rasterization happens on the
// engine's own threads and the pixels land later. Timing the call therefore
// measures the enqueue, not the paint, and reports a comfortable 0.00 ms for a
// page that is visibly stuttering. Locking the surface blocks until the
// renderer has released it, which is the only way to put the real paint cost on
// the clock, so every measurement here ends with a LockPixels/UnlockPixels pair.
double sync_and_paint(Renderer *renderer, View *view, uint64_t *painted_px) {
  auto t0 = std::chrono::steady_clock::now();
  renderer->Render();

  uint64_t px = 0;
  if (Surface *s = view->surface()) {
    IntRect d = s->dirty_bounds();
    if (!d.IsEmpty()) {
      int w = d.right - d.left;
      int h = d.bottom - d.top;
      if (w > 0 && h > 0)
        px = (uint64_t)w * (uint64_t)h;
    }
    // Blocks until the renderer is done writing these pixels.
    s->LockPixels();
    s->UnlockPixels();
    s->ClearDirtyBounds();
  }

  auto t1 = std::chrono::steady_clock::now();
  if (painted_px)
    *painted_px = px;
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void report(const char *label, const Stats &s, uint64_t viewport_px) {
  std::printf("\n  %s\n", label);
  std::printf("    frames        : %d\n", s.samples);
  std::printf("    mean          : %.2f ms\n", s.mean);
  std::printf("    p50 / p95     : %.2f ms / %.2f ms\n", s.p50, s.p95);
  std::printf("    max           : %.2f ms\n", s.max);
  if (viewport_px > 0) {
    std::printf("    painted/frame : %llu px (%.1f%% of viewport)\n",
                (unsigned long long)(s.total_painted_px / (uint64_t)(s.samples ? s.samples : 1)),
                100.0 * static_cast<double>(s.total_painted_px) /
                    static_cast<double>(viewport_px * (uint64_t)(s.samples ? s.samples : 1)));
  }
}

// Pumps the renderer until the view stops loading, so measurements are not
// polluted by first-paint, layout, and script execution.
bool pump_until_loaded(Renderer *renderer, View *view, int max_frames, int frame_ms) {
  for (int i = 0; i < max_frames; i++) {
    renderer->Update();
    renderer->Render();
    if (!view->is_loading())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms));
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--page")
      o.page = next("--page");
    else if (a == "--width")
      o.width = (uint32_t)std::strtoul(next("--width").c_str(), nullptr, 10);
    else if (a == "--height")
      o.height = (uint32_t)std::strtoul(next("--height").c_str(), nullptr, 10);
    else if (a == "--scale")
      o.scale = std::strtod(next("--scale").c_str(), nullptr);
    else if (a == "--accelerated")
      o.accelerated = true;
    else if (a == "--compositor")
      o.compositor = true;
    else if (a == "--ticks")
      o.ticks = (int)std::strtol(next("--ticks").c_str(), nullptr, 10);
    else if (a == "--warmup")
      o.warmup = (int)std::strtol(next("--warmup").c_str(), nullptr, 10);
    else if (a == "--trace")
      o.trace = true;
    else if (a == "--js-scroll")
      o.js_scroll = true;
    else {
      std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
      return 2;
    }
  }

  // App::Create installs the default FileSystem, FontLoader, Logger and
  // SurfaceFactory, which Renderer::Create would otherwise require us to
  // reimplement. We never call App::Run(); the probe drives the renderer
  // directly so every frame boundary is explicit and measurable.
  Settings settings;
  settings.file_system_path = "assets/";

  Config config;
  config.scroll_timer_delay = 1.0 / 60.0;

  RefPtr<App> app = App::Create(settings, config);
  if (!app) {
    std::fprintf(stderr, "App::Create failed (is the working directory the build dir?)\n");
    return 1;
  }

  RefPtr<Renderer> renderer = app->renderer();
  if (!renderer) {
    std::fprintf(stderr, "no renderer\n");
    return 1;
  }

  ViewConfig cfg;
  cfg.initial_device_scale = o.scale;
  cfg.is_accelerated = o.accelerated;
  cfg.enable_compositor = o.compositor;
  cfg.display_id = 0;
  cfg.enable_javascript = true;

  RefPtr<View> view = renderer->CreateView(o.width, o.height, cfg, nullptr);
  if (!view) {
    std::fprintf(stderr, "CreateView failed\n");
    return 1;
  }

  const std::string url = "file:///" + o.page;
  std::printf("PerfProbe: %s  %ux%u @ %.2fx  accelerated=%d compositor=%d scroll=%s\n",
              url.c_str(), o.width, o.height, o.scale,
              (int)o.accelerated, (int)o.compositor, o.js_scroll ? "js" : "event");
  view->LoadURL(url.c_str());

  if (!pump_until_loaded(renderer.get(), view.get(), 600, 8)) {
    std::fprintf(stderr, "page did not finish loading in 600 frames\n");
    return 1;
  }

  // Let fonts settle and the JS-built list finish its first layout.
  for (int i = 0; i < o.warmup; i++) {
    renderer->Update();
    renderer->Render();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  // Scroll delivery requires input focus, the same way the browser's Tab::Show
  // focuses the overlay before the first wheel tick. Without it FireScrollEvent
  // is silently dropped and every scroll measurement below reads as zero work.
  view->Focus();

  // Read the document height through the engine so we know the page is
  // actually scrollable; a page that cannot scroll makes every number below
  // meaningless.
  String height_js = view->EvaluateScript(
      "(function(){return String(document.documentElement.scrollHeight);})();", nullptr);
  const long doc_height = std::strtol(height_js.utf8().data() ? height_js.utf8().data() : "0", nullptr, 10);
  const uint64_t viewport_px = (uint64_t)o.width * (uint64_t)o.height;
  std::printf("  document scrollHeight: %ld px (viewport %u px)\n", doc_height, o.height);

  if (doc_height <= (long)o.height) {
    std::fprintf(stderr,
                 "  page is not scrollable at this size; scroll numbers would be vacuous\n");
  }

  // Idle cost: how expensive is a frame when nothing is happening. This is the
  // floor that any interaction has to clear.
  std::vector<double> idle;
  std::vector<uint64_t> idle_px;
  for (int i = 0; i < 30; i++) {
    renderer->Update();
    uint64_t px = 0;
    idle.push_back(sync_and_paint(renderer.get(), view.get(), &px));
    idle_px.push_back(px);
  }
  report("idle frames (no input)", summarize(idle, idle_px), viewport_px);

  // Scroll cost: one simulated wheel tick per frame, which is what a real
  // wheel gesture produces.
  //
  // Direction alternates every tick on purpose. settings.html is only a few
  // hundred pixels taller than the viewport at this size, so a one-directional
  // run reaches the bottom within a few frames and every later frame reports
  // zero work -- which reads as "scrolling is free" when the truth is "there
  // was nothing left to scroll". Alternating keeps the document inside its
  // scrollable range for the whole sample.
  std::vector<double> scroll;
  std::vector<uint64_t> painted;
  for (int i = 0; i < o.ticks; i++) {
    // Two ways to move the document, because they exercise different engine
    // paths and only one of them works in every configuration:
    //
    //  * FireScrollEvent is the real input path the browser uses, but it is
    //    routed through the same machinery as compositor-driven scroll
    //    animation, and it does not take effect for a View that is not
    //    attached to a window.
    //  * window.scrollTo produces the identical repaint work regardless of
    //    how the scroll was requested, which is what the paint measurement
    //    below actually needs.
    if (o.js_scroll) {
      const int y = ((i / 2) % 2 == 0) ? (i * 20) : ((o.ticks - i) * 20);
      char js[96];
      std::snprintf(js, sizeof(js), "window.scrollTo(0,%d);", y);
      view->EvaluateScript(js, nullptr);
    } else {
      ScrollEvent evt;
      evt.type = ScrollEvent::kType_ScrollByPixel;
      evt.delta_x = 0;
      evt.delta_y = (i % 2 == 0) ? 120 : -120;
      view->FireScrollEvent(evt);
    }

    // RefreshDisplay is what drives scroll animation and requestAnimationFrame
    // (Renderer.h: "updates animations, smooth scroll, and
    // window.requestAnimationFrame()"). Without it a scroll queued through
    // FireScrollEvent is never advanced.
    renderer->RefreshDisplay(0);
    renderer->Update();
    uint64_t px = 0;
    scroll.push_back(sync_and_paint(renderer.get(), view.get(), &px));
    painted.push_back(px);

    if (o.trace && (i % 10 == 0 || i == o.ticks - 1)) {
      String y = view->EvaluateScript(
          "(function(){return String(window.scrollY||window.pageYOffset||0);})();", nullptr);
      std::printf("    tick %3d: scrollY=%-6s painted=%llu px  %.2f ms\n", i,
                  y.utf8().data() ? y.utf8().data() : "?", (unsigned long long)px,
                  scroll.back());
    }
  }
  Stats ss = summarize(scroll, painted);
  report("scroll frames (wheel tick each frame)", ss, viewport_px);

  // Report the scroll offset actually reached, so a run that silently stopped
  // moving the document cannot be mistaken for a fast one.
  String offset_js = view->EvaluateScript(
      "(function(){return String(window.scrollY||window.pageYOffset||0);})();", nullptr);
  std::printf("  final scrollY: %s\n",
              offset_js.utf8().data() ? offset_js.utf8().data() : "?");

  // Synthetic hover: the mouse-move storm a cursor crossing the page produces.
  // Hover is what makes hover styles repaint, so it separates "scrolling is
  // slow" from "any pointer movement is slow".
  std::vector<double> hover;
  std::vector<uint64_t> hover_px;
  for (int i = 0; i < o.ticks; i++) {
    MouseEvent me;
    me.type = MouseEvent::kType_MouseMoved;
    me.x = 40 + (i % 60) * 12;
    me.y = 120 + (i % 40) * 10;
    me.button = MouseEvent::kButton_None;
    view->FireMouseEvent(me);

    renderer->Update();
    uint64_t px = 0;
    hover.push_back(sync_and_paint(renderer.get(), view.get(), &px));
    hover_px.push_back(px);
  }
  report("hover frames (synthetic mouse move each frame)", summarize(hover, hover_px), viewport_px);

  std::printf("\n");
  renderer->Update();
  renderer->Render();
  app->Quit();
  return 0;
}
