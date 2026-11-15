#version 100

// ZUN radio scene: one quad per shape, the shape itself is cut out here (Scena.h, ShapeType)
precision mediump float;

varying vec2 v_uv;
varying vec4 v_color;
varying vec3 v_params;

void main()
{
  float type = v_params.x;
  float thickness = v_params.y;
  float aa = max(v_params.z, 0.0001);
  float a = 1.0;
  if (type < 0.5)
  {
    a = 1.0 - smoothstep(1.0 - aa, 1.0, length(v_uv));
  }
  else if (type < 2.5)
  {
    float d = type < 1.5 ? length(v_uv) : max(abs(v_uv.x), abs(v_uv.y));
    float inner = 1.0 - thickness;
    a = smoothstep(inner - aa, inner, d) * (1.0 - smoothstep(1.0 - aa, 1.0, d));
  }
  else if (type > 3.5)
  {
    a = 0.5 + 0.5 * clamp((length(v_uv) - thickness) / (1.0 - thickness), 0.0, 1.0);
  }
  gl_FragColor = vec4(v_color.rgb, v_color.a * a);
}
