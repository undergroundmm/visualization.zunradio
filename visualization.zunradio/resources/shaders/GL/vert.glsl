#version 150

// ZUN radio scene: one quad per shape, the shape itself is cut out here (Scena.h, ShapeType)
uniform vec2 u_size;

in vec2 a_position;
in vec2 a_uv;
in vec4 a_color;
in vec3 a_params;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_params;

void main()
{
  gl_Position = vec4(a_position.x / u_size.x * 2.0 - 1.0, 1.0 - a_position.y / u_size.y * 2.0, 0.0, 1.0);
  v_uv = a_uv;
  v_color = a_color;
  v_params = a_params;
}
