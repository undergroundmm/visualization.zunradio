/*
 *  Copyright (C) 2026 Underground Media Management s.r.o.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Scena.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace zun
{
namespace
{

constexpr float PI = 3.14159265358979f;

// Motion from the video generator (video.py, POHYB) and scena.js; lengths in pixels of the
// 1080 px wide video, here multiplied by K = card of the video (822 px) to card of the scene
struct Motion
{
  static constexpr int dots = 26, rings = 14;
  static constexpr float dotsR[2] = {5, 24}, ringsR[2] = {18, 70};
  static constexpr float dotsGrow = 1.0f, dotsBounce = 60;
  static constexpr float ringsGrow = 0.6f, ringsBounce = 45;
  static constexpr float drift[2] = {20, 70}, driftHz[2] = {0.1f, 0.3f};
  static constexpr float travel[2] = {120, 320}, travelHz[2] = {0.015f, 0.05f};
  static constexpr float cardPulse = 0.03f;
  static constexpr float waveFrom = 0.85f, waveMs = 600;
  static constexpr int flashEvery = 8;
  static constexpr float lightDelay = 100, lightLevel = 36.0f / 255.0f, lightDecay = 0.8f;
  static constexpr float decay = 0.8f; // pulse per video frame (33 ms) falls to 80 %
  static constexpr float beatFrom = 0.6f; // rise of the bass against the ceiling that counts as a beat
  static constexpr float beatGap = 280; // ms between beats at least (up to 214 BPM)
};

constexpr float K = std::min(SPACE_H * 0.56f, SPACE_W * 0.42f) / 822.0f;

constexpr float RED[3] = {229 / 255.0f, 35 / 255.0f, 34 / 255.0f};
// Pure black instead of #060606 of the web: logos of record labels are white on black (no alpha)
constexpr float BLACK[3] = {0.0f, 0.0f, 0.0f};

class Random
{
public:
  explicit Random(uint64_t seed) : m_x(seed) {}
  float Next()
  {
    m_x = (m_x * 1103515245ULL + 12345ULL) % 2147483648ULL;
    return static_cast<float>(m_x) / 2147483648.0f;
  }
  float Between(const float (&range)[2]) { return range[0] + Next() * (range[1] - range[0]); }

private:
  uint64_t m_x;
};

float DecayFactor(double dt, float perFrame)
{
  return static_cast<float>(std::pow(perFrame, dt / 33.3));
}

} // namespace

// ---------- audio ----------

void Biquad::LowPass(float rate, float freq)
{
  const float w = 2 * PI * freq / rate, c = std::cos(w), alpha = std::sin(w) / (2 * 0.7071f);
  const float a0 = 1 + alpha;
  b0 = (1 - c) / 2 / a0;
  b1 = (1 - c) / a0;
  b2 = b0;
  a1 = -2 * c / a0;
  a2 = (1 - alpha) / a0;
}

void Biquad::HighPass(float rate, float freq)
{
  const float w = 2 * PI * freq / rate, c = std::cos(w), alpha = std::sin(w) / (2 * 0.7071f);
  const float a0 = 1 + alpha;
  b0 = (1 + c) / 2 / a0;
  b1 = -(1 + c) / a0;
  b2 = b0;
  a1 = -2 * c / a0;
  a2 = (1 - alpha) / a0;
}

float Biquad::Process(float x)
{
  const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = (std::fabs(y) < 1e-20f) ? 0.0f : y; // no denormals in silence
  return y1;
}

void Energy::Push(float x)
{
  const float sq = x * x;
  m_sum += sq - m_buf[m_pos];
  m_buf[m_pos] = sq;
  m_pos = (m_pos + 1) % m_buf.size();
  if (++m_sinceExact >= 48000) // rounding of the running sum stays small
  {
    m_sinceExact = 0;
    m_sum = 0.0;
    for (float v : m_buf)
      m_sum += v;
  }
  if (m_sum < 0.0)
    m_sum = 0.0;
}

// Rise of the energy against the state 30 ms ago gives a pulse 0..1 with an instant attack
// and a decay; the ceiling is the 97th percentile of the rises in the last ~10 s.
// Returns the strength of the new rise.
float Band::Update(float energy, double t, double dt)
{
  pulse *= DecayFactor(dt, Motion::decay);
  const float db = 10.0f * std::log10(energy + 1e-10f);
  if (!m_hasOld)
  {
    m_hasOld = true;
    m_old = db;
    m_oldTime = t;
    return 0.0f;
  }
  if (t - m_oldTime < 30)
    return 0.0f;
  const float rise = std::max(0.0f, db - m_old);
  m_old = db;
  m_oldTime = t;
  m_rises.push_back(rise);
  if (m_rises.size() > 300)
    m_rises.pop_front();
  if (++m_sinceCeiling >= 30)
  {
    m_sinceCeiling = 0;
    std::vector<float> sorted(m_rises.begin(), m_rises.end());
    std::sort(sorted.begin(), sorted.end());
    m_ceiling = std::max(1.0f, sorted[static_cast<size_t>(sorted.size() * 0.97)]);
  }
  const float strength = std::min(1.0f, rise / m_ceiling);
  if (strength > pulse)
    pulse = strength;
  return strength;
}

void Band::Decay(double dt)
{
  pulse *= DecayFactor(dt, Motion::decay);
  m_hasOld = false;
}

// ---------- scene ----------

Scene::Scene()
{
  Start(2, 48000);
  Layout();
}

double Scene::Now()
{
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void Scene::Start(int channels, int samplesPerSec)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_channels = std::max(1, channels);
  m_rate = static_cast<float>(samplesPerSec > 0 ? samplesPerSec : 48000);
  m_lp1.LowPass(m_rate, 120); // basses below 120 Hz as in the video (ffmpeg lowpass 120, twice)
  m_lp2.LowPass(m_rate, 120);
  m_hp.HighPass(m_rate, 3000); // highs above 3 kHz (ffmpeg highpass 3k)
}

void Scene::AudioData(const float* data, size_t length)
{
  if (!data)
    return;
  std::lock_guard<std::mutex> lock(m_mutex);
  const size_t step = static_cast<size_t>(m_channels);
  for (size_t i = 0; i + step <= length; i += step)
  {
    float mono = 0.0f;
    for (size_t c = 0; c < step; c++)
      mono += data[i + c];
    mono /= static_cast<float>(step);
    m_bassEnergy.Push(m_lp2.Process(m_lp1.Process(mono)));
    m_trebleEnergy.Push(m_hp.Process(mono));
  }
  m_hasAudio = true;
  Analyse(Now());
}

// From the last samples of each band (as an AnalyserNode of the web), about every 20 ms
void Scene::Analyse(double t)
{
  const double dt = m_lastAnalysis < 0 ? 0.0 : std::min(100.0, t - m_lastAnalysis);
  const float held = WindowPulseAt(t);
  const float strength = m_bass.Update(m_bassEnergy.Value(), t, dt);
  m_treble.Update(m_trebleEnergy.Value(), t, dt);
  m_lastAnalysis = t;
  if (strength >= Motion::beatFrom && t - m_lastBeat > Motion::beatGap)
    Beat(t, strength);
  // The window gets the peak at once and holds it, then the pulse decays as on the web
  if (m_bass.pulse >= held)
  {
    m_peak = m_bass.pulse;
    m_peakTime = t;
  }
}

float Scene::BassAt(double t) const
{
  if (m_lastAnalysis < 0)
    return 0.0f;
  return m_bass.pulse * DecayFactor(std::max(0.0, t - m_lastAnalysis), Motion::decay);
}

float Scene::TrebleAt(double t) const
{
  if (m_lastAnalysis < 0)
    return 0.0f;
  return m_treble.pulse * DecayFactor(std::max(0.0, t - m_lastAnalysis), Motion::decay);
}

float Scene::WindowPulseAt(double t) const
{
  const float held =
      m_peak * DecayFactor(std::max(0.0, t - m_peakTime - PEAK_HOLD_MS), Motion::decay);
  return std::max(BassAt(t), held);
}

void Scene::Layout()
{
  Random rnd(7);
  auto create = [&rnd](int count, const float (&radius)[2]) {
    std::vector<Element> result;
    for (int i = 0; i < count; i++)
    {
      Element e;
      e.x = rnd.Next() * SPACE_W;
      e.y = rnd.Next() * SPACE_H;
      e.r = rnd.Between(radius);
      e.ax = rnd.Between(Motion::drift);
      e.ay = rnd.Between(Motion::drift);
      e.f = rnd.Between(Motion::driftHz);
      e.phase = rnd.Next() * 2 * PI;
      e.px = rnd.Between(Motion::travel);
      e.py = rnd.Between(Motion::travel);
      e.pfx = rnd.Between(Motion::travelHz);
      e.pfy = rnd.Between(Motion::travelHz);
      e.pphx = rnd.Next() * 2 * PI;
      e.pphy = rnd.Next() * 2 * PI;
      result.push_back(e);
    }
    return result;
  };
  m_dots = create(Motion::dots, Motion::dotsR);
  m_rings = create(Motion::rings, Motion::ringsR);
}

void Scene::Beat(double t, float strength)
{
  m_beats++;
  const int beats = m_beats;
  m_lastBeat = t;
  m_direction = -m_direction;
  if (strength >= Motion::waveFrom || beats % 4 == 0)
    m_waves.push_back(t);
  if (beats % Motion::flashEvery == 0)
  {
    m_flashUntil = t + FLASH_MS;
    m_flashLight = t + Motion::lightDelay;
  }
}

void Scene::Position(const Element& e, double sec, float bounce, float& x, float& y) const
{
  const double s = sec;
  x = e.x + e.px * K * static_cast<float>(std::sin(2 * PI * e.pfx * s + e.pphx)) +
      e.ax * K * static_cast<float>(std::sin(2 * PI * e.f * s + e.phase));
  y = e.y + e.py * K * static_cast<float>(std::sin(2 * PI * e.pfy * s + e.pphy)) +
      e.ay * K * static_cast<float>(std::cos(2 * PI * e.f * s + e.phase));
  const float dx = x - CARD_CX, dy = y - CARD_CY;
  float d = std::sqrt(dx * dx + dy * dy);
  if (d < 1.0f)
    d = 1.0f;
  x += dx / d * bounce;
  y += dy / d * bounce;
}

void Scene::Frame(double t, float width, float height, bool card, std::vector<Shape>& shapes)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_start < 0)
    m_start = t;
  const double dt = m_lastFrame < 0 ? 0.0 : std::min(100.0, t - m_lastFrame);
  m_lastFrame = t;

  if (m_flashLight > 0 && t >= m_flashLight)
  {
    m_light = 1.0f;
    m_flashLight = 0;
  }
  m_light *= DecayFactor(dt, Motion::lightDecay);

  // 1080 space to pixels; circles stay round (height scale), the card follows the skin scaling
  const float sx = width / SPACE_W, sy = height / SPACE_H, sr = sy;
  const float cx = CARD_CX * sx, cy = CARD_CY * sy;
  const double sec = (t - m_start) / 1000.0;
  const float pb = BassAt(t), pv = TrebleAt(t);
  const float aaPx = 1.5f;

  shapes.clear();
  // In the scene window the black is a skin image under the visualisation, so the logo of the
  // record label lies behind the dots and rings, "on the wall" as in the video (Lada 2026-11-08)
  if (!card)
    shapes.push_back({SHAPE_FILL, width / 2, height / 2, width / 2, height / 2, BLACK[0], BLACK[1],
                      BLACK[2], 1.0f, 0.0f, 0.0f});

  if (m_light > 0.01f) // the flash reflects into the room: more towards the edges
  {
    const float R = std::sqrt(width * width + height * height) / 2 + std::fabs(cx - width / 2) +
                    std::fabs(cy - height / 2);
    const float inner = std::min(0.9f, CARD_SIDE * 0.5f * sy / R);
    shapes.push_back({SHAPE_GLOW, cx, cy, R, R, 1.0f, 1.0f, 1.0f,
                      Motion::lightLevel * m_light, inner, 0.0f});
  }

  for (const Element& e : m_dots)
  {
    float x, y;
    Position(e, sec, Motion::dotsBounce * K * pb, x, y);
    const float r = e.r * K * (1 + Motion::dotsGrow * pb) * sr;
    shapes.push_back({SHAPE_DISC, x * sx, y * sy, r, r, RED[0], RED[1], RED[2], 1.0f, 0.0f,
                      aaPx / std::max(r, 1.0f)});
  }

  const float ringWidth = (3 + 6 * pv) * K * sr;
  for (const Element& e : m_rings)
  {
    float x, y;
    Position(e, sec, Motion::ringsBounce * K * pv, x, y);
    const float r = e.r * K * (1 + Motion::ringsGrow * pv) * sr + ringWidth / 2;
    shapes.push_back({SHAPE_RING, x * sx, y * sy, r, r, RED[0], RED[1], RED[2], 1.0f,
                      std::min(1.0f, ringWidth / r), aaPx / std::max(r, 1.0f)});
  }

  // Waves run out from behind the card
  m_waves.erase(std::remove_if(m_waves.begin(), m_waves.end(),
                               [t](double w) { return t - w >= Motion::waveMs; }),
                m_waves.end());
  for (double w : m_waves)
  {
    const float u = static_cast<float>((t - w) / Motion::waveMs);
    const float lw = std::max(1.0f, 12 * K * (1 - u) * sr);
    const float r = (CARD_SIDE * 0.55f + 650 * K * (1 - (1 - u) * (1 - u))) * sr + lw / 2;
    shapes.push_back({SHAPE_RING, cx, cy, r, r, RED[0], RED[1], RED[2], 1 - u,
                      std::min(1.0f, lw / r), aaPx / r});
  }

  // Card pulse: the red card behind the cover of the scene window grows with the bass
  if (card)
  {
    const float grow = 1 + Motion::cardPulse * pb;
    shapes.push_back({SHAPE_FILL, cx, cy, CARD_SIDE / 2 * grow * sx, CARD_SIDE / 2 * grow * sy,
                      RED[0], RED[1], RED[2], 1.0f, 0.0f, 0.0f});
  }
}

int Scene::PulseState(double t)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  // From a pulse of 0.04 on: small changes of the basses move the fields a little, as on the web
  const int level =
      std::min(PULSE_LEVELS, static_cast<int>(WindowPulseAt(t) * PULSE_LEVELS + 0.7f));
  if (level <= 0)
    return 0;
  return m_direction > 0 ? level : PULSE_LEVELS + level;
}

bool Scene::Flash(double t)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return t < m_flashUntil;
}

bool Scene::Report(double t, char* text, size_t size)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (t - m_lastReport < 10000)
    return false;
  m_lastReport = t;
  std::snprintf(text, size, "audio %s, beats %d, bass pulse %.2f, treble pulse %.2f",
                m_hasAudio ? "yes" : "no", m_beats, BassAt(t), TrebleAt(t));
  return true;
}

void Scene::ToVertices(const std::vector<Shape>& shapes, std::vector<Vertex>& vertices)
{
  vertices.clear();
  vertices.reserve(shapes.size() * 6);
  static const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  for (const Shape& s : shapes)
  {
    for (const auto& c : corners)
    {
      vertices.push_back({s.cx + c[0] * s.hw, s.cy + c[1] * s.hh, c[0], c[1], s.r, s.g, s.b, s.a,
                          static_cast<float>(s.type), s.thickness, s.aa});
    }
  }
}

} // namespace zun
