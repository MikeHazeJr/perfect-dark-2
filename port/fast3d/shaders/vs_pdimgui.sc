$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

/*
 * Dear ImGui vertex shader for the bgfx backend (port/fast3d/imgui_impl_bgfx.cpp).
 * Same arithmetic as imgui_impl_opengl3.cpp: ProjMtx * vec4(Position.xy, 0, 1).
 */

#include <bgfx_shader.sh>

uniform mat4 u_imguiProj;

void main()
{
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
	gl_Position = mul(u_imguiProj, vec4(a_position.xy, 0.0, 1.0));
}
