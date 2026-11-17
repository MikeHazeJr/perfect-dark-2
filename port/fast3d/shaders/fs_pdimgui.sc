$input v_texcoord0, v_color0

/*
 * Dear ImGui fragment shader for the bgfx backend. Same arithmetic as
 * imgui_impl_opengl3.cpp: Frag_Color * texture(Texture, Frag_UV.st).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_imguiTex, 0);

void main()
{
	gl_FragColor = v_color0 * texture2D(s_imguiTex, v_texcoord0.st);
}
