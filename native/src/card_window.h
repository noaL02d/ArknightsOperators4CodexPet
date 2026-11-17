#pragma once

#include <windows.h>

#include <vector>

#include "session.h"

class CardWindow {
 public:
  static constexpr UINT kRowClick = WM_APP + 21;

  bool create(HINSTANCE instance, HWND owner);
  void destroy();

  void setMessages(std::vector<SessionMessage> messages, float dpiScale);
  void show(int x, int y);
  void moveTo(int x, int y);
  void hide();
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }
  int rowAt(int clientY) const;
  int width() const { return width_; }
  int height() const { return height_; }
  std::vector<SessionMessage>& messages() { return messages_; }

 private:
  static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
  bool ensureBitmap(int width, int height);
  void paint();
  void present();

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  void* bits_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  float dpi_ = 1.0f;
  int padding_ = 0;
  int headerHeight_ = 0;
  int rowHeight_ = 0;
  std::vector<SessionMessage> messages_;
  std::vector<unsigned char> mask_;
  bool visible_ = false;
};
