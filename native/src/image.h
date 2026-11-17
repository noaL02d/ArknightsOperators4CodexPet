#pragma once

#include <string>
#include <vector>

bool readFileBytes(const std::wstring& path, std::vector<unsigned char>& out);
bool readFileText(const std::wstring& path, std::string& out);
std::wstring widen(const std::string& text);
std::string narrow(const std::wstring& text);

// Decodes an image file into tightly packed RGBA8 (straight alpha).
bool loadImageRGBA(const std::wstring& path, std::vector<unsigned char>& rgba, int& width,
                   int& height);

// Writes tightly packed RGBA8 to a PNG file.
bool writePngRGBA(const std::wstring& path, const unsigned char* rgba, int width, int height);
