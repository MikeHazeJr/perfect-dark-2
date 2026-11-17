$input a_position, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_color0

/*
 * Scenario scene renderer vertex shader for the bgfx backend
 * (port/fast3d/scenario_scene_renderer.cpp). Same arithmetic as its GL
 * shader: u_VP * vec4(a_Pos, 1.0).
 */

#include <bgfx_shader.sh>

uniform mat4 u_sceneVP;

void main()
{
	gl_Position = mul(u_sceneVP, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
	v_color0 = a_color0;
}
