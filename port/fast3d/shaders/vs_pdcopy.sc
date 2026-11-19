$input a_position

/*
 * Framebuffer copy for the bgfx backend: a full-target quad. fs_pdcopy.sc
 * fetches whole texels, so the copy is exact (see gfx_bgfx_copy_rect).
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = a_position;
}
