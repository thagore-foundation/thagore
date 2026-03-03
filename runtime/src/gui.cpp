#include "thag/gui.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct GuiCanvas {
  int width = 0;
  int height = 0;
  std::string title;
  std::vector<uint32_t> pixels;
  int frame_index = 0;
  int target_fps = 60;
  bool should_close = false;
  std::string last_frame_path;
  std::chrono::steady_clock::time_point next_tick{};
};

static GuiCanvas* as_canvas(void* canvas) {
  return static_cast<GuiCanvas*>(canvas);
}

static const GuiCanvas* as_canvas(const void* canvas) {
  return static_cast<const GuiCanvas*>(canvas);
}

static char* dup_cstr(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) {
    return nullptr;
  }
  if (!text.empty()) {
    std::memcpy(out, text.data(), text.size());
  }
  out[text.size()] = '\0';
  return out;
}

static bool in_bounds(const GuiCanvas& canvas, int x, int y) {
  return x >= 0 && y >= 0 && x < canvas.width && y < canvas.height;
}

static uint32_t normalize_rgb(int rgba) {
  const uint32_t v = static_cast<uint32_t>(rgba);
  return ((v >> 16) & 0xFFu) << 16 | ((v >> 8) & 0xFFu) << 8 | (v & 0xFFu);
}

static void set_pixel(GuiCanvas& canvas, int x, int y, uint32_t rgb) {
  if (!in_bounds(canvas, x, y)) {
    return;
  }
  const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width) +
                          static_cast<std::size_t>(x);
  canvas.pixels[idx] = rgb;
}

static std::string sanitize_name(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char ch : text) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
      out.push_back(static_cast<char>(ch));
      continue;
    }
    out.push_back('_');
  }
  if (out.empty()) {
    out = "thagore_canvas";
  }
  return out;
}

}  // namespace

extern "C" {

void* thag_gui_create_canvas(int width, int height, const char* title) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto* canvas = new (std::nothrow) GuiCanvas();
  if (canvas == nullptr) {
    return nullptr;
  }
  canvas->width = width;
  canvas->height = height;
  canvas->title = title == nullptr ? "thagore_canvas" : std::string(title);
  canvas->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0xFFFFFFu);
  canvas->target_fps = 60;
  canvas->frame_index = 0;
  canvas->should_close = false;
  canvas->last_frame_path.clear();
  canvas->next_tick = std::chrono::steady_clock::time_point{};
  return canvas;
}

int thag_gui_destroy_canvas(void* canvas) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  delete c;
  return 1;
}

int thag_gui_clear(void* canvas, int rgba) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  const uint32_t rgb = normalize_rgb(rgba);
  std::fill(c->pixels.begin(), c->pixels.end(), rgb);
  return 1;
}

int thag_gui_draw_point(void* canvas, int x, int y, int rgba) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  set_pixel(*c, x, y, normalize_rgb(rgba));
  return 1;
}

int thag_gui_draw_line(void* canvas, int x0, int y0, int x1, int y1, int rgba) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  const uint32_t rgb = normalize_rgb(rgba);
  int dx = std::abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    set_pixel(*c, x0, y0, rgb);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  return 1;
}

int thag_gui_present(void* canvas) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  const std::string base = sanitize_name(c->title);
  const std::string file_name = base + "_frame_" + std::to_string(c->frame_index++) + ".ppm";
  std::filesystem::path output = std::filesystem::current_path() / file_name;
  std::ofstream out(output, std::ios::binary);
  if (!out) {
    return 0;
  }
  out << "P3\n" << c->width << " " << c->height << "\n255\n";
  for (int y = 0; y < c->height; ++y) {
    for (int x = 0; x < c->width; ++x) {
      const std::size_t idx =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(c->width) + static_cast<std::size_t>(x);
      const uint32_t rgb = c->pixels[idx];
      const int r = static_cast<int>((rgb >> 16) & 0xFFu);
      const int g = static_cast<int>((rgb >> 8) & 0xFFu);
      const int b = static_cast<int>(rgb & 0xFFu);
      out << r << " " << g << " " << b << "\n";
    }
  }
  if (!out.good()) {
    return 0;
  }
  c->last_frame_path = output.string();
  return 1;
}

const char* thag_gui_last_frame_path(void* canvas) {
  const GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return nullptr;
  }
  return dup_cstr(c->last_frame_path);
}

int thag_gui_poll_event(void* canvas) {
  return as_canvas(canvas) == nullptr ? 0 : 0;
}

int thag_gui_should_close(void* canvas) {
  const GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 1;
  }
  return c->should_close ? 1 : 0;
}

int thag_gui_request_close(void* canvas) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr) {
    return 0;
  }
  c->should_close = true;
  return 1;
}

int thag_gui_set_target_fps(void* canvas, int fps) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr || fps <= 0) {
    return 0;
  }
  c->target_fps = fps;
  c->next_tick = std::chrono::steady_clock::time_point{};
  return 1;
}

int thag_gui_tick(void* canvas) {
  GuiCanvas* c = as_canvas(canvas);
  if (c == nullptr || c->target_fps <= 0) {
    return 0;
  }
  const auto frame_duration = std::chrono::milliseconds(1000 / c->target_fps);
  const auto now = std::chrono::steady_clock::now();
  if (c->next_tick.time_since_epoch().count() == 0) {
    c->next_tick = now + frame_duration;
    return 1;
  }
  if (now < c->next_tick) {
    std::this_thread::sleep_until(c->next_tick);
  }
  c->next_tick += frame_duration;
  const auto after = std::chrono::steady_clock::now();
  if (c->next_tick < after) {
    c->next_tick = after + frame_duration;
  }
  return 1;
}

}  // extern "C"
