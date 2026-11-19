$input v_texcoord0, v_texcoord1, v_color0, v_fog

/*
 * Scenario scene renderer fragment shader for the bgfx backend. Same
 * arithmetic as the GL shader in scenario_scene_renderer.cpp.
 *
 * u_sceneParams    x: has secondary texture, y: primary texcoord channel,
 *                  z: secondary texcoord channel, w: alpha cutoff
 * u_sceneFogColor  rgb: the stage fog colour (GL u_FogColor)
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneTex, 0);
SAMPLER2D(s_sceneTex2, 1);

uniform vec4 u_sceneParams;
uniform vec4 u_sceneFogColor;

vec2 pdTexcoord(int channel) {
	return channel == 0 ? v_texcoord0 : v_texcoord1;
}

void main()
{
	vec4 sampled = texture2D(s_sceneTex, pdTexcoord(int(u_sceneParams.y)));
	if (int(u_sceneParams.x) != 0) {
		vec4 sampled2 = texture2D(s_sceneTex2, pdTexcoord(int(u_sceneParams.z)));
		sampled = mix(sampled, sampled2, 0.5);
	}
	vec4 outColor = sampled * v_color0;
	if (outColor.a < u_sceneParams.w) discard;
	outColor.rgb = mix(outColor.rgb, u_sceneFogColor.rgb, v_fog);
	gl_FragColor = outColor;
}
