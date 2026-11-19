$input a_position, a_texcoord0, a_texcoord1, a_color0, a_texcoord2, a_texcoord3
$output v_texcoord0, v_texcoord1, v_color0, v_fog

/*
 * Scenario scene renderer vertex shader for the bgfx backend
 * (port/fast3d/scenario_scene_renderer.cpp). Same arithmetic as its GL
 * shader, line for line: the sky offset, roomHighlight's lighting from the
 * room texels and the stage fog.
 *
 * a_texcoord2    the vertex's sky flag (GL a_Sky)
 * a_texcoord3    the vertex's room, as a float (GL a_Room)
 * u_sceneSky     xyz: the camera's draw offset (GL u_SkyOffset)
 * u_sceneFog     x: fog on, y: fog_mul, z: fog_offset (GL u_Fog),
 *                w: room count (GL u_RoomLightCount)
 */

#include <bgfx_shader.sh>

uniform mat4 u_sceneVP;
uniform vec4 u_sceneSky;
uniform vec4 u_sceneFog;
SAMPLER2D(s_sceneRoomLight, 2);

float pdRoomUnlit() {
	int room = int(floor(a_texcoord3 + 0.5));
	if (room < 0 || room >= int(u_sceneFog.w)) return 0.0;
	return texelFetch(s_sceneRoomLight, ivec2(room, 0), 0).w;
}

vec4 pdRoomLit(vec4 col) {
	int room = int(floor(a_texcoord3 + 0.5));
	if (room < 0 || room >= int(u_sceneFog.w)) return col;
	vec4 l0 = texelFetch(s_sceneRoomLight, ivec2(room, 0), 0);
	vec4 l1 = texelFetch(s_sceneRoomLight, ivec2(room, 1), 0);
	if (l0.w > 0.5) return col;
	vec3 c = floor(col.rgb * 255.0 + 0.5);
	if (l1.w > 0.5) c = vec3(max(c.r, max(c.g, c.b)));
	float m = max(c.r, max(c.g, c.b));
	float mult = m > l0.x ? l0.x / m : 1.0;
	c = floor(c * mult);
	m = floor(m * mult);
	float extra = l1.w > 0.5 ? 0.0 : min(l0.y, 285.0 - m);
	if (c.r + c.g + c.b != 0.0 || l0.z > 0.5) c += vec3(extra);
	c = floor(c * l1.rgb);
	return vec4(clamp(c, 0.0, 255.0) / 255.0, col.a);
}

void main()
{
	vec4 clip = mul(u_sceneVP, vec4(a_position + a_texcoord2 * u_sceneSky.xyz, 1.0));
	gl_Position = clip;
	v_fog = 0.0;
	if (u_sceneFog.x > 0.5 && a_texcoord2 < 0.5 && pdRoomUnlit() < 0.5) {
		float w = abs(clip.w) < 0.001 ? 0.001 : clip.w;
		float winv = 1.0 / w;
		if (winv < 0.0) winv = 32767.0;
		v_fog = clamp(clip.z * winv * u_sceneFog.y + u_sceneFog.z, 0.0, 255.0) / 255.0;
	}
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
	v_color0 = pdRoomLit(a_color0);
}
