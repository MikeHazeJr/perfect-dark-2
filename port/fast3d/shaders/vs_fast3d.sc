$input a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_color0, a_color1, a_color2, a_color3, a_texcoord6, a_texcoord7, a_normal, a_tangent, a_bitangent
$output v_texcoord0, v_texcoord1, v_clamp, v_fog, v_gray, v_input1, v_input2, v_input3, v_input4, v_input5, v_input6, v_input7

/*
 * fast3d vertex shader for the bgfx backend (port/fast3d/gfx_bgfx.cpp).
 *
 * fast3d transforms, lights and fogs vertices on the CPU, so this is a pass
 * through, like the vertex shaders gfx_opengl.cpp writes at runtime. The
 * vertex layout of each draw is the legacy layout of its combiner: the
 * backend maps each legacy float group to the attribute named below and
 * leaves the rest out; an attribute missing from a layout is disabled and
 * its varying is never read by fs_fast3d.sc.
 *
 *   a_position            aVtxPos (clip space, from the CPU)
 *   a_texcoord0/1         aTexCoord0/1
 *   a_texcoord2/3/4/5 .x  aTexClampS0, aTexClampT0, aTexClampS1, aTexClampT1
 *   a_color0              aFog
 *   a_color1              aGrayscaleColor
 *   a_color2, a_color3, a_texcoord6, a_texcoord7, a_normal, a_tangent,
 *   a_bitangent           aInput1 .. aInput7
 */

#include <bgfx_shader.sh>

void main()
{
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
	v_clamp = vec4(a_texcoord2.x, a_texcoord3.x, a_texcoord4.x, a_texcoord5.x);
	v_fog = a_color0;
	v_gray = a_color1;
	v_input1 = a_color2;
	v_input2 = a_color3;
	v_input3 = a_texcoord6;
	v_input4 = a_texcoord7;
	v_input5 = a_normal;
	v_input6 = a_tangent;
	v_input7 = a_bitangent;
	gl_Position = a_position;
}
