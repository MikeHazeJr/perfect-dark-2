/*
 * gfx_tee.cpp - renderer parity mode (--renderer-parity).
 *
 * A fast3d rendering backend that forwards every call to the classic GL
 * backend (gfx_opengl.cpp, which presents to the window as usual) and to the
 * bgfx backend (gfx_bgfx.cpp, which renders the same commands offscreen on
 * its own GL context). Each frame is therefore drawn once through each
 * backend from the same command stream, and a smoke "screenshot" event
 * compares the two finished images pixel for pixel (gfx_parity.cpp).
 *
 * Ids: texture ids and shader programs handed to fast3d are the GL backend's,
 * with the matching bgfx object looked up here. Framebuffer ids are equal in
 * both backends by construction (both number them 0, 1, 2, ...).
 */

#include <stdint.h>
#include <stdio.h>

#include <map>
#include <unordered_map>
#include <utility>

#include "gfx_rendering_api.h"
#include "gfx_opengl.h"
#include "gfx_bgfx.h"
#include "gfx_tee.h"
#include "system.h"

namespace {

struct TeeProgram {
    struct ShaderProgram *gl;
    struct ShaderProgram *bgfx;
};

std::map<std::pair<uint64_t, uint32_t>, TeeProgram> s_programs;
std::unordered_map<uint32_t, uint32_t> s_textures; /* GL id -> bgfx id */

GfxRenderingAPI &G = gfx_opengl_api;
GfxRenderingAPI &B = gfx_bgfx_api;

uint32_t bgfxTex(uint32_t glId)
{
    auto it = s_textures.find(glId);
    return it == s_textures.end() ? 0 : it->second;
}

TeeProgram *prog(struct ShaderProgram *p)
{
    return reinterpret_cast<TeeProgram *>(p);
}

} // namespace

static const char *tee_get_name(void) { return "OpenGL + bgfx parity"; }
static int tee_get_max_texture_size(void) { return G.get_max_texture_size(); }
static struct GfxClipParameters tee_get_clip_parameters(void) { return G.get_clip_parameters(); }

static void tee_unload_shader(struct ShaderProgram *old_prg)
{
    G.unload_shader(old_prg ? prog(old_prg)->gl : nullptr);
    B.unload_shader(old_prg ? prog(old_prg)->bgfx : nullptr);
}

static void tee_load_shader(struct ShaderProgram *new_prg)
{
    G.load_shader(prog(new_prg)->gl);
    B.load_shader(prog(new_prg)->bgfx);
}

static struct ShaderProgram *tee_create_and_load_new_shader(uint64_t id0, uint32_t id1)
{
    TeeProgram &p = s_programs[std::make_pair(id0, id1)];
    p.gl = G.create_and_load_new_shader(id0, id1);
    p.bgfx = B.create_and_load_new_shader(id0, id1);
    return reinterpret_cast<struct ShaderProgram *>(&p);
}

static struct ShaderProgram *tee_lookup_shader(uint64_t id0, uint32_t id1)
{
    auto it = s_programs.find(std::make_pair(id0, id1));
    return it == s_programs.end() ? nullptr : reinterpret_cast<struct ShaderProgram *>(&it->second);
}

static void tee_shader_get_info(struct ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2])
{
    G.shader_get_info(prog(p)->gl, num_inputs, used_textures);
}

static void tee_clear_shaders(void)
{
    G.clear_shaders();
    B.clear_shaders();
    s_programs.clear();
}

static uint32_t tee_new_texture(void)
{
    const uint32_t gl = G.new_texture();
    s_textures[gl] = B.new_texture();
    return gl;
}

static void tee_select_texture(int tile, uint32_t id, bool linear)
{
    G.select_texture(tile, id, linear);
    /* A GL name that is not one of fast3d's textures (the skin editor's
     * canvas override, gfx_pc.cpp) is passed on tagged, and bgfx imports it. */
    uint32_t b = bgfxTex(id);
    if (b == 0 && id != 0) {
        b = (id << 3) | GFX_BGFX_TEXID_KIND_GL_NAME;
    }
    B.select_texture(tile, b, linear);
}

static void tee_upload_texture(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    G.upload_texture(rgba, w, h);
    B.upload_texture(rgba, w, h);
}

static void tee_set_sampler_parameters(int tile, bool linear, uint32_t cms, uint32_t cmt)
{
    G.set_sampler_parameters(tile, linear, cms, cmt);
    B.set_sampler_parameters(tile, linear, cms, cmt);
}

static void tee_set_depth_mode(bool test, bool update, bool compare, bool prim, uint16_t zmode)
{
    G.set_depth_mode(test, update, compare, prim, zmode);
    B.set_depth_mode(test, update, compare, prim, zmode);
}

static void tee_set_depth_range(float n, float f)
{
    G.set_depth_range(n, f);
    B.set_depth_range(n, f);
}

static void tee_set_viewport(int x, int y, int w, int h)
{
    G.set_viewport(x, y, w, h);
    B.set_viewport(x, y, w, h);
}

static void tee_set_scissor(int x, int y, int w, int h)
{
    G.set_scissor(x, y, w, h);
    B.set_scissor(x, y, w, h);
}

static void tee_set_use_alpha(bool use_alpha, bool modulate, bool additive)
{
    G.set_use_alpha(use_alpha, modulate, additive);
    B.set_use_alpha(use_alpha, modulate, additive);
}

static void tee_draw_triangles(float buf[], size_t len, size_t tris)
{
    G.draw_triangles(buf, len, tris);
    B.draw_triangles(buf, len, tris);
}

static void tee_init(void)
{
    G.init();
    gfxBgfxSetTeeMode(1);
    B.init();
}

static void tee_on_resize(void) { G.on_resize(); B.on_resize(); }
static void tee_start_frame(void) { G.start_frame(); B.start_frame(); }
static void tee_end_frame(void) { G.end_frame(); B.end_frame(); }
static void tee_finish_render(void) { G.finish_render(); B.finish_render(); }

static int tee_create_framebuffer(void)
{
    const int gl = G.create_framebuffer();
    const int bg = B.create_framebuffer();
    if (gl != bg) {
        sysFatalError("renderer parity: framebuffer ids diverged (GL %d, bgfx %d)", gl, bg);
    }
    return gl;
}

static void tee_update_framebuffer_parameters(int fb, uint32_t w, uint32_t h, uint32_t msaa, bool invert_y,
                                              bool render_target, bool has_depth, bool can_extract_depth)
{
    G.update_framebuffer_parameters(fb, w, h, msaa, invert_y, render_target, has_depth, can_extract_depth);
    B.update_framebuffer_parameters(fb, w, h, msaa, invert_y, render_target, has_depth, can_extract_depth);
}

static bool tee_start_draw_to_framebuffer(int fb, float noise_scale)
{
    const bool gl = G.start_draw_to_framebuffer(fb, noise_scale);
    B.start_draw_to_framebuffer(fb, noise_scale);
    return gl;
}

static void tee_copy_framebuffer(int dst, int src, int left, int top, bool flip_y, bool use_back)
{
    G.copy_framebuffer(dst, src, left, top, flip_y, use_back);
    B.copy_framebuffer(dst, src, left, top, flip_y, use_back);
}

static void tee_clear_framebuffer(bool color, bool depth)
{
    G.clear_framebuffer(color, depth);
    B.clear_framebuffer(color, depth);
}

static void tee_resolve_msaa_color_buffer(int target, int source)
{
    G.resolve_msaa_color_buffer(target, source);
    B.resolve_msaa_color_buffer(target, source);
}

static void *tee_get_framebuffer_texture_id(int fb)
{
    void *gl = G.get_framebuffer_texture_id(fb);
    /* ImGui draws with the GL name; the bgfx side maps it back to its own
     * copy of the same framebuffer. */
    gfxBgfxTeeNoteFramebufferTexture((uint32_t)(uintptr_t)gl, fb);
    return gl;
}

static void tee_select_texture_fb(int fb)
{
    G.select_texture_fb(fb);
    B.select_texture_fb(fb);
}

static void tee_delete_texture(uint32_t id)
{
    G.delete_texture(id);
    B.delete_texture(bgfxTex(id));
    s_textures.erase(id);
}

static void tee_set_texture_filter(enum FilteringMode mode)
{
    G.set_texture_filter(mode);
    B.set_texture_filter(mode);
}

static enum FilteringMode tee_get_texture_filter(void)
{
    return G.get_texture_filter();
}

struct GfxRenderingAPI gfx_tee_api = {
    tee_get_name,
    tee_get_max_texture_size,
    tee_get_clip_parameters,
    tee_unload_shader,
    tee_load_shader,
    tee_create_and_load_new_shader,
    tee_lookup_shader,
    tee_shader_get_info,
    tee_clear_shaders,
    tee_new_texture,
    tee_select_texture,
    tee_upload_texture,
    tee_set_sampler_parameters,
    tee_set_depth_mode,
    tee_set_depth_range,
    tee_set_viewport,
    tee_set_scissor,
    tee_set_use_alpha,
    tee_draw_triangles,
    tee_init,
    tee_on_resize,
    tee_start_frame,
    tee_end_frame,
    tee_finish_render,
    tee_create_framebuffer,
    tee_update_framebuffer_parameters,
    tee_start_draw_to_framebuffer,
    tee_copy_framebuffer,
    tee_clear_framebuffer,
    tee_resolve_msaa_color_buffer,
    tee_get_framebuffer_texture_id,
    tee_select_texture_fb,
    tee_delete_texture,
    tee_set_texture_filter,
    tee_get_texture_filter,
};

extern "C" int gfxTeeRequested(void)
{
    return sysArgCheck("--renderer-parity") ? 1 : 0;
}
