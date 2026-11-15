/*
 *  Copyright (C) 2026 Underground Media Management s.r.o.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// OpenGL and OpenGL ES renderer of the ZUN radio scene (Linux, LibreELEC, Android, macOS)

#include "Scena.h"

#include <chrono>
#include <iterator>
#include <cstddef>

#include <kodi/addon-instance/Visualization.h>
#include <kodi/gui/General.h>
#include <kodi/gui/gl/GL.h>
#include <kodi/gui/gl/Shader.h>

namespace
{
// Windows of Python add-ons (WindowXML), Kodi xbmc/guilib/WindowIDs.h; the scene window of
// plugin.audio.zunradio is one of them, skin menus and the standard visualisation are not
constexpr int WINDOW_PYTHON_START = 13000;
constexpr int WINDOW_PYTHON_END = 13099;

double NowMs()
{
  return zun::Scene::Now();
}
} // namespace

class ATTR_DLL_LOCAL CVisualizationZun : public kodi::addon::CInstanceVisualization,
                                         public kodi::gui::gl::CShaderProgram
{
public:
  explicit CVisualizationZun(const kodi::addon::IInstanceInfo& instance)
    : CInstanceVisualization(instance)
  {
  }
  ~CVisualizationZun() override = default;

  bool Start(int channels, int samplesPerSec, int bitsPerSample,
             const std::string& songName) override;
  void Stop() override;
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

  void OnCompiledAndLinked() override;
  bool OnEnabled() override;

private:
  zun::Scene m_scene;
  bool m_card = false;
  std::vector<zun::Shape> m_shapes;
  std::vector<zun::Vertex> m_vertices;
  GLuint m_vbo = 0;
  GLint m_uSize = -1;
  GLint m_aPosition = -1, m_aUv = -1, m_aColor = -1, m_aParams = -1;
  bool m_startOK = false;
};

bool CVisualizationZun::Start(int channels, int samplesPerSec, int bitsPerSample,
                              const std::string& songName)
{
  (void)bitsPerSample;
  (void)songName;
  const std::string frag = kodi::addon::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/frag.glsl");
  const std::string vert = kodi::addon::GetAddonPath("resources/shaders/" GL_TYPE_STRING "/vert.glsl");
  if (!LoadShaderFiles(vert, frag) || !CompileAndLink())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to create or compile the shaders");
    return false;
  }
  m_scene.Start(channels, samplesPerSec);
  glGenBuffers(1, &m_vbo);
  m_startOK = true;
  return true;
}

void CVisualizationZun::Stop()
{
  if (!m_startOK)
    return;
  m_startOK = false;
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &m_vbo);
  m_vbo = 0;
}

void CVisualizationZun::AudioData(const float* audioData, size_t audioDataLength)
{
  m_scene.AudioData(audioData, audioDataLength);
}

void CVisualizationZun::Render()
{
  if (!m_startOK)
    return;

  // The red card only under the cover of the scene window, not in the standard visualisation
  // nor behind the skin menus, where Estuary draws the chosen visualisation as background
  const int window = kodi::gui::GetCurrentWindowId();
  const bool card = window >= WINDOW_PYTHON_START && window <= WINDOW_PYTHON_END;
  m_card = card;
  m_scene.Frame(NowMs(), static_cast<float>(Width()), static_cast<float>(Height()), card, m_shapes);
  zun::Scene::ToVertices(m_shapes, m_vertices);
  char report[160];
  if (m_scene.Report(NowMs(), report, sizeof(report)))
    kodi::Log(ADDON_LOG_DEBUG, "%s", report);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(zun::Vertex), m_vertices.data(),
               GL_STREAM_DRAW);
  const GLsizei stride = sizeof(zun::Vertex);
  glVertexAttribPointer(m_aPosition, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(zun::Vertex, x)));
  glVertexAttribPointer(m_aUv, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(zun::Vertex, u)));
  glVertexAttribPointer(m_aColor, 4, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(zun::Vertex, r)));
  glVertexAttribPointer(m_aParams, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(zun::Vertex, type)));
  glEnableVertexAttribArray(m_aPosition);
  glEnableVertexAttribArray(m_aUv);
  glEnableVertexAttribArray(m_aColor);
  glEnableVertexAttribArray(m_aParams);

  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  EnableShader();
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
  DisableShader();

  glDisableVertexAttribArray(m_aPosition);
  glDisableVertexAttribArray(m_aUv);
  glDisableVertexAttribArray(m_aColor);
  glDisableVertexAttribArray(m_aParams);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CVisualizationZun::OnCompiledAndLinked()
{
  m_uSize = glGetUniformLocation(ProgramHandle(), "u_size");
  m_aPosition = glGetAttribLocation(ProgramHandle(), "a_position");
  m_aUv = glGetAttribLocation(ProgramHandle(), "a_uv");
  m_aColor = glGetAttribLocation(ProgramHandle(), "a_color");
  m_aParams = glGetAttribLocation(ProgramHandle(), "a_params");
}

bool CVisualizationZun::OnEnabled()
{
  glUniform2f(m_uSize, static_cast<float>(Width()), static_cast<float>(Height()));
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
