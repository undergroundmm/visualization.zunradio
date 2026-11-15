/*
 *  Copyright (C) 2026 Underground Media Management s.r.o.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// Same shapes as resources/shaders/GL/frag.glsl (Scena.h, ShapeType)

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0, float4 col : COLOR,
            float3 params : TEXCOORD1) : SV_TARGET
{
  float type = params.x;
  float thickness = params.y;
  float aa = max(params.z, 0.0001);
  float a = 1.0;
  if (type < 0.5)
  {
    a = 1.0 - smoothstep(1.0 - aa, 1.0, length(uv));
  }
  else if (type < 2.5)
  {
    float d = type < 1.5 ? length(uv) : max(abs(uv.x), abs(uv.y));
    float inner = 1.0 - thickness;
    a = smoothstep(inner - aa, inner, d) * (1.0 - smoothstep(1.0 - aa, 1.0, d));
  }
  else if (type > 3.5)
  {
    a = 0.5 + 0.5 * saturate((length(uv) - thickness) / (1.0 - thickness));
  }
  return float4(col.rgb, col.a * a);
}
