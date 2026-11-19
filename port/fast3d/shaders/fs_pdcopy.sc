/*
 * Framebuffer copy for the bgfx backend (port/fast3d/gfx_bgfx.cpp).
 *
 * Reproduces glBlitFramebuffer(..., GL_NEAREST) from the classic GL renderer:
 * each destination pixel centre maps back into the source rectangle and the
 * texel under it is fetched whole, so unscaled copies (the only kind the game
 * issues) are bit-exact and a negative scale mirrors the image.
 *
 * u_copyMap  x0, y0: source position of destination pixel edge 0,
 *            z, w: source pixels per destination pixel (negative = mirrored)
 * u_copyDst  x, y: destination rectangle origin in GL window coordinates
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_copySrc, 0);

uniform vec4 u_copyMap;
uniform vec4 u_copyDst;

void main()
{
	vec2 dst = gl_FragCoord.xy - u_copyDst.xy;
	vec2 src = u_copyMap.xy + dst * u_copyMap.zw;
	gl_FragColor = texelFetch(s_copySrc, ivec2(floor(src)), 0);
}
