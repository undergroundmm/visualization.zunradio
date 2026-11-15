/*
 *  Copyright (C) 2026 Underground Media Management s.r.o.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// DirectX 11 renderer of the ZUN radio scene (Windows, Xbox)

#include "Scena.h"

#include <chrono>
#include <iterator>
#include <cstring>

#include <d3d11_1.h>
#include <kodi/addon-instance/Visualization.h>
#include <kodi/gui/General.h>

namespace
{
// Precompiled shaders (CMakeLists.txt, fxc)
#include "ScenaPixelShader.inc"
#include "ScenaVertexShader.inc"

// Windows of Python add-ons (WindowXML), Kodi xbmc/guilib/WindowIDs.h; the scene window of
// plugin.audio.zunradio is one of them, skin menus and the standard visualisation are not
constexpr int WINDOW_PYTHON_START = 13000;
constexpr int WINDOW_PYTHON_END = 13099;
constexpr size_t MAX_VERTICES = 6 * 128;

struct cbViewPort
{
  float width;
  float height;
  float align1, align2;
};

double NowMs()
{
  return zun::Scene::Now();
}

template<class T>
void SafeRelease(T*& p)
{
  if (p)
  {
    p->Release();
    p = nullptr;
  }
}
} // namespace

class ATTR_DLL_LOCAL CVisualizationZun : public kodi::addon::CInstanceVisualization
{
public:
  explicit CVisualizationZun(const kodi::addon::IInstanceInfo& instance);
  ~CVisualizationZun() override;

  bool Start(int channels, int samplesPerSec, int bitsPerSample,
             const std::string& songName) override;
  void Render() override;
  bool IsDirty() override { return true; }
  bool GetPresets(std::vector<std::string>& presets) override
  {
    presets.assign(std::begin(zun::PRESETS), std::end(zun::PRESETS));
    return true;
  }
  int GetActivePreset() override { return m_card ? m_scene.PulseState(NowMs()) : 0; }
  bool IsLocked() override { return m_card && m_scene.Flash(NowMs()); }
  void AudioData(const float* audioData, size_t audioDataLength) override;

private:
  bool InitRendererObjects();

  zun::Scene m_scene;
  bool m_card = false;
  std::vector<zun::Shape> m_shapes;
  std::vector<zun::Vertex> m_vertices;

  ID3D11Device* m_device = nullptr;
  ID3D11DeviceContext* m_context = nullptr;
  ID3D11VertexShader* m_vShader = nullptr;
  ID3D11PixelShader* m_pShader = nullptr;
  ID3D11InputLayout* m_inputLayout = nullptr;
  ID3D11Buffer* m_vBuffer = nullptr;
  ID3D11Buffer* m_cViewPort = nullptr;
  ID3D11BlendState* m_blend = nullptr;
  float m_width = 0.0f, m_height = 0.0f;
  bool m_ok = false;
};

CVisualizationZun::~CVisualizationZun()
{
  SafeRelease(m_blend);
  SafeRelease(m_cViewPort);
  SafeRelease(m_vBuffer);
  SafeRelease(m_inputLayout);
  SafeRelease(m_vShader);
  SafeRelease(m_pShader);
  SafeRelease(m_device);
}

CVisualizationZun::CVisualizationZun(const kodi::addon::IInstanceInfo& instance)
  : CInstanceVisualization(instance)
{
  m_width = static_cast<float>(Width());
  m_height = static_cast<float>(Height());
  m_context = static_cast<ID3D11DeviceContext*>(Device());
  if (m_context)
  {
    m_context->GetDevice(&m_device);
    m_ok = InitRendererObjects();
  }
  if (!m_ok)
    kodi::Log(ADDON_LOG_ERROR, "Failed to create the DirectX objects");
}

bool CVisualizationZun::Start(int channels, int samplesPerSec, int bitsPerSample,
                              const std::string& songName)
{
  (void)bitsPerSample;
  (void)songName;
  m_scene.Start(channels, samplesPerSec);
  return true;
}

void CVisualizationZun::AudioData(const float* audioData, size_t audioDataLength)
{
  m_scene.AudioData(audioData, audioDataLength);
}

void CVisualizationZun::Render()
{
  if (!m_ok)
    return;
  // The red card only under the cover of the scene window, not in the standard visualisation
  // nor behind the skin menus, where Estuary draws the chosen visualisation as background
  const int window = kodi::gui::GetCurrentWindowId();
  const bool card = window >= WINDOW_PYTHON_START && window <= WINDOW_PYTHON_END;
  m_card = card;
  m_scene.Frame(NowMs(), m_width, m_height, card, m_shapes);
  zun::Scene::ToVertices(m_shapes, m_vertices);
  char report[160];
  if (m_scene.Report(NowMs(), report, sizeof(report)))
    kodi::Log(ADDON_LOG_DEBUG, "%s", report);
  if (m_vertices.size() > MAX_VERTICES)
    m_vertices.resize(MAX_VERTICES);

  D3D11_MAPPED_SUBRESOURCE res;
  if (S_OK != m_context->Map(m_vBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
    return;
  std::memcpy(res.pData, m_vertices.data(), sizeof(zun::Vertex) * m_vertices.size());
  m_context->Unmap(m_vBuffer, 0);

  const UINT stride = sizeof(zun::Vertex), offset = 0;
  m_context->IASetVertexBuffers(0, 1, &m_vBuffer, &stride, &offset);
  m_context->IASetInputLayout(m_inputLayout);
  m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_context->VSSetShader(m_vShader, nullptr, 0);
  m_context->VSSetConstantBuffers(0, 1, &m_cViewPort);
  m_context->PSSetShader(m_pShader, nullptr, 0);
  const float factor[4] = {0, 0, 0, 0};
  m_context->OMSetBlendState(m_blend, factor, 0xFFFFFFFF);
  m_context->Draw(static_cast<UINT>(m_vertices.size()), 0);
}

bool CVisualizationZun::InitRendererObjects()
{
  if (S_OK != m_device->CreateVertexShader(ScenaVertexShaderCode, sizeof(ScenaVertexShaderCode),
                                           nullptr, &m_vShader))
    return false;

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(zun::Vertex, x),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(zun::Vertex, u),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(zun::Vertex, r),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(zun::Vertex, type),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (S_OK != m_device->CreateInputLayout(layout, ARRAYSIZE(layout), ScenaVertexShaderCode,
                                          sizeof(ScenaVertexShaderCode), &m_inputLayout))
    return false;

  if (S_OK != m_device->CreatePixelShader(ScenaPixelShaderCode, sizeof(ScenaPixelShaderCode),
                                          nullptr, &m_pShader))
    return false;

  CD3D11_BUFFER_DESC desc(static_cast<UINT>(sizeof(zun::Vertex) * MAX_VERTICES),
                          D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
  if (S_OK != m_device->CreateBuffer(&desc, nullptr, &m_vBuffer))
    return false;

  desc.ByteWidth = sizeof(cbViewPort);
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.CPUAccessFlags = 0;
  const cbViewPort viewPort = {m_width, m_height, 0.0f, 0.0f};
  D3D11_SUBRESOURCE_DATA initData = {};
  initData.pSysMem = &viewPort;
  if (S_OK != m_device->CreateBuffer(&desc, &initData, &m_cViewPort))
    return false;

  D3D11_BLEND_DESC blend = {};
  blend.RenderTarget[0].BlendEnable = TRUE;
  blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (S_OK != m_device->CreateBlendState(&blend, &m_blend))
    return false;

  return true;
}

// The add-on itself; Kodi may ask for more visualisation instances at once (a skin window
// and the scene window of plugin.audio.zunradio), each gets its own object.
class ATTR_DLL_LOCAL CAddonZun : public kodi::addon::CAddonBase
{
public:
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override
  {
    if (!instance.IsType(ADDON_INSTANCE_VISUALIZATION))
      return ADDON_STATUS_NOT_IMPLEMENTED;
    hdl = new CVisualizationZun(instance);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CAddonZun)
