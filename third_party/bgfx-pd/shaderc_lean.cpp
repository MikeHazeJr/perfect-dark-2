/*
 * Perfect Dark 2: pieces of shaderc that upstream keeps inside its optional
 * glslang and glsl-optimizer blocks, which the lean pd-shaderc build leaves
 * out (third_party/bgfx-pd/CMakeLists.txt).
 *
 *   - g_allocator and the TinyStlAllocator functions are defined in
 *     shaderc_spirv.cpp only when glslang is compiled in.
 *   - shaderc_glsl.cpp without glsl-optimizer defines a second
 *     compileSPIRVShader instead of compileGLSLShader, so that file is left
 *     out and compileGLSLShader is provided here.
 *
 * pd-shaderc compiles for GLSL 4.30 (-p 430), which shaderc writes out
 * without the optimizer (shaderc.cpp, glsl_profile > 400), so
 * compileGLSLShader is never reached for our shaders.
 */

#include "shaderc.h"

#include <bx/allocator.h>

namespace bgfx
{
	static bx::DefaultAllocator s_allocator;
	bx::AllocatorI* g_allocator = &s_allocator;

	struct TinyStlAllocator
	{
		static void* static_allocate(size_t _bytes);
		static void static_deallocate(void* _ptr, size_t /*_bytes*/);
	};

	void* TinyStlAllocator::static_allocate(size_t _bytes)
	{
		return bx::alloc(g_allocator, _bytes);
	}

	void TinyStlAllocator::static_deallocate(void* _ptr, size_t /*_bytes*/)
	{
		if (NULL != _ptr)
		{
			bx::free(g_allocator, _ptr);
		}
	}

	bool compileGLSLShader(const Options& _options, uint32_t _version, const std::string& _code, bx::WriterI* _writer, bx::WriterI* _messages)
	{
		BX_UNUSED(_options, _version, _code, _writer);
		bx::Error messageErr;
		bx::write(_messages, &messageErr, "pd-shaderc: GLSL profiles up to 4.00 need glsl-optimizer, which this build leaves out; use -p 430.\n");
		return false;
	}

} // namespace bgfx
