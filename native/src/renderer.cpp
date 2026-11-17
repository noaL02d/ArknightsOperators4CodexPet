#include "renderer.h"

#include <algorithm>
#include <cstring>

#include "image.h"
#include "log.h"

namespace {

const char* kShaderSource = R"(
cbuffer Params : register(b0) {
  float2 viewport;
  float2 padding;
};

struct VSInput {
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

VSOutput vsMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.position.x / viewport.x * 2.0 - 1.0,
                           1.0 - input.position.y / viewport.y * 2.0, 0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

Texture2D atlasTexture : register(t0);
SamplerState atlasSampler : register(s0);

float4 psMain(VSOutput input) : SV_TARGET {
  float4 texel = atlasTexture.Sample(atlasSampler, input.uv);
  return texel * input.color;
}
)";

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                      LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

ID3DBlob* compileShader(const char* entry, const char* target) {
  static HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  static D3DCompileFn compile = nullptr;
  if (!compiler) return nullptr;
  if (!compile) {
    compile = reinterpret_cast<D3DCompileFn>(
        reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile")));
  }
  if (!compile) return nullptr;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = compile(kShaderSource, std::strlen(kShaderSource), "pet.hlsl", nullptr, nullptr, entry,
                       target, 0, 0, &code, &errors);
  if (FAILED(hr)) {
    if (errors) {
      OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
      errors->Release();
    }
    return nullptr;
  }
  if (errors) errors->Release();
  return code;
}

}  // namespace

namespace {

const GUID kIID_IDXGIDevice = {0x54ec77fa, 0x1377, 0x44e6,
                               {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
const GUID kIID_IDXGIFactory2 = {0x50c83a1c, 0xe072, 0x4c48,
                                 {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};
const GUID kIID_IDCompositionDevice = {0xc37ea93a, 0xe7aa, 0x450d,
                                       {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
const GUID kIID_ID3D11Texture2D = {0x6f15aaf2, 0xd208, 0x4e89,
                                   {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

template <typename T>
void releaseCom(T*& pointer) {
  if (pointer) {
    pointer->Release();
    pointer = nullptr;
  }
}

}  // namespace

Renderer::~Renderer() { shutdown(); }

bool Renderer::init() {
  if (!createDevice()) return false;
  if (!createShaders()) return false;
  if (!createStates()) return false;
  return true;
}

bool Renderer::initForWindow(HWND window) {
  window_ = window;
  windowed_ = true;
  if (!createDevice()) return false;
  if (!createShaders()) return false;
  if (!createStates()) return false;
  return createSwapChain();
}

bool Renderer::createSwapChain() {
  IDXGIDevice* dxgiDevice = nullptr;
  if (FAILED(device_->QueryInterface(kIID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice)))) {
    return false;
  }
  IDXGIAdapter* adapter = nullptr;
  IDXGIFactory2* factory = nullptr;
  bool ok = false;
  if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
    if (SUCCEEDED(adapter->GetParent(kIID_IDXGIFactory2, reinterpret_cast<void**>(&factory))) &&
        factory) {
      RECT client{};
      GetClientRect(window_, &client);
      DXGI_SWAP_CHAIN_DESC1 desc{};
      desc.Width = static_cast<UINT>(std::max(1L, client.right - client.left));
      desc.Height = static_cast<UINT>(std::max(1L, client.bottom - client.top));
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.Stereo = FALSE;
      desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.BufferCount = 2;
      desc.Scaling = DXGI_SCALING_STRETCH;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
      ok = SUCCEEDED(factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swapChain_));
      if (ok) {
        ok = SUCCEEDED(DCompositionCreateDevice(dxgiDevice, kIID_IDCompositionDevice,
                                                reinterpret_cast<void**>(&compositionDevice_)));
      }
      if (ok) {
        ok = SUCCEEDED(compositionDevice_->CreateTargetForHwnd(window_, TRUE, &compositionTarget_));
      }
      if (ok) ok = SUCCEEDED(compositionDevice_->CreateVisual(&compositionVisual_));
      if (ok) ok = SUCCEEDED(compositionVisual_->SetContent(swapChain_));
      // DirectComposition visuals default to premultiplied alpha.
      if (ok) ok = SUCCEEDED(compositionTarget_->SetRoot(compositionVisual_));
      if (ok) ok = SUCCEEDED(compositionDevice_->Commit());
    }
    releaseCom(adapter);
  }
  releaseCom(factory);
  releaseCom(dxgiDevice);
  return ok;
}

void Renderer::shutdown() {
  releaseCom(backBufferView_);
  releaseCom(backBuffer_);
  releaseCom(compositionVisual_);
  releaseCom(compositionTarget_);
  releaseCom(compositionDevice_);
  releaseCom(swapChain_);
  releaseTargets();
  for (Texture2D* texture : textures_) {
    if (texture->view) texture->view->Release();
    if (texture->texture) texture->texture->Release();
    delete texture;
  }
  textures_.clear();
  if (normalBlend_) normalBlend_->Release();
  if (additiveBlend_) additiveBlend_->Release();
  if (sampler_) sampler_->Release();
  if (rasterizer_) rasterizer_->Release();
  if (indexBuffer_) indexBuffer_->Release();
  if (vertexBuffer_) vertexBuffer_->Release();
  if (constantBuffer_) constantBuffer_->Release();
  if (inputLayout_) inputLayout_->Release();
  if (pixelShader_) pixelShader_->Release();
  if (vertexShader_) vertexShader_->Release();
  if (context_) context_->Release();
  if (device_) device_->Release();
  normalBlend_ = nullptr;
  additiveBlend_ = nullptr;
  sampler_ = nullptr;
  rasterizer_ = nullptr;
  indexBuffer_ = nullptr;
  vertexBuffer_ = nullptr;
  constantBuffer_ = nullptr;
  inputLayout_ = nullptr;
  pixelShader_ = nullptr;
  vertexShader_ = nullptr;
  context_ = nullptr;
  device_ = nullptr;
}

bool Renderer::createDevice() {
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION,
                                 &device_, &level, &context_);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           levels, 3, D3D11_SDK_VERSION, &device_, &level, &context_);
  }
  return SUCCEEDED(hr) && device_ != nullptr;
}

bool Renderer::createShaders() {
  ID3DBlob* vertexCode = compileShader("vsMain", "vs_4_0");
  ID3DBlob* pixelCode = compileShader("psMain", "ps_4_0");
  if (!vertexCode || !pixelCode) {
    if (vertexCode) vertexCode->Release();
    if (pixelCode) pixelCode->Release();
    return false;
  }
  HRESULT hr = device_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                           nullptr, &vertexShader_);
  if (SUCCEEDED(hr)) {
    hr = device_->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr,
                                    &pixelShader_);
  }
  D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, r), D3D11_INPUT_PER_VERTEX_DATA,
       0},
  };
  if (SUCCEEDED(hr)) {
    hr = device_->CreateInputLayout(layout, 3, vertexCode->GetBufferPointer(),
                                    vertexCode->GetBufferSize(), &inputLayout_);
  }
  vertexCode->Release();
  pixelCode->Release();
  if (FAILED(hr)) return false;

  D3D11_BUFFER_DESC buffer{};
  buffer.Usage = D3D11_USAGE_DYNAMIC;
  buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer.ByteWidth = 16;
  if (FAILED(device_->CreateBuffer(&buffer, nullptr, &constantBuffer_))) return false;

  maxVertices_ = 8192 * 4;
  maxIndices_ = 8192 * 6;
  buffer = {};
  buffer.Usage = D3D11_USAGE_DYNAMIC;
  buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer.ByteWidth = static_cast<UINT>(sizeof(Vertex) * maxVertices_);
  if (FAILED(device_->CreateBuffer(&buffer, nullptr, &vertexBuffer_))) return false;

  buffer = {};
  buffer.Usage = D3D11_USAGE_DYNAMIC;
  buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer.ByteWidth = static_cast<UINT>(sizeof(unsigned short) * maxIndices_);
  if (FAILED(device_->CreateBuffer(&buffer, nullptr, &indexBuffer_))) return false;

  return true;
}

bool Renderer::createStates() {
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device_->CreateSamplerState(&sampler, &sampler_))) return false;

  auto makeBlend = [&](D3D11_BLEND destination) {
    D3D11_BLEND_DESC desc{};
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlend = destination;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* state = nullptr;
    if (FAILED(device_->CreateBlendState(&desc, &state))) return (ID3D11BlendState*)nullptr;
    return state;
  };
  normalBlend_ = makeBlend(D3D11_BLEND_INV_SRC_ALPHA);
  additiveBlend_ = makeBlend(D3D11_BLEND_ONE);
  if (!normalBlend_ || !additiveBlend_) return false;

  D3D11_RASTERIZER_DESC rasterizer{};
  rasterizer.FillMode = D3D11_FILL_SOLID;
  rasterizer.CullMode = D3D11_CULL_NONE;
  rasterizer.FrontCounterClockwise = FALSE;
  rasterizer.DepthClipEnable = TRUE;
  rasterizer.ScissorEnable = FALSE;
  rasterizer.MultisampleEnable = FALSE;
  if (FAILED(device_->CreateRasterizerState(&rasterizer, &rasterizer_))) return false;
  return true;
}

void Renderer::releaseTargets() {
  if (targetView_) targetView_->Release();
  if (targetTexture_) targetTexture_->Release();
  if (readbackTexture_) readbackTexture_->Release();
  targetView_ = nullptr;
  targetTexture_ = nullptr;
  readbackTexture_ = nullptr;
  if (bitmap_) {
    if (memoryDC_ && previousBitmap_) SelectObject(memoryDC_, previousBitmap_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bitmapBits_ = nullptr;
    previousBitmap_ = nullptr;
  }
  if (memoryDC_) {
    DeleteDC(memoryDC_);
    memoryDC_ = nullptr;
  }
}

bool Renderer::resize(int width, int height) {
  width = std::max(1, width);
  height = std::max(1, height);
  if (windowed_) {
    if (width_ == width && height_ == height && backBufferView_) return true;
    width_ = width;
    height_ = height;
    releaseCom(backBufferView_);
    releaseCom(backBuffer_);
    if (!swapChain_) return false;
    HRESULT hr = swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                           DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;
    hr = swapChain_->GetBuffer(0, kIID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer_));
    if (FAILED(hr)) return false;
    return SUCCEEDED(device_->CreateRenderTargetView(backBuffer_, nullptr, &backBufferView_));
  }
  if (width_ == width && height_ == height && targetTexture_) return true;
  releaseTargets();
  width_ = width;
  height_ = height;

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device_->CreateTexture2D(&desc, nullptr, &targetTexture_))) return false;
  if (FAILED(device_->CreateRenderTargetView(targetTexture_, nullptr, &targetView_))) return false;

  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device_->CreateTexture2D(&desc, nullptr, &readbackTexture_))) return false;

  memoryDC_ = CreateCompatibleDC(nullptr);
  if (!memoryDC_) return false;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  bitmap_ = CreateDIBSection(memoryDC_, &info, DIB_RGB_COLORS, &bitmapBits_, nullptr, 0);
  if (!bitmap_ || !bitmapBits_) return false;
  previousBitmap_ = SelectObject(memoryDC_, bitmap_);
  return true;
}

Texture2D* Renderer::createTexture(const unsigned char* rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0) return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = rgba;
  data.SysMemPitch = static_cast<UINT>(width * 4);
  auto* result = new Texture2D();
  if (FAILED(device_->CreateTexture2D(&desc, &data, &result->texture))) {
    delete result;
    return nullptr;
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = desc.Format;
  view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view.Texture2D.MipLevels = 1;
  if (FAILED(device_->CreateShaderResourceView(result->texture, &view, &result->view))) {
    result->texture->Release();
    delete result;
    return nullptr;
  }
  result->width = width;
  result->height = height;
  textures_.push_back(result);
  return result;
}

void Renderer::begin() {
  batch_.texture = nullptr;
  batch_.blend = Blend::Normal;
  batch_.vertices.clear();
  batch_.indices.clear();
  const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  ID3D11RenderTargetView* view = windowed_ ? backBufferView_ : targetView_;
  context_->OMSetRenderTargets(1, &view, nullptr);
  context_->ClearRenderTargetView(view, clear);
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width_);
  viewport.Height = static_cast<float>(height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);
  context_->RSSetState(rasterizer_);
  context_->IASetInputLayout(inputLayout_);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vertexShader_, nullptr, 0);
  context_->PSSetShader(pixelShader_, nullptr, 0);
  context_->PSSetSamplers(0, 1, &sampler_);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context_->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    float values[4] = {static_cast<float>(width_), static_cast<float>(height_), 0.0f, 0.0f};
    std::memcpy(mapped.pData, values, sizeof(values));
    context_->Unmap(constantBuffer_, 0);
  }
  context_->VSSetConstantBuffers(0, 1, &constantBuffer_);
}

void Renderer::flush() {
  if (batch_.vertices.empty() || !batch_.texture) return;
  if (static_cast<int>(batch_.vertices.size()) > maxVertices_ ||
      static_cast<int>(batch_.indices.size()) > maxIndices_) {
    batch_.vertices.clear();
    batch_.indices.clear();
    return;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, batch_.vertices.data(), batch_.vertices.size() * sizeof(Vertex));
  context_->Unmap(vertexBuffer_, 0);
  if (FAILED(context_->Map(indexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, batch_.indices.data(), batch_.indices.size() * sizeof(unsigned short));
  context_->Unmap(indexBuffer_, 0);

  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  context_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
  context_->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R16_UINT, 0);
  context_->PSSetShaderResources(0, 1, &batch_.texture->view);
  ID3D11BlendState* blend = batch_.blend == Blend::Additive ? additiveBlend_ : normalBlend_;
  float factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  context_->OMSetBlendState(blend, factor, 0xFFFFFFFF);
  context_->DrawIndexed(static_cast<UINT>(batch_.indices.size()), 0, 0);
  batch_.vertices.clear();
  batch_.indices.clear();
}

void Renderer::draw(const Texture2D* texture, Blend blend, const Vertex* vertices, int vertexCount,
                    const unsigned short* indices, int indexCount) {
  if (!texture || vertexCount <= 0 || indexCount <= 0) return;
  if (batch_.texture != texture || batch_.blend != blend) {
    flush();
    batch_.texture = texture;
    batch_.blend = blend;
  }
  unsigned short base = static_cast<unsigned short>(batch_.vertices.size());
  batch_.vertices.insert(batch_.vertices.end(), vertices, vertices + vertexCount);
  for (int i = 0; i < indexCount; ++i) {
    batch_.indices.push_back(static_cast<unsigned short>(base + indices[i]));
  }
}

void Renderer::drawQuad(const Texture2D* texture, Blend blend, const Vertex quad[4]) {
  static const unsigned short indices[6] = {0, 1, 2, 2, 3, 0};
  draw(texture, blend, quad, 4, indices, 6);
}

void Renderer::end() {
  flush();
  context_->OMSetRenderTargets(0, nullptr, nullptr);
  if (windowed_) return;
  if (!readbackTexture_ || !bitmapBits_) return;
  context_->CopyResource(readbackTexture_, targetTexture_);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(readbackTexture_, 0, D3D11_MAP_READ, 0, &mapped))) return;
  const auto* source = static_cast<const unsigned char*>(mapped.pData);
  auto* destination = static_cast<unsigned char*>(bitmapBits_);
  for (int y = 0; y < height_; ++y) {
    std::memcpy(destination + static_cast<size_t>(y) * width_ * 4,
                source + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(width_) * 4);
  }
  context_->Unmap(readbackTexture_, 0);
}

bool Renderer::present(HWND window, int x, int y) {
  if (windowed_) {
    if (!swapChain_) return false;
    HRESULT hr = swapChain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    return hr == S_OK || hr == DXGI_STATUS_OCCLUDED;
  }
  if (!memoryDC_ || !bitmap_) return false;
  HDC screen = GetDC(nullptr);
  if (!screen) return false;
  POINT destination{x, y};
  POINT source{0, 0};
  SIZE size{width_, height_};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  BOOL ok = UpdateLayeredWindow(window, screen, &destination, &size, memoryDC_, &source, 0, &blend,
                                ULW_ALPHA);
  static int reported = 0;
  if (!ok && reported < 3) {
    ++reported;
    PETLOG("pet: UpdateLayeredWindow failed, err %lu\n", GetLastError());
  }
  ReleaseDC(nullptr, screen);
  return ok != FALSE;
}

unsigned char Renderer::alphaAt(int x, int y) const {
  if (!bitmapBits_ || x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
  const auto* pixels = static_cast<const unsigned char*>(bitmapBits_);
  return pixels[(static_cast<size_t>(y) * width_ + x) * 4 + 3];
}

bool Renderer::savePng(const std::wstring& path) const {
  if (!bitmapBits_) return false;
  std::vector<unsigned char> rgba(static_cast<size_t>(width_) * height_ * 4);
  const auto* pixels = static_cast<const unsigned char*>(bitmapBits_);
  for (size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i + 0] = pixels[i + 2];
    rgba[i + 1] = pixels[i + 1];
    rgba[i + 2] = pixels[i + 0];
    rgba[i + 3] = pixels[i + 3];
  }
  return writePngRGBA(path, rgba.data(), width_, height_);
}
