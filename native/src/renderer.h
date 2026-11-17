#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>

#include <string>
#include <vector>

enum class Blend { Normal, Additive };

struct Texture2D {
  ID3D11Texture2D* texture = nullptr;
  ID3D11ShaderResourceView* view = nullptr;
  int width = 0;
  int height = 0;
};

struct Vertex {
  float x, y, u, v;
  float r, g, b, a;
};

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  bool init();
  bool initForWindow(HWND window);
  void shutdown();

  // Recreates the render target, readback texture and layered-window bitmap.
  bool resize(int width, int height);

  // Uploads straight-alpha RGBA pixels as a premultiplied texture.
  Texture2D* createTexture(const unsigned char* rgba, int width, int height);

  void begin();
  void draw(const Texture2D* texture, Blend blend, const Vertex* vertices, int vertexCount,
            const unsigned short* indices, int indexCount);
  void drawQuad(const Texture2D* texture, Blend blend, const Vertex quad[4]);
  void end();

  bool present(HWND window, int x, int y);
  bool savePng(const std::wstring& path) const;
  bool windowed() const { return windowed_; }

  int width() const { return width_; }
  int height() const { return height_; }
  const unsigned char* pixels() const {
    return static_cast<const unsigned char*>(bitmapBits_);
  }
  unsigned char alphaAt(int x, int y) const;

 private:
  struct Batch {
    const Texture2D* texture = nullptr;
    Blend blend = Blend::Normal;
    std::vector<Vertex> vertices;
    std::vector<unsigned short> indices;
  };

  bool createDevice();
  bool createShaders();
  bool createStates();
  bool createSwapChain();
  void flush();
  void releaseTargets();

  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  ID3D11VertexShader* vertexShader_ = nullptr;
  ID3D11PixelShader* pixelShader_ = nullptr;
  ID3D11InputLayout* inputLayout_ = nullptr;
  ID3D11Buffer* constantBuffer_ = nullptr;
  ID3D11Buffer* vertexBuffer_ = nullptr;
  ID3D11Buffer* indexBuffer_ = nullptr;
  ID3D11SamplerState* sampler_ = nullptr;
  ID3D11BlendState* normalBlend_ = nullptr;
  ID3D11BlendState* additiveBlend_ = nullptr;
  ID3D11RasterizerState* rasterizer_ = nullptr;

  ID3D11Texture2D* targetTexture_ = nullptr;
  ID3D11RenderTargetView* targetView_ = nullptr;
  ID3D11Texture2D* readbackTexture_ = nullptr;

  HDC memoryDC_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  void* bitmapBits_ = nullptr;
  HGDIOBJ previousBitmap_ = nullptr;

  bool windowed_ = false;
  HWND window_ = nullptr;
  IDXGISwapChain1* swapChain_ = nullptr;
  IDCompositionDevice* compositionDevice_ = nullptr;
  IDCompositionTarget* compositionTarget_ = nullptr;
  IDCompositionVisual* compositionVisual_ = nullptr;
  ID3D11Texture2D* backBuffer_ = nullptr;
  ID3D11RenderTargetView* backBufferView_ = nullptr;

  std::vector<Texture2D*> textures_;

  Batch batch_;
  int width_ = 0;
  int height_ = 0;
  int maxVertices_ = 0;
  int maxIndices_ = 0;
};
