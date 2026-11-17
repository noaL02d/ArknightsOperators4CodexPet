#include "image.h"

#include <windows.h>

#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../vendor/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

std::wstring widen(const std::string& text) {
  if (text.empty()) return std::wstring();
  int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
  return result;
}

std::string narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  std::string result(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length,
                      nullptr, nullptr);
  return result;
}

bool readFileBytes(const std::wstring& path, std::vector<unsigned char>& out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (1024LL * 1024LL * 64LL)) {
    CloseHandle(file);
    return false;
  }
  out.resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  bool ok = true;
  size_t offset = 0;
  while (offset < out.size()) {
    DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 1u << 20));
    if (!ReadFile(file, out.data() + offset, chunk, &read, nullptr) || read == 0) {
      ok = false;
      break;
    }
    offset += read;
  }
  CloseHandle(file);
  return ok;
}

bool readFileText(const std::wstring& path, std::string& out) {
  std::vector<unsigned char> bytes;
  if (!readFileBytes(path, bytes)) return false;
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
    out.erase(0, 3);
  }
  return true;
}

bool loadImageRGBA(const std::wstring& path, std::vector<unsigned char>& rgba, int& width,
                   int& height) {
  std::vector<unsigned char> bytes;
  if (!readFileBytes(path, bytes)) return false;
  int channels = 0;
  stbi_uc* pixels =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
  if (!pixels) return false;
  rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
  stbi_image_free(pixels);
  return true;
}

bool writePngRGBA(const std::wstring& path, const unsigned char* rgba, int width, int height) {
  FILE* file = _wfopen(path.c_str(), L"wb");
  if (!file) return false;
  auto write = [](void* context, void* data, int size) {
    fwrite(data, 1, static_cast<size_t>(size), static_cast<FILE*>(context));
  };
  int ok = stbi_write_png_to_func(write, file, width, height, 4, rgba, width * 4);
  fclose(file);
  return ok != 0;
}
