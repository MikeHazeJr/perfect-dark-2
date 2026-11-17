$input v_texcoord0, v_texcoord1, v_clamp, v_fog, v_gray, v_input1, v_input2, v_input3, v_input4, v_input5, v_input6, v_input7

/*
 * fast3d colour combiner uber-shader for the bgfx backend
 * (port/fast3d/gfx_bgfx.cpp). Compiled ahead of time by pd-shaderc.
 *
 * The classic GL renderer (gfx_opengl.cpp, gfx_opengl_create_and_load_new_
 * shader) writes one GLSL program per combiner state at runtime. This shader
 * computes the same result for every combiner state, driven by uniforms, and
 * mirrors the generated text line by line so the GL driver compiles the same
 * arithmetic:
 *
 *   - Each combiner operand is selected by its gfx_cc.h item code. Selection
 *     only moves values; it does no arithmetic.
 *   - The generated text inlines the constant operands (vec3(0.0), 1.0) and
 *     the compiler folds them and decides which multiply and add to fuse,
 *     including the first add of the WRAP that follows each cycle. So each
 *     formula is written here once per "shape" with the same literal
 *     constants, and the WRAP's first add sits inside the same expression.
 *     The backend derives the shape from the operands (gfx_bgfx_cc_shape):
 *
 *       0  D                             (do_single)
 *       1  A * C                         (do_multiply)
 *       2  mix(0, A, C)    3  mix(1, A, C)    4  mix(B, A, C)   (do_mix)
 *       5  mix(0, A, 1)    6  mix(1, A, 1)    7  mix(B, A, 1)
 *       8  (A - B) * C + 0                    (general, D = SHADER_0)
 *       9  (A - B) * C + 1                    (general, D = SHADER_1)
 *      10  (A - B) * 1 + 1                    (general, C = D = SHADER_1)
 *      11  (A - B) * C + D                    (general)
 *
 *   - Colour and alpha are computed as a vec3 and a float. Every legacy form
 *     (vec4 when colour and alpha match, vec4(rgb, a), vec3 without alpha)
 *     is the same per-component arithmetic.
 *
 * u_ccSel[0..3]  operand items a, b, c, d for cycle 0 rgb, cycle 0 alpha,
 *                cycle 1 rgb, cycle 1 alpha
 * u_ccShape      shapes in the same order
 * u_ccFlags.x    SHADER_OPT_* bits 0..13 of shader_id1, plus
 *                bit 14 texture 0 used, bit 15 texture 1 used,
 *                bit 16 three-point filtering mode (FILTER_THREE_POINT)
 * u_ccFrame      frame_count, noise_scale, three_point_filter0, three_point_filter1
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);

uniform vec4 u_ccSel[4];
uniform vec4 u_ccShape;
uniform vec4 u_ccFlags;
uniform vec4 u_ccFrame;

#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)

#define TEX_OFFSET(tex, uv, texSize, off) texture2D(tex, uv - (off)/texSize)

float random(in vec3 value) {
    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));
    return fract(sin(random) * 143758.5453);
}

vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
    vec2 offset = fract(texCoord*texSize - vec2(0.5));
    offset -= step(1.0, offset.x + offset.y);
    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);
    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));
    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));
    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
}

// hookTexture2D without and with three-point filtering, as generated when the
// combiner has no blur option.
vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {
    return texture2D(tex, uv);
}

vec4 hookTexture2D3(in sampler2D tex, in vec2 uv, in vec2 texSize, in float three_point_filter) {
    return mix(texture2D(tex, uv), filter3point(tex, uv, texSize), three_point_filter);
}

// hookTexture2D for the blur option (menu backgrounds), without and with
// three-point filtering.
vec4 hookTexture2DBlur(in sampler2D t, in vec2 uv, in vec2 texSize) {
    vec4 cw = vec4(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 xy = vec2(float(i & 3), float(i >> 2));
        float w = 0.009947 - length(xy) * 0.001;
        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;
        vec4 tex = texture2D(t, scaled_uv);
        cw += vec4(tex.rgb * w, w);
    }
    return vec4(cw.rgb / cw.a, 1.0);
}

vec4 hookTexture2DBlur3(in sampler2D t, in vec2 uv, in vec2 texSize, in float three_point_filter) {
    vec4 cw = vec4(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 xy = vec2(float(i & 3), float(i >> 2));
        float w = 0.009947 - length(xy) * 0.001;
        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;
        vec4 tex = mix(texture2D(t, scaled_uv), filter3point(t, scaled_uv, texSize), three_point_filter);
        cw += vec4(tex.rgb * w, w);
    }
    return vec4(cw.rgb / cw.a, 1.0);
}

vec3 ccItemRgb(int item, vec4 texVal0, vec4 texVal1, vec3 combined, float noise)
{
    if (item == 1) return v_input1.rgb;
    if (item == 2) return v_input2.rgb;
    if (item == 3) return v_input3.rgb;
    if (item == 4) return v_input4.rgb;
    if (item == 5) return v_input5.rgb;
    if (item == 6) return v_input6.rgb;
    if (item == 7) return v_input7.rgb;
    if (item == 8) return texVal0.rgb;
    if (item == 9) return vec3(texVal0.a, texVal0.a, texVal0.a);
    if (item == 10) return texVal1.rgb;
    if (item == 11) return vec3(texVal1.a, texVal1.a, texVal1.a);
    if (item == 12) return vec3(1.0, 1.0, 1.0);
    if (item == 13) return combined;
    if (item == 14) return vec3(noise, noise, noise);
    return vec3(0.0, 0.0, 0.0);
}

float ccItemAlpha(int item, vec4 texVal0, vec4 texVal1, float combined, float noise)
{
    if (item == 1) return v_input1.a;
    if (item == 2) return v_input2.a;
    if (item == 3) return v_input3.a;
    if (item == 4) return v_input4.a;
    if (item == 5) return v_input5.a;
    if (item == 6) return v_input6.a;
    if (item == 7) return v_input7.a;
    if (item == 8) return texVal0.a;
    if (item == 9) return texVal0.a;
    if (item == 10) return texVal1.a;
    if (item == 11) return texVal1.a;
    if (item == 12) return 1.0;
    if (item == 13) return combined;
    if (item == 14) return noise;
    return 0.0;
}

// One combiner cycle plus the first add of the WRAP after it. LOW is the
// WRAP's literal low bound: (-1.01) after cycle 0, (-0.51) after cycle 1.
#define CC_FORMULA(TYPE, ZERO, ONE, LOW)                                       \
    TYPE r;                                                                    \
    if (shape == 0) r = (d)-(LOW);                                             \
    else if (shape == 1) r = (a * c)-(LOW);                                    \
    else if (shape == 2) r = (mix(ZERO, a, c))-(LOW);                          \
    else if (shape == 3) r = (mix(ONE, a, c))-(LOW);                           \
    else if (shape == 4) r = (mix(b, a, c))-(LOW);                             \
    else if (shape == 5) r = (mix(ZERO, a, ONE))-(LOW);                        \
    else if (shape == 6) r = (mix(ONE, a, ONE))-(LOW);                         \
    else if (shape == 7) r = (mix(b, a, ONE))-(LOW);                           \
    else if (shape == 8) r = ((a - b) * c + ZERO)-(LOW);                       \
    else if (shape == 9) r = ((a - b) * c + ONE)-(LOW);                        \
    else if (shape == 10) r = ((a - b) * ONE + ONE)-(LOW);                     \
    else r = ((a - b) * c + d)-(LOW);                                          \
    return r

vec3 ccCycle0Rgb(int shape, vec3 a, vec3 b, vec3 c, vec3 d)
{
    CC_FORMULA(vec3, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), -1.01);
}

float ccCycle0Alpha(int shape, float a, float b, float c, float d)
{
    CC_FORMULA(float, 0.0, 1.0, -1.01);
}

vec3 ccCycle1Rgb(int shape, vec3 a, vec3 b, vec3 c, vec3 d)
{
    CC_FORMULA(vec3, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), -0.51);
}

float ccCycle1Alpha(int shape, float a, float b, float c, float d)
{
    CC_FORMULA(float, 0.0, 1.0, -0.51);
}

void main()
{
    int flags = int(u_ccFlags.x);
    bool optAlpha = (flags & 1) != 0;
    bool optFog = (flags & 2) != 0;
    bool optTextureEdge = (flags & 4) != 0;
    bool optNoise = (flags & 8) != 0;
    bool opt2Cyc = (flags & 16) != 0;
    bool optAlphaThreshold = (flags & 32) != 0;
    bool optInvisible = (flags & 64) != 0;
    bool optGrayscale = (flags & 128) != 0;
    bool clampS0 = (flags & 256) != 0;
    bool clampT0 = (flags & 512) != 0;
    bool clampS1 = (flags & 1024) != 0;
    bool clampT1 = (flags & 2048) != 0;
    bool optBlur = (flags & 4096) != 0;
    bool optAlphaFromFog = (flags & 8192) != 0;
    bool usedTex0 = (flags & 16384) != 0;
    bool usedTex1 = (flags & 32768) != 0;
    bool threePoint = (flags & 65536) != 0;

    float frame_count = u_ccFrame.x;
    float noise_scale = u_ccFrame.y;

    vec4 texVal0 = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 texVal1 = vec4(0.0, 0.0, 0.0, 0.0);

    if (usedTex0) {
        vec2 texSize0 = vec2(textureSize(s_tex0, 0));
        vec2 vTexCoordAdj0 = v_texcoord0;
        if (clampS0 && clampT0) {
            vTexCoordAdj0 = clamp(v_texcoord0, 0.5 / texSize0, vec2(v_clamp.x, v_clamp.y));
        } else if (clampS0) {
            vTexCoordAdj0 = vec2(clamp(v_texcoord0.s, 0.5 / texSize0.s, v_clamp.x), v_texcoord0.t);
        } else if (clampT0) {
            vTexCoordAdj0 = vec2(v_texcoord0.s, clamp(v_texcoord0.t, 0.5 / texSize0.t, v_clamp.y));
        }
        if (optBlur) {
            texVal0 = threePoint ? hookTexture2DBlur3(s_tex0, vTexCoordAdj0, texSize0, u_ccFrame.z)
                                 : hookTexture2DBlur(s_tex0, vTexCoordAdj0, texSize0);
        } else {
            texVal0 = threePoint ? hookTexture2D3(s_tex0, vTexCoordAdj0, texSize0, u_ccFrame.z)
                                 : hookTexture2D(s_tex0, vTexCoordAdj0, texSize0);
        }
    }

    if (usedTex1) {
        vec2 texSize1 = vec2(textureSize(s_tex1, 0));
        vec2 vTexCoordAdj1 = v_texcoord1;
        if (clampS1 && clampT1) {
            vTexCoordAdj1 = clamp(v_texcoord1, 0.5 / texSize1, vec2(v_clamp.z, v_clamp.w));
        } else if (clampS1) {
            vTexCoordAdj1 = vec2(clamp(v_texcoord1.s, 0.5 / texSize1.s, v_clamp.z), v_texcoord1.t);
        } else if (clampT1) {
            vTexCoordAdj1 = vec2(v_texcoord1.s, clamp(v_texcoord1.t, 0.5 / texSize1.t, v_clamp.w));
        }
        if (optBlur) {
            texVal1 = threePoint ? hookTexture2DBlur3(s_tex1, vTexCoordAdj1, texSize1, u_ccFrame.w)
                                 : hookTexture2DBlur(s_tex1, vTexCoordAdj1, texSize1);
        } else {
            texVal1 = threePoint ? hookTexture2D3(s_tex1, vTexCoordAdj1, texSize1, u_ccFrame.w)
                                 : hookTexture2D(s_tex1, vTexCoordAdj1, texSize1);
        }
    }

    float noise = ((random(vec3(floor(gl_FragCoord.xy * noise_scale), frame_count)) + 1.0) / 2.0);

    // Cycle 0. COMBINED in cycle 0 reads the uninitialised texel in the
    // generated shaders; it is zero here.
    vec3 texelRgb = vec3(0.0, 0.0, 0.0);
    float texelA = 0.0;
    {
        vec4 sr = u_ccSel[0];
        vec4 sa = u_ccSel[1];
        vec3 wrapped = ccCycle0Rgb(int(u_ccShape.x),
            ccItemRgb(int(sr.x), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.y), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.z), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.w), texVal0, texVal1, texelRgb, noise));
        float wrappedA = ccCycle0Alpha(int(u_ccShape.y),
            ccItemAlpha(int(sa.x), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.y), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.z), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.w), texVal0, texVal1, texelA, noise));
        // The rest of WRAP(texel, -1.01, 1.01).
        texelRgb = mod(wrapped, (1.01)-(-1.01)) + (-1.01);
        texelA = mod(wrappedA, (1.01)-(-1.01)) + (-1.01);
    }

    if (opt2Cyc) {
        vec4 sr = u_ccSel[2];
        vec4 sa = u_ccSel[3];
        vec3 wrapped = ccCycle1Rgb(int(u_ccShape.z),
            ccItemRgb(int(sr.x), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.y), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.z), texVal0, texVal1, texelRgb, noise),
            ccItemRgb(int(sr.w), texVal0, texVal1, texelRgb, noise));
        float wrappedA = ccCycle1Alpha(int(u_ccShape.w),
            ccItemAlpha(int(sa.x), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.y), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.z), texVal0, texVal1, texelA, noise),
            ccItemAlpha(int(sa.w), texVal0, texVal1, texelA, noise));
        // The rest of WRAP(texel, -0.51, 1.51).
        texelRgb = mod(wrapped, (1.51)-(-0.51)) + (-0.51);
        texelA = mod(wrappedA, (1.51)-(-0.51)) + (-0.51);
    } else {
        texelRgb = WRAP(texelRgb, -0.51, 1.51);
        texelA = WRAP(texelA, -0.51, 1.51);
    }

    texelRgb = clamp(texelRgb, 0.0, 1.0);
    texelA = clamp(texelA, 0.0, 1.0);

    if (optFog) {
        texelRgb = mix(texelRgb, v_fog.rgb, v_fog.a);
    }
    if (optAlphaFromFog) {
        texelA = v_fog.a;
    }

    if (optTextureEdge && optAlpha) {
        if (texelA > 0.19) texelA = 1.0; else discard;
    }

    if (optAlpha && optNoise) {
        texelA *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), frame_count)) + texelA, 0.0, 1.0));
    }

    if (optGrayscale) {
        float intensity = (texelRgb.r + texelRgb.g + texelRgb.b) / 3.0;
        vec3 new_texel = v_gray.rgb * intensity;
        texelRgb = mix(texelRgb, new_texel, v_gray.a);
    }

    if (optAlpha) {
        if (optAlphaThreshold) {
            if (texelA < 8.0 / 256.0) discard;
        }
        if (optInvisible) {
            texelA = 0.0;
        }
        gl_FragColor = vec4(texelRgb, texelA);
    } else {
        gl_FragColor = vec4(texelRgb, 1.0);
    }
}
