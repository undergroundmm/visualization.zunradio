/*
 *  Copyright (C) 2026 Underground Media Management s.r.o.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// ZUN radio scene: one quad per shape, the shape itself is cut out in the pixel shader

cbuffer cbViewPort : register(b0)
{
  float g_width;
  float g_height;
  float align1;
  float align2;
};

struct VS_IN
{
  float2 pos : POSITION;
  float2 uv : TEXCOORD0;
  float4 col : COLOR;
  float3 params : TEXCOORD1;
};

struct VS_OUT
{
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 col : COLOR;
  float3 params : TEXCOORD1;
};

VS_OUT main(VS_IN v)
{
  VS_OUT r;
  r.pos = float4(v.pos.x / g_width * 2.0 - 1.0, 1.0 - v.pos.y / g_height * 2.0, 0.0, 1.0);
  r.uv = v.uv;
  r.col = v.col;
  r.params = v.params;
  return r;
}
