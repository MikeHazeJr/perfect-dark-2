/*
 * Perfect Dark 2: replacement for bgfx's src/debug_renderdoc.cpp.
 *
 * Decision D6 (context/designs/rendering/renderer-2.0-scope.md): our copy of
 * bgfx must never load RenderDoc in release builds. Upstream loads
 * renderdoc.dll by name when bgfx::Init has debug or profile set. This stub
 * keeps the three entry points the renderers call and does nothing, so the
 * binary contains no code path that loads renderdoc.dll at all. Developers who
 * want a capture can attach RenderDoc to the process from the outside.
 */

namespace bgfx
{
	void* loadRenderDoc()
	{
		return nullptr;
	}

	void unloadRenderDoc(void* /*_renderdocdll*/)
	{
	}

	void renderDocTriggerCapture()
	{
	}

} // namespace bgfx
