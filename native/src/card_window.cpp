#include "card_window.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "image.h"

namespace {

constexpr int kMaxRows = 10;

float sdRoundRect(float px, float py, float left, float top, float right, float bottom, float radius) {
  float cx = (left + right) * 0.5f;
  float cy = (top + bottom) * 0.5f;
  float hx = (right - left) * 0.5f;
  float hy = (bottom - top) * 0.5f;
  float qx = std::fabs(px - cx) - (hx - radius);
  float qy = std::fabs(py - cy) - (hy - radius);
  float ax = std::max(qx, 0.0f);
  float ay = std::max(qy, 0.0f);
  return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - radius;
}

void formatTime(int64_t ms, wchar_t* out, size_t count) {
  if (ms <= 0) {
    wcsncpy(out, L"", count);
    return;
  }
  std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
  localtime_s(&local, &seconds);
  std::tm todayTm{};
  std::time_t now = std::time(nullptr);
  localtime_s(&todayTm, &now);
  if (local.tm_year == todayTm.tm_year && local.tm_yday == todayTm.tm_yday) {
    swprintf(out, count, L"%02d:%02d", local.tm_hour, local.tm_min);
  } else {
    swprintf(out, count, L"%02d-%02d %02d:%02d", local.tm_mon + 1, local.tm_mday, local.tm_hour,
             local.tm_min);
  }
}

HFONT makeFont(int pixelHeight, int weight) {
  return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

}  // namespace

bool CardWindow::create(HINSTANCE instance, HWND owner) {
  owner_ = owner;
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"ArknightsPetCard";
  RegisterClassExW(&windowClass);

  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                          windowClass.lpszClassName, L"Codex Messages", WS_POPUP, 0, 0, 10, 10,
                          owner, nullptr, instance, this);
  return hwnd_ != nullptr;
}

void CardWindow::destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (bitmap_) {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bits_ = nullptr;
    previous_ = nullptr;
  }
  if (dc_) {
    DeleteDC(dc_);
    dc_ = nullptr;
  }
}

LRESULT CALLBACK CardWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    auto* self = reinterpret_cast<CardWindow*>(create->lpCreateParams);
    if (self) self->hwnd_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self = reinterpret_cast<CardWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (self) return self->handle(message, wParam, lParam);
  return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CardWindow::handle(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_NCHITTEST:
      return HTCLIENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONUP: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      int row = rowAt(y);
      if (row >= 0 && row < static_cast<int>(messages_.size())) {
        if (owner_) SendMessageW(owner_, kRowClick, static_cast<WPARAM>(row), 0);
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

int CardWindow::rowAt(int clientY) const {
  int top = padding_ + headerHeight_;
  if (clientY < top) return -1;
  int index = (clientY - top) / std::max(1, rowHeight_);
  if (index < 0 || index >= static_cast<int>(messages_.size())) return -1;
  return index;
}

bool CardWindow::ensureBitmap(int width, int height) {
  if (width == width_ && height == height_ && bitmap_) return true;
  if (bitmap_) {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bits_ = nullptr;
    previous_ = nullptr;
  }
  if (!dc_) dc_ = CreateCompatibleDC(nullptr);
  if (!dc_) return false;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
  if (!bitmap_ || !bits_) return false;
  previous_ = SelectObject(dc_, bitmap_);
  width_ = width;
  height_ = height;
  return true;
}

void CardWindow::setMessages(std::vector<SessionMessage> messages, float dpiScale) {
  messages_ = std::move(messages);
  if (messages_.size() > kMaxRows) messages_.resize(kMaxRows);
  dpi_ = dpiScale;
  padding_ = static_cast<int>(std::lround(12 * dpi_));
  headerHeight_ = static_cast<int>(std::lround(26 * dpi_));
  rowHeight_ = static_cast<int>(std::lround(46 * dpi_));
  int width = static_cast<int>(std::lround(320 * dpi_));
  int height = padding_ * 2 + headerHeight_ + static_cast<int>(messages_.size()) * rowHeight_;
  if (!ensureBitmap(width, height)) return;
  mask_.assign(static_cast<size_t>(width) * height, 0);
  paint();
  present();
}

void CardWindow::paint() {
  if (!bits_ || !dc_) return;
  auto* pixels = static_cast<uint32_t*>(bits_);
  const int w = width_;
  const int h = height_;
  const float radius = 12.0f * dpi_;
  const float inset = 6.0f * dpi_;
  const float left = inset;
  const float top = inset * 0.5f;
  const float right = w - inset;
  const float bottom = h - inset * 0.5f;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float px = x + 0.5f;
      float py = y + 0.5f;
      float d = sdRoundRect(px, py, left, top, right, bottom, radius);
      float panel = std::clamp(0.5f - d, 0.0f, 1.0f);
      float shadowD = sdRoundRect(px, py - 4 * dpi_, left - 4 * dpi_, top, right + 4 * dpi_,
                                  bottom + 6 * dpi_, radius + 4 * dpi_);
      float shadow = std::clamp((2.0f * dpi_ - shadowD) / (10.0f * dpi_), 0.0f, 1.0f) * 0.45f;
      float alpha = panel * 0.95f + shadow * (1.0f - panel);
      float r = 17.0f;
      float g = 19.0f;
      float b = 24.0f;
      if (panel > 0.0f && d > -1.2f * dpi_) {
        r += (255.0f - r) * 0.09f;
        g += (255.0f - g) * 0.09f;
        b += (255.0f - b) * 0.09f;
      }
      auto* out = reinterpret_cast<unsigned char*>(&pixels[y * w + x]);
      out[0] = static_cast<unsigned char>(std::clamp(b, 0.0f, 255.0f));
      out[1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 255.0f));
      out[2] = static_cast<unsigned char>(std::clamp(r, 0.0f, 255.0f));
      unsigned char alphaByte = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
      out[3] = alphaByte;
      mask_[static_cast<size_t>(y) * w + x] = alphaByte;
    }
  }

  int dotRadius = static_cast<int>(std::lround(4 * dpi_));
  int dotX = padding_ + static_cast<int>(std::lround(6 * dpi_));
  for (size_t i = 0; i < messages_.size(); ++i) {
    const SessionMessage& message = messages_[i];
    int rowTop = padding_ + headerHeight_ + static_cast<int>(i) * rowHeight_;
    int cy = rowTop + rowHeight_ / 2;
    unsigned char cr = 139, cg = 147, cb = 165;
    if (message.status == SessionStatus::Running) {
      cr = 89;
      cg = 167;
      cb = 255;
    } else if (message.status == SessionStatus::Done) {
      cr = 110;
      cg = 231;
      cb = 168;
    }
    for (int y = cy - dotRadius - 1; y <= cy + dotRadius + 1; ++y) {
      if (y < 0 || y >= h) continue;
      for (int x = dotX - dotRadius - 1; x <= dotX + dotRadius + 1; ++x) {
        if (x < 0 || x >= w) continue;
        float dx = static_cast<float>(x) + 0.5f - dotX;
        float dy = static_cast<float>(y) + 0.5f - cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        float coverage = std::clamp(dotRadius + 0.5f - dist, 0.0f, 1.0f);
        if (coverage <= 0.0f) continue;
        auto* out = reinterpret_cast<unsigned char*>(&pixels[y * w + x]);
        out[0] = static_cast<unsigned char>(out[0] * (1 - coverage) + cb * coverage);
        out[1] = static_cast<unsigned char>(out[1] * (1 - coverage) + cg * coverage);
        out[2] = static_cast<unsigned char>(out[2] * (1 - coverage) + cr * coverage);
      }
    }
    if (i + 1 < messages_.size()) {
      int lineY = rowTop + rowHeight_;
      for (int x = padding_; x < w - padding_; ++x) {
        if (lineY < 0 || lineY >= h) continue;
        auto* out = reinterpret_cast<unsigned char*>(&pixels[lineY * w + x]);
        out[0] = static_cast<unsigned char>(out[0] * 0.94f + 255 * 0.06f);
        out[1] = static_cast<unsigned char>(out[1] * 0.94f + 255 * 0.06f);
        out[2] = static_cast<unsigned char>(out[2] * 0.94f + 255 * 0.06f);
      }
    }
  }

  SetBkMode(dc_, TRANSPARENT);
  int titleHeight = static_cast<int>(std::lround(11 * dpi_));
  int titleWeight = FW_SEMIBOLD;
  HFONT font = makeFont(titleHeight, titleWeight);
  HGDIOBJ oldFont = SelectObject(dc_, font);
  SetTextColor(dc_, RGB(139, 147, 165));
  RECT headerRect{padding_ + static_cast<int>(std::lround(4 * dpi_)),
                  padding_ + static_cast<int>(std::lround(2 * dpi_)), w - padding_, padding_ + headerHeight_};
  DrawTextW(dc_, L"Codex 会话", -1, &headerRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  int nameHeight = static_cast<int>(std::lround(12 * dpi_));
  int subHeight = static_cast<int>(std::lround(10 * dpi_));
  HFONT nameFont = makeFont(nameHeight, FW_SEMIBOLD);
  HFONT subFont = makeFont(subHeight, FW_NORMAL);
  int textLeft = dotX + dotRadius + static_cast<int>(std::lround(10 * dpi_));
  for (size_t i = 0; i < messages_.size(); ++i) {
    const SessionMessage& message = messages_[i];
    int rowTop = padding_ + headerHeight_ + static_cast<int>(i) * rowHeight_;
    SelectObject(dc_, nameFont);
    SetTextColor(dc_, RGB(233, 237, 245));
    RECT nameRect{textLeft, rowTop + static_cast<int>(std::lround(6 * dpi_)), w - padding_,
                  rowTop + static_cast<int>(std::lround(6 * dpi_ + 18 * dpi_))};
    std::wstring title = widen(message.title);
    DrawTextW(dc_, title.c_str(), -1, &nameRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc_, subFont);
    SetTextColor(dc_, RGB(139, 147, 165));
    wchar_t time[32]{};
    formatTime(message.status == SessionStatus::Done ? message.completedAt : message.startedAt, time, 32);
    std::wstring sub;
    if (message.status == SessionStatus::Running) {
      sub = std::wstring(L"运行中 · ") + time;
    } else if (message.status == SessionStatus::Done) {
      sub = std::wstring(L"已完成 · ") + time;
    } else {
      sub = time;
    }
    RECT subRect{textLeft, rowTop + static_cast<int>(std::lround(24 * dpi_)), w - padding_,
                 rowTop + static_cast<int>(std::lround(24 * dpi_ + 16 * dpi_))};
    DrawTextW(dc_, sub.c_str(), -1, &subRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  }
  SelectObject(dc_, oldFont);
  DeleteObject(font);
  DeleteObject(nameFont);
  DeleteObject(subFont);

  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    auto* out = reinterpret_cast<unsigned char*>(&pixels[i]);
    unsigned int alpha = mask_[i];
    out[0] = static_cast<unsigned char>(out[0] * alpha / 255);
    out[1] = static_cast<unsigned char>(out[1] * alpha / 255);
    out[2] = static_cast<unsigned char>(out[2] * alpha / 255);
    out[3] = static_cast<unsigned char>(alpha);
  }
}

void CardWindow::present() {
  if (!hwnd_ || !dc_ || !bits_) return;
  HDC screen = GetDC(nullptr);
  if (!screen) return;
  RECT rect{};
  GetWindowRect(hwnd_, &rect);
  POINT destination{rect.left, rect.top};
  POINT source{0, 0};
  SIZE size{width_, height_};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  UpdateLayeredWindow(hwnd_, screen, &destination, &size, dc_, &source, 0, &blend, ULW_ALPHA);
  ReleaseDC(nullptr, screen);
}

void CardWindow::show(int x, int y) {
  if (!hwnd_ || !bits_) return;
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  present();
  visible_ = true;
}

void CardWindow::hide() {
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
  visible_ = false;
}
