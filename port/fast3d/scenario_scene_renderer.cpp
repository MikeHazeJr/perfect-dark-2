#include "scenario_scene_renderer.h"
#include "scenario_dyntex.h"

#include "../external/imgui-node-editor/crude_json.h"
#include "../external/stb_image.h"
#include "glad/glad.h"
#include "modasset_gltf_document.h"
#include "scenario_scene_renderer_bgfx.h"
#include "modasset_gltf_scene.h"
#include "modasset_gltf_source.h"
#include "sha256.h"

extern "C" {
#include "fs.h"
#include "system.h"
}

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace {

struct BufferView {
	uint32_t offset = 0;
	uint32_t length = 0;
	uint32_t stride = 0;
};

struct Accessor {
	int view = -1;
	uint32_t offset = 0;
	int component_type = 0;
	uint32_t count = 0;
	std::string type;
};

/* c3844 glass fix: glTF alphaMode, mirrored from the extract-side
 * PDSCENARIO_ALPHA_* enum. Drives the per-material pass choice in the render loop:
 * OPAQUE -> opaque pass, no discard; MASK -> opaque pass + alpha-test cutout;
 * BLEND -> translucent blend pass. Replaces the texture-alpha-only uses_alpha
 * heuristic so glass blends, grates cut out crisply, and solids stay opaque. */
enum MaterialAlphaMode {
	ALPHA_MODE_OPAQUE = 0,
	ALPHA_MODE_MASK   = 1,
	ALPHA_MODE_BLEND  = 2,
};

struct Material {
	int image = -1;
	int secondary_image = -1;
	int primary_texcoord = 0;
	int wrap_s = GL_REPEAT;
	int wrap_t = GL_REPEAT;
	int secondary_wrap_s = GL_REPEAT;
	int secondary_wrap_t = GL_REPEAT;
	int secondary_texcoord = 1;
	bool has_secondary = false;
	bool uses_alpha = false;
	int alpha_mode = ALPHA_MODE_OPAQUE;
	/* B-945: true when the glTF carried an explicit alphaMode key. Explicit
	 * classification (including explicit OPAQUE from the fixed extractor) is
	 * authoritative; the has-alpha->MASK promotion below only applies to
	 * legacy scenes that predate alphaMode emission. */
	bool alpha_mode_explicit = false;
	float alpha_cutoff = 0.01f;
	/* Animated surfaces: the dyntex.c preset this texture moves by
	 * (pd2_material.animation) and the factors that turn N64 texture
	 * coordinates into its runtime UVs (pd2_material.uv_scale). */
	int animation = SCENARIO_DYNTEX_NONE;
	float anim_u_scale = 0.0f;
	float anim_v_scale = 0.0f;
	/* B-1267: pd2_material.cull is "back": the game drew this surface
	 * with back faces culled, and so does the scene. Absent (a scene
	 * written before the field, or an authored level) or "none": both
	 * faces are drawn, as every surface was before. */
	bool cull_back = false;
	/* B-1268: pd2_material.texgen: a reflective room surface, lit and
	 * texture-generated (G_LIGHTING | G_TEXTURE_GEN). Its vertex colours
	 * are normals; the shader lights it and makes its UVs from them with
	 * the G_TEXTURE scale (texgen_scale) and the runtime UV scale
	 * (texgen_uv_scale, here per N64 texel step: the scale / 32). */
	bool texgen = false;
	float texgen_scale_s = 0.0f;
	float texgen_scale_t = 0.0f;
	float texgen_u_step = 0.0f;
	float texgen_v_step = 0.0f;
};

struct Image {
	const uint8_t *bytes = nullptr;
	uint32_t size = 0;
	std::string name;
	int width = 0;
	int height = 0;
	unsigned char *rgba = nullptr;
	GLuint gl = 0;
	uint32_t bgfx_tex = 0; /* --renderer bgfx (scenario_scene_renderer_bgfx.cpp) */
	bool has_nonopaque_alpha = false;
};

struct Vertex {
	float x;
	float y;
	float z;
	float u0;
	float v0;
	float u1;
	float v1;
	float r;
	float g;
	float b;
	float a;
	/* B-1172: 1 for a vertex of a sky room (rooms.json sky): the game draws
	 * those rooms relative to the camera's room (roomPopulateMtx), so the
	 * shader moves them by the camera's draw offset. */
	float sky;
	/* The vertex's room (the scene's _PD_ROOM attribute), -1 when it names none. */
	int32_t room;
};

/* An animated vertex: its N64 texture coordinates (after the group shift)
 * and texture scales from _PD_DYNTEX, its preset, material and room. */
struct DyntexVertex {
	uint32_t vertex = 0;
	int32_t base_s = 0;
	int32_t base_t = 0;
	uint16_t scale_s = 0xffff;
	uint16_t scale_t = 0xffff;
	int kind = SCENARIO_DYNTEX_NONE;
	int material = -1;
	int32_t room = -1;
};

struct DrawGroup {
	int material = -1;
	uint32_t first = 0;
	uint32_t count = 0;
};

struct Scene {
	std::string scenario_id;
	std::string source_path;
	std::vector<Vertex> vertices;
	std::vector<DrawGroup> groups;
	std::vector<Material> materials;
	std::vector<Image> images;
	size_t instances = 0;
	int selected_scene = -1;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint shader = 0;
	GLuint white_tex = 0;
	/* --renderer bgfx resources (scenario_scene_renderer_bgfx.cpp). */
	uint32_t bgfx_vb = 0;
	uint32_t bgfx_white = 0;
	bool bgfx_ready = false;
	bool gpu_ready = false;
	/* B-1172: the sky flags changed after the vertex upload. */
	bool sky_dirty = false;
	size_t sky_vertex_count = 0;
	/* B-1172: the room lighting of each camera slot (and of the last camera),
	 * two texel rows per room: brightness, flash, flash_black, unlit; tint
	 * r, g, b, greyscale. */
	std::vector<float> slot_room_lights[8];
	std::vector<float> room_lights;
	GLuint room_light_tex = 0;
	int room_light_tex_width = 0;
	bool logged_room_lights = false;
	/* B-1172: the stage fog of the next draw (scenarioSceneRendererSetFog). */
	int fog_enabled = 0;
	float fog_mul = 0.0f;
	float fog_offset = 0.0f;
	float fog_color[3] = {};
	bool logged_fog = false;
	bool active = false;
	bool logged_render = false;
	bool logged_camera_ready = false;
	bool logged_camera_invalid = false;
	bool logged_missing_camera = false;
	bool logged_waiting_camera = false;
	bool logged_transform = false;
	bool logged_gpu_unavailable = false;
	bool shader_failed = false;
	bool has_camera = false;
	bool camera_ever_valid = false;
	size_t alpha_texture_count = 0;
	size_t alpha_material_count = 0;
	size_t secondary_material_count = 0;
	size_t opaque_material_count = 0;
	size_t mask_material_count = 0;
	size_t blend_material_count = 0;
	float view_projection[4][4] = {};
	/* B-1172: the camera of each scene draw queued in this frame's display
	 * list (one per player view), so each draw uses the camera of the player
	 * that queued it, and the last camera set (backdrop draws). */
	scenario_scene_camera_t camera_slots[8] = {};
	scenario_scene_camera_t camera = {};
	int camera_slot_count = 0;
	int last_camera_slot = -1;
	/* Animated surfaces (dyntex.c presets): the vertices whose runtime UVs
	 * move, and the frame state the game sets before each draw. */
	std::vector<DyntexVertex> dyntex_vertices;
	size_t dyntex_groups = 0;
	size_t dyntex_kind_counts[SCENARIO_DYNTEX_KIND_COUNT] = {};
	size_t dyntex_unusable_primitives = 0;
	scenario_dyntex_frame_t dyntex_frame = {};
	bool dyntex_frame_dirty = false;
	bool logged_dyntex_update = false;
};

Scene g_scene;

static uint32_t readLe32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float readLeFloat(const uint8_t *p)
{
	uint32_t u = readLe32(p);
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

static const crude_json::value *member(const crude_json::value &v,
	const char *key)
{
	if (!v.is_object() || !v.contains(key)) {
		return nullptr;
	}
	return &v[key];
}

static const crude_json::array *arrayMember(const crude_json::value &v,
	const char *key)
{
	const crude_json::value *m = member(v, key);
	return m && m->is_array() ? m->get_ptr<crude_json::array>() : nullptr;
}

static const crude_json::object *objectMember(const crude_json::value &v,
	const char *key)
{
	const crude_json::value *m = member(v, key);
	return m && m->is_object() ? m->get_ptr<crude_json::object>() : nullptr;
}

static int intMember(const crude_json::value &v, const char *key,
	int fallback)
{
	const crude_json::value *m = member(v, key);
	return m && m->is_number() ? (int)m->get<crude_json::number>() : fallback;
}

static int glWrap(int value);

static const crude_json::object *objectField(const crude_json::object &obj,
	const char *key)
{
	auto it = obj.find(key);
	return it != obj.end() && it->second.is_object()
		? it->second.get_ptr<crude_json::object>() : nullptr;
}

static int intField(const crude_json::object &obj, const char *key,
	int fallback)
{
	auto it = obj.find(key);
	return it != obj.end() && it->second.is_number()
		? (int)it->second.get<crude_json::number>() : fallback;
}

static float floatField(const crude_json::object &obj, const char *key,
	float fallback)
{
	auto it = obj.find(key);
	return it != obj.end() && it->second.is_number()
		? (float)it->second.get<crude_json::number>() : fallback;
}

/* Two finite numbers under key, else false and out untouched. */
static bool numberPairField(const crude_json::object &obj, const char *key,
	float out[2])
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->second.is_array()) {
		return false;
	}
	const crude_json::array *pair = it->second.get_ptr<crude_json::array>();
	if (!pair || pair->size() != 2 || !(*pair)[0].is_number()
			|| !(*pair)[1].is_number()) {
		return false;
	}
	const float a = (float)(*pair)[0].get<crude_json::number>();
	const float b = (float)(*pair)[1].get<crude_json::number>();
	if (!std::isfinite(a) || !std::isfinite(b)) {
		return false;
	}
	out[0] = a;
	out[1] = b;
	return true;
}

static std::string stringField(const crude_json::object &obj, const char *key)
{
	auto it = obj.find(key);
	return it != obj.end() && it->second.is_string()
		? it->second.get<crude_json::string>() : std::string();
}

static bool probeLoggingEnabled()
{
	static int enabled = -1;
	if (enabled < 0) {
		enabled = sysArgCheck("--debug-scenario-render-probe") ? 1 : 0;
	}
	return enabled != 0;
}

static bool readTextureBinding(const crude_json::array *textures,
	const crude_json::array *samplers, int tex_index, int &image,
	int &wrap_s, int &wrap_t)
{
	if (!textures || tex_index < 0 || (size_t)tex_index >= textures->size()) {
		return false;
	}

	const crude_json::value &tex = (*textures)[(size_t)tex_index];
	const crude_json::object *tex_obj = tex.get_ptr<crude_json::object>();
	if (!tex_obj) {
		return false;
	}
	image = intField(*tex_obj, "source", -1);
	int sampler = intField(*tex_obj, "sampler", -1);
	if (samplers && sampler >= 0 && (size_t)sampler < samplers->size()) {
		const crude_json::value &s = (*samplers)[(size_t)sampler];
		const crude_json::object *sampler_obj =
			s.get_ptr<crude_json::object>();
		if (sampler_obj) {
			wrap_s = glWrap(intField(*sampler_obj, "wrapS", 10497));
			wrap_t = glWrap(intField(*sampler_obj, "wrapT", 10497));
		}
	}
	return image >= 0;
}

static std::string stringMember(const crude_json::value &v, const char *key)
{
	const crude_json::value *m = member(v, key);
	return m && m->is_string() ? m->get<crude_json::string>() : std::string();
}

static uint32_t componentSize(int component_type)
{
	switch (component_type) {
	case 5121: return 1;
	case 5123: return 2;
	case 5125: return 4;
	case 5126: return 4;
	default: return 0;
	}
}

static uint32_t typeComponentCount(const std::string &type)
{
	if (type == "SCALAR") return 1;
	if (type == "VEC2") return 2;
	if (type == "VEC3") return 3;
	if (type == "VEC4") return 4;
	return 0;
}

static const uint8_t *accessorData(const Accessor &a,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, uint32_t *out_stride)
{
	if (a.view < 0 || (size_t)a.view >= views.size()) {
		return nullptr;
	}
	const BufferView &view = views[(size_t)a.view];
	uint32_t elem = componentSize(a.component_type)
		* typeComponentCount(a.type);
	uint32_t stride = view.stride ? view.stride : elem;
	uint64_t start = (uint64_t)view.offset + a.offset;
	if (!modAssetGltfAccessorBounds(bin_size, view.offset, view.length,
			a.offset, a.count, stride, elem)) {
		return nullptr;
	}
	if (out_stride) {
		*out_stride = stride;
	}
	return bin + start;
}

static bool readFloatVec2(const Accessor &a,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, uint32_t index, float *x, float *y)
{
	uint32_t stride = 0;
	const uint8_t *data = accessorData(a, views, bin, bin_size, &stride);
	if (!data || a.component_type != 5126 || a.type != "VEC2"
			|| index >= a.count) {
		return false;
	}
	const uint8_t *p = data + stride * index;
	*x = readLeFloat(p);
	*y = readLeFloat(p + 4);
	return true;
}

static bool readFloatVec3(const Accessor &a,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, uint32_t index, float *x, float *y, float *z)
{
	uint32_t stride = 0;
	const uint8_t *data = accessorData(a, views, bin, bin_size, &stride);
	if (!data || a.component_type != 5126 || a.type != "VEC3"
			|| index >= a.count) {
		return false;
	}
	const uint8_t *p = data + stride * index;
	*x = readLeFloat(p);
	*y = readLeFloat(p + 4);
	*z = readLeFloat(p + 8);
	return true;
}

static bool readFloatVec4(const Accessor &a,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, uint32_t index, float *x, float *y, float *z, float *w)
{
	uint32_t stride = 0;
	const uint8_t *data = accessorData(a, views, bin, bin_size, &stride);
	if (!data || a.component_type != 5126 || a.type != "VEC4"
			|| index >= a.count) {
		return false;
	}
	const uint8_t *p = data + stride * index;
	*x = readLeFloat(p);
	*y = readLeFloat(p + 4);
	*z = readLeFloat(p + 8);
	*w = readLeFloat(p + 12);
	return true;
}

static bool readIndexValue(const Accessor &a,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, uint32_t index, uint32_t *out)
{
	uint32_t stride = 0;
	const uint8_t *data = accessorData(a, views, bin, bin_size, &stride);
	if (!data || a.type != "SCALAR" || index >= a.count) {
		return false;
	}
	const uint8_t *p = data + stride * index;
	switch (a.component_type) {
	case 5121:
		*out = p[0];
		return true;
	case 5123:
		*out = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
		return true;
	case 5125:
		*out = readLe32(p);
		return true;
	default:
		return false;
	}
}

static bool parseViews(const crude_json::value &root,
	std::vector<BufferView> &views)
{
	const crude_json::array *arr = arrayMember(root, "bufferViews");
	if (!arr) {
		return false;
	}
	for (const auto &v : *arr) {
		BufferView view;
		view.offset = (uint32_t)intMember(v, "byteOffset", 0);
		view.length = (uint32_t)intMember(v, "byteLength", 0);
		view.stride = (uint32_t)intMember(v, "byteStride", 0);
		views.push_back(view);
	}
	return !views.empty();
}

static bool parseAccessors(const crude_json::value &root,
	std::vector<Accessor> &accessors)
{
	const crude_json::array *arr = arrayMember(root, "accessors");
	if (!arr) {
		return false;
	}
	for (const auto &v : *arr) {
		Accessor a;
		a.view = intMember(v, "bufferView", -1);
		a.offset = (uint32_t)intMember(v, "byteOffset", 0);
		a.component_type = intMember(v, "componentType", 0);
		a.count = (uint32_t)intMember(v, "count", 0);
		a.type = stringMember(v, "type");
		accessors.push_back(a);
	}
	return !accessors.empty();
}

static int glWrap(int value)
{
	switch (value) {
	case 33071: return GL_CLAMP_TO_EDGE;
	case 33648: return GL_MIRRORED_REPEAT;
	case 10497:
	default:
		return GL_REPEAT;
	}
}

static bool imageHasNonOpaqueAlpha(const unsigned char *rgba, int width,
	int height)
{
	if (!rgba || width <= 0 || height <= 0) {
		return false;
	}
	const size_t pixels = (size_t)width * (size_t)height;
	for (size_t i = 0; i < pixels; i++) {
		if (rgba[i * 4u + 3u] < 255u) {
			return true;
		}
	}
	return false;
}

static void parseMaterials(const crude_json::value &root,
	std::vector<Material> &materials)
{
	const crude_json::array *samplers = arrayMember(root, "samplers");
	const crude_json::array *textures = arrayMember(root, "textures");
	const crude_json::array *mats = arrayMember(root, "materials");
	if (!mats) {
		return;
	}

	size_t mat_index = 0;
	for (const auto &m : *mats) {
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parse material index=%zu",
				mat_index);
		}
		Material mat;
		const crude_json::object *mat_obj =
			m.get_ptr<crude_json::object>();
		const crude_json::object *pbr = mat_obj
			? objectField(*mat_obj, "pbrMetallicRoughness") : nullptr;
		if (pbr) {
			auto it = pbr->find("baseColorTexture");
			if (it != pbr->end() && it->second.is_object()) {
				const crude_json::object *base =
					it->second.get_ptr<crude_json::object>();
				int tex_index = base ? intField(*base, "index", -1) : -1;
				mat.primary_texcoord = base ? intField(*base, "texCoord", 0) : 0;
				readTextureBinding(textures, samplers, tex_index, mat.image,
					mat.wrap_s, mat.wrap_t);
			}
		}
		const crude_json::object *extras = mat_obj
			? objectField(*mat_obj, "extras") : nullptr;
		const crude_json::object *pd2 =
			extras ? objectField(*extras, "pd2_material") : nullptr;
		if (pd2) {
			mat.primary_texcoord = 1;
			mat.cull_back = stringField(*pd2, "cull") == "back";
		}
		if (pd2) {
			auto anim_it = pd2->find("animation");
			if (anim_it != pd2->end()) {
				int kind = anim_it->second.is_string()
					? scenarioDyntexKindFromName(
						anim_it->second.get<crude_json::string>().c_str())
					: -1;
				float us = 0.0f;
				float vs = 0.0f;
				bool scale_ok = false;
				auto scale_it = pd2->find("uv_scale");
				if (scale_it != pd2->end() && scale_it->second.is_array()) {
					const crude_json::array *uv =
						scale_it->second.get_ptr<crude_json::array>();
					if (uv && uv->size() == 2 && (*uv)[0].is_number()
							&& (*uv)[1].is_number()) {
						us = (float)(*uv)[0].get<crude_json::number>();
						vs = (float)(*uv)[1].get<crude_json::number>();
						scale_ok = std::isfinite(us) && std::isfinite(vs);
					}
				}
				if (kind > SCENARIO_DYNTEX_NONE && scale_ok) {
					mat.animation = kind;
					mat.anim_u_scale = us;
					mat.anim_v_scale = vs;
				} else {
					sysLogPrintf(LOG_WARNING,
						"SCENARIO.RENDER: material %zu has an animation the renderer cannot use; it stays still",
						materials.size());
				}
			}
		}
		if (pd2) {
			/* B-1268: a reflective room surface. */
			auto texgen_it = pd2->find("texgen");
			if (texgen_it != pd2->end()) {
				float scale[2] = {0.0f, 0.0f};
				float uv[2] = {0.0f, 0.0f};
				if (texgen_it->second.is_boolean()
						&& texgen_it->second.get<crude_json::boolean>()
						&& numberPairField(*pd2, "texgen_scale", scale)
						&& numberPairField(*pd2, "texgen_uv_scale", uv)
						&& scale[0] > 0.0f && scale[1] > 0.0f) {
					mat.texgen = true;
					mat.texgen_scale_s = scale[0];
					mat.texgen_scale_t = scale[1];
					mat.texgen_u_step = uv[0] / 32.0f;
					mat.texgen_v_step = uv[1] / 32.0f;
				} else {
					sysLogPrintf(LOG_WARNING,
						"SCENARIO.RENDER: material %zu says texgen without a usable texgen_scale and texgen_uv_scale; it is drawn unlit",
						materials.size());
				}
			}
		}
		const crude_json::object *secondary =
			pd2 ? objectField(*pd2, "secondaryTexture") : nullptr;
		if (secondary) {
			int secondary_index = intField(*secondary, "index", -1);
			mat.secondary_texcoord = intField(*secondary, "texCoord", 1);
			if (readTextureBinding(textures, samplers, secondary_index,
					mat.secondary_image, mat.secondary_wrap_s,
					mat.secondary_wrap_t)) {
				mat.has_secondary = true;
			}
		}
		/* c3844 glass fix: parse glTF alphaMode (default OPAQUE) so the render loop
		 * can pick the opaque vs blend pass per material. BLEND -> translucent pass;
		 * MASK -> opaque pass with alpha-test cutout at alphaCutoff; OPAQUE -> solid. */
		if (mat_obj) {
			std::string mode = stringField(*mat_obj, "alphaMode");
			if (mode == "BLEND") {
				mat.alpha_mode = ALPHA_MODE_BLEND;
				mat.alpha_mode_explicit = true;
			} else if (mode == "MASK") {
				mat.alpha_mode = ALPHA_MODE_MASK;
				mat.alpha_cutoff = floatField(*mat_obj, "alphaCutoff", 0.5f);
				mat.alpha_mode_explicit = true;
			} else if (mode == "OPAQUE") {
				/* B-945: fixed extractor writes OPAQUE explicitly; trust it and
				 * keep the legacy MASK promotion off for this material. */
				mat.alpha_mode = ALPHA_MODE_OPAQUE;
				mat.alpha_mode_explicit = true;
			}
		}
		materials.push_back(mat);
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parsed material index=%zu image=%d secondary=%d",
				mat_index, mat.image, mat.has_secondary ? 1 : 0);
		}
		mat_index++;
	}
}

static bool parseImages(const crude_json::value &root,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, std::vector<Image> &images, size_t decoded_budget = 0)
{
	const bool bounded = decoded_budget != 0;
	const crude_json::array *arr = arrayMember(root, "images");
	if (!arr) {
		return true;
	}
	for (const auto &v : *arr) {
		images.emplace_back();
		Image &img = images.back();
		img.name = stringMember(v, "name");
		int view_index = intMember(v, "bufferView", -1);
		if (view_index >= 0 && (size_t)view_index < views.size()) {
			const BufferView &bv = views[(size_t)view_index];
			if ((uint64_t)bv.offset + bv.length <= bin_size) {
				img.bytes = bin + bv.offset;
				img.size = bv.length;
				if (probeLoggingEnabled()) {
					sysLogPrintf(LOG_NOTE,
						"SCENARIO.RENDER.CPU_PROBE: decode image index=%zu name='%s' bytes=%u",
						images.size(), img.name.c_str(), img.size);
				}
				int comp = 0;
				if (bounded) {
					int width = 0, height = 0;
					if (img.size > INT32_MAX
							|| !stbi_info_from_memory(img.bytes, (int)img.size, &width, &height, &comp)
							|| width <= 0 || height <= 0
							|| (uint64_t)width * (uint64_t)height > decoded_budget / 4) return false;
					decoded_budget -= (size_t)width * (size_t)height * 4;
				}
				img.rgba = stbi_load_from_memory(img.bytes,
					(int)img.size, &img.width, &img.height, &comp, 4);
				if (probeLoggingEnabled()) {
					sysLogPrintf(LOG_NOTE,
						"SCENARIO.RENDER.CPU_PROBE: decoded image index=%zu name='%s' size=%dx%d ok=%d",
						images.size(), img.name.c_str(), img.width, img.height,
						img.rgba ? 1 : 0);
				}
				img.has_nonopaque_alpha =
					imageHasNonOpaqueAlpha(img.rgba, img.width, img.height);
			}
		}
		if (bounded && !img.rgba) return false;
	}
	return true;
}

static void markMaterialAlpha(Scene &scene)
{
	scene.alpha_texture_count = 0;
	scene.alpha_material_count = 0;
	scene.secondary_material_count = 0;
	scene.opaque_material_count = 0;
	scene.mask_material_count = 0;
	scene.blend_material_count = 0;
	for (const Image &img : scene.images) {
		if (img.has_nonopaque_alpha) {
			scene.alpha_texture_count++;
		}
	}
	for (Material &mat : scene.materials) {
		bool primary_alpha = mat.image >= 0
			&& (size_t)mat.image < scene.images.size()
			&& scene.images[(size_t)mat.image].has_nonopaque_alpha;
		bool secondary_alpha = mat.secondary_image >= 0
			&& (size_t)mat.secondary_image < scene.images.size()
			&& scene.images[(size_t)mat.secondary_image].has_nonopaque_alpha;
		bool texture_alpha = primary_alpha || secondary_alpha;
		/* Back-compat only: a material with NO explicit glTF alphaMode whose texture
		 * carries non-opaque alpha is a cutout -> promote to MASK so it gets the
		 * alpha-test discard (covers scenes extracted before alphaMode emission).
		 * B-945: explicit alphaMode (including explicit OPAQUE) is authoritative --
		 * regenerated scenes decode I4/I8 textures with replicated-intensity alpha
		 * for blend parity, and promoting those would punch alpha-test holes into
		 * solid geometry. */
		if (mat.alpha_mode == ALPHA_MODE_OPAQUE && !mat.alpha_mode_explicit
				&& texture_alpha) {
			mat.alpha_mode = ALPHA_MODE_MASK;
		}
		/* uses_alpha kept as a legacy alias = "draws in the translucent blend pass". */
		mat.uses_alpha = (mat.alpha_mode == ALPHA_MODE_BLEND);
		if (mat.has_secondary) {
			scene.secondary_material_count++;
		}
		switch (mat.alpha_mode) {
		case ALPHA_MODE_BLEND:
			scene.blend_material_count++;
			scene.alpha_material_count++;
			break;
		case ALPHA_MODE_MASK:
			scene.mask_material_count++;
			break;
		default:
			scene.opaque_material_count++;
			break;
		}
	}
}

static bool appendPrimitive(const crude_json::value &prim,
	const std::vector<Accessor> &accessors,
	const std::vector<BufferView> &views, const uint8_t *bin,
	uint32_t bin_size, const modasset_gltf_scene_instance_t &instance, Scene &scene,
	size_t vertex_limit)
{
	const crude_json::object *attrs = objectMember(prim, "attributes");
	if (!attrs) {
		return false;
	}

	auto findAttr = [&](const char *name) -> int {
		auto it = attrs->find(name);
		if (it == attrs->end() || !it->second.is_number()) {
			return -1;
		}
		return (int)it->second.get<crude_json::number>();
	};

	int pos_i = findAttr("POSITION");
	int uv0_i = findAttr("TEXCOORD_0");
	int uv1_i = findAttr("TEXCOORD_1");
	int color_i = findAttr("COLOR_0");
	int room_i = findAttr("_PD_ROOM");
	if (pos_i < 0 || (size_t)pos_i >= accessors.size()) {
		return false;
	}
	if (uv0_i >= 0 && (size_t)uv0_i >= accessors.size()) {
		return false;
	}
	if (uv1_i >= 0 && (size_t)uv1_i >= accessors.size()) {
		return false;
	}

	const Accessor &pos = accessors[(size_t)pos_i];
	const Accessor *uv0 = uv0_i >= 0 ? &accessors[(size_t)uv0_i] : nullptr;
	const Accessor *uv1 = uv1_i >= 0 ? &accessors[(size_t)uv1_i] : nullptr;
	const Accessor *color =
		(color_i >= 0 && (size_t)color_i < accessors.size())
			? &accessors[(size_t)color_i] : nullptr;
	if ((uv0 && uv0->count != pos.count) || (uv1 && uv1->count != pos.count)
			|| (color && color->count != pos.count)) {
		return false;
	}
	/* B-1172: the vertex's room, for sky rooms. Optional: an authored scene
	 * may name none, and then has no sky rooms. */
	const Accessor *room =
		(room_i >= 0 && (size_t)room_i < accessors.size()
			&& accessors[(size_t)room_i].count == pos.count)
			? &accessors[(size_t)room_i] : nullptr;
	auto readRoom = [&](uint32_t index) -> int32_t {
		uint32_t roomnum = 0;
		if (!room || !readIndexValue(*room, views, bin, bin_size, index, &roomnum)) {
			return -1;
		}
		return (int32_t)roomnum;
	};

	int material = intMember(prim, "material", -1);
	int indices_i = intMember(prim, "indices", -1);
	uint32_t first = (uint32_t)scene.vertices.size();
	if (indices_i >= 0 && (size_t)indices_i >= accessors.size()) return false;
	const auto vertex_count = indices_i >= 0 ? accessors[(size_t)indices_i].count : pos.count;
	if (vertex_limit && (first > vertex_limit || vertex_count > vertex_limit - first)) return false;

	/* Animated surfaces: an animated material's primitive carries
	 * _PD_DYNTEX (VEC4: N64 s, t and the texture scales). A primitive
	 * without a usable one draws still. */
	const Material *prim_material =
		(material >= 0 && (size_t)material < scene.materials.size())
			? &scene.materials[(size_t)material] : nullptr;
	const Accessor *dyntex = nullptr;
	std::vector<DyntexVertex> prim_dyntex;
	if (prim_material && prim_material->animation > SCENARIO_DYNTEX_NONE) {
		int dyntex_i = findAttr("_PD_DYNTEX");
		if (dyntex_i >= 0 && (size_t)dyntex_i < accessors.size()
				&& accessors[(size_t)dyntex_i].count == pos.count
				&& accessors[(size_t)dyntex_i].type == "VEC4"
				&& accessors[(size_t)dyntex_i].component_type == 5126) {
			dyntex = &accessors[(size_t)dyntex_i];
		} else {
			scene.dyntex_unusable_primitives++;
		}
	}
	auto readDyntex = [&](uint32_t index) -> bool {
		if (!dyntex) {
			return true;
		}
		float s = 0.0f, t = 0.0f, scale_s = 0.0f, scale_t = 0.0f;
		if (!readFloatVec4(*dyntex, views, bin, bin_size, index,
				&s, &t, &scale_s, &scale_t)) {
			return false;
		}
		DyntexVertex d;
		d.base_s = (int32_t)s;
		d.base_t = (int32_t)t;
		d.scale_s = (uint16_t)scale_s;
		d.scale_t = (uint16_t)scale_t;
		d.kind = prim_material->animation;
		d.material = material;
		prim_dyntex.push_back(d);
		return true;
	};

	if (indices_i >= 0) {
		const Accessor &indices = accessors[(size_t)indices_i];
		if ((indices.count % 3u) != 0u) {
			return false;
		}
		for (uint32_t i = 0; i < indices.count; i++) {
			uint32_t idx = 0;
			Vertex out {};
			if (!readIndexValue(indices, views, bin, bin_size, i, &idx)
					|| idx >= pos.count
					|| !readFloatVec3(pos, views, bin, bin_size, idx,
						&out.x, &out.y, &out.z)) {
				return false;
			}
			if (uv0 && !readFloatVec2(*uv0, views, bin, bin_size, idx,
					&out.u0, &out.v0)) {
				return false;
			}
			if (uv1 && !readFloatVec2(*uv1, views, bin, bin_size, idx,
					&out.u1, &out.v1)) {
				return false;
			}
			if (!uv0) {
				out.u0 = out.u1;
				out.v0 = out.v1;
			}
			if (!uv1) {
				out.u1 = out.u0;
				out.v1 = out.v0;
			}
			out.r = out.g = out.b = out.a = 1.0f;
			if (color && !readFloatVec4(*color, views, bin, bin_size, idx,
					&out.r, &out.g, &out.b, &out.a)) {
				return false;
			}
			out.room = readRoom(idx);
			if (!readDyntex(idx)) {
				return false;
			}
			scene.vertices.push_back(out);
		}
	} else {
		if ((pos.count % 3u) != 0u) {
			return false;
		}
		for (uint32_t i = 0; i < pos.count; i++) {
			Vertex out {};
			if (!readFloatVec3(pos, views, bin, bin_size, i,
					&out.x, &out.y, &out.z)) {
				return false;
			}
			if (uv0 && !readFloatVec2(*uv0, views, bin, bin_size, i,
					&out.u0, &out.v0)) {
				return false;
			}
			if (uv1 && !readFloatVec2(*uv1, views, bin, bin_size, i,
					&out.u1, &out.v1)) {
				return false;
			}
			if (!uv0) {
				out.u0 = out.u1;
				out.v0 = out.v1;
			}
			if (!uv1) {
				out.u1 = out.u0;
				out.v1 = out.v0;
			}
			out.r = out.g = out.b = out.a = 1.0f;
			if (color && !readFloatVec4(*color, views, bin, bin_size, i,
					&out.r, &out.g, &out.b, &out.a)) {
				return false;
			}
			out.room = readRoom(i);
			if (!readDyntex(i)) {
				return false;
			}
			scene.vertices.push_back(out);
		}
	}

	for (size_t i = first; i < scene.vertices.size(); i++) {
		Vertex &vertex = scene.vertices[i];
		const float position[3] = { vertex.x, vertex.y, vertex.z };
		float transformed[3];
		if (!modAssetGltfSceneTransformPoint(&instance, position, transformed)) return false;
		vertex.x = transformed[0]; vertex.y = transformed[1]; vertex.z = transformed[2];
	}
	if (instance.mirrored) {
		for (size_t i = first; i + 2 < scene.vertices.size(); i += 3)
			std::swap(scene.vertices[i + 1], scene.vertices[i + 2]);
		for (size_t i = 0; i + 2 < prim_dyntex.size(); i += 3)
			std::swap(prim_dyntex[i + 1], prim_dyntex[i + 2]);
	}
	for (size_t k = 0; k < prim_dyntex.size(); k++) {
		DyntexVertex d = prim_dyntex[k];
		d.vertex = first + (uint32_t)k;
		d.room = scene.vertices[d.vertex].room;
		scene.dyntex_vertices.push_back(d);
	}

	DrawGroup group;
	group.material = material;
	group.first = first;
	group.count = (uint32_t)scene.vertices.size() - first;
	if (group.count > 0) {
		scene.groups.push_back(group);
	}
	return group.count > 0;
}

/* Canonical buffer validation is shared with the generic compiler. Decoding
 * occurs only after the shared validator proves alphabet, padding and size. */
static uint8_t *decodeSceneBuffer(const char *uri, uint32_t declared)
{
	if (!modAssetGltfDataUriSize(uri, declared)) return nullptr;
	auto digit = [](unsigned char c) -> unsigned {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		return c == '+' ? 62 : 63;
	};
	uint8_t *out = static_cast<uint8_t *>(std::malloc(declared));
	if (!out) return nullptr;
	const char *encoded = std::strchr(uri, ',') + 1;
	uint32_t cursor = 0;
	for (size_t i = 0; encoded[i]; i += 4) {
		const uint32_t bits = (digit(encoded[i]) << 18) | (digit(encoded[i + 1]) << 12) |
			(encoded[i + 2] == '=' ? 0 : digit(encoded[i + 2]) << 6) |
			(encoded[i + 3] == '=' ? 0 : digit(encoded[i + 3]));
		if (cursor < declared) out[cursor++] = static_cast<uint8_t>(bits >> 16);
		if (cursor < declared) out[cursor++] = static_cast<uint8_t>(bits >> 8);
		if (cursor < declared) out[cursor++] = static_cast<uint8_t>(bits);
	}
	return out;
}

enum class SceneBuild { Runtime, GeometryProbe, GridGeometry };

static bool buildCpuSceneBytes(const char *scenario_id, const char *path,
	const uint8_t *bytes, u32 size, Scene &out, SceneBuild build)
{
	if (!bytes || !size) return false;
	const bool geometry_probe = build == SceneBuild::GeometryProbe;
	const bool detached = build != SceneBuild::Runtime;
	/* A small mesh can be instanced many times. Bound the expanded
	 * vertex and image storage independently of the archive size. */
	constexpr size_t geometry_bytes = 64u * 1024u * 1024u;
	const size_t vertex_limit = detached ? geometry_bytes / (sizeof(Vertex) + sizeof(DyntexVertex)) : 0;
	if (probeLoggingEnabled()) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER.CPU_PROBE: loaded source scenario='%s' source=%s bytes=%u",
			scenario_id ? scenario_id : "?", path ? path : "", size);
	}

	bool ok = false;
	const uint8_t *json_bytes = nullptr;
	uint32_t json_size = 0;
	const uint8_t *bin = nullptr;
	uint32_t bin_size = 0;
	std::unique_ptr<uint8_t, decltype(&std::free)> owned_bin(nullptr, std::free);
	modasset_gltf_scene_plan_t plan {};
	const std::unique_ptr<modasset_gltf_scene_plan_t, decltype(&modAssetGltfScenePlanFree)>
		plan_cleanup(&plan, modAssetGltfScenePlanFree);
	char plan_error[128] = {};

	if (size >= 4 && std::memcmp(bytes, "glTF", 4) == 0) {
		modasset_gltf_glb_chunks_t chunks;
		uint32_t declared;
		char uri[1];
		if (!modAssetGltfGlbChunks(bytes, size, &chunks) ||
				!modAssetGltfBufferDocument(reinterpret_cast<const char *>(chunks.json), chunks.json_size,
					1, uri, sizeof(uri), &declared) ||
				!modAssetGltfBufferSize(declared, chunks.bin_size, 1)) goto done;
		json_bytes = chunks.json;
		json_size = chunks.json_size;
		bin = chunks.bin;
		bin_size = declared;
	} else if (!detached) {
		std::vector<char> uri(static_cast<size_t>(size) + 1);
		uint32_t declared;
		if (!modAssetGltfBufferDocument(reinterpret_cast<const char *>(bytes), size,
				0, uri.data(), uri.size(), &declared)) goto done;
		json_bytes = bytes;
		json_size = size;
		if (std::strncmp(uri.data(), "data:", 5) == 0) {
			owned_bin.reset(decodeSceneBuffer(uri.data(), declared));
			bin_size = declared;
		} else {
			char buffer_path[FS_MAXPATH + 1];
			if (!modAssetGltfBufferPath(path, uri.data(), buffer_path, sizeof(buffer_path))) goto done;
			owned_bin.reset(static_cast<uint8_t *>(fsFileLoad(buffer_path, &bin_size)));
		}
		if (!owned_bin || !modAssetGltfBufferSize(declared, bin_size, 0)) goto done;
		bin = owned_bin.get();
	} else {
		goto done;
	}

	{
		std::string json((const char *)json_bytes, (size_t)json_size);
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parse json bytes=%u bin=%u",
				json_size, bin_size);
		}
		crude_json::value root;
		std::vector<BufferView> views;
		std::vector<Accessor> accessors;
		if (!modAssetGltfReadDocument(json.data(), json.size(), root) ||
				!modAssetGltfScenePlanBuild(json.data(), json.size(), &plan, plan_error, sizeof(plan_error)) ||
				!parseViews(root, views)
				|| !parseAccessors(root, accessors)) {
			goto done;
		}
		if (detached) {
			if (const auto *images = arrayMember(root, "images")) {
				for (const auto &image : *images) {
					const auto *uri = member(image, "uri");
					if (uri && (!geometry_probe || !uri->is_string()
							|| uri->get<crude_json::string>().rfind("data:", 0) != 0)) goto done;
				}
			}
		}
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parsed json views=%zu accessors=%zu",
				views.size(), accessors.size());
		}

		out.scenario_id = scenario_id ? scenario_id : "";
		out.source_path = path ? path : "";
		out.instances = plan.count;
		out.selected_scene = plan.selected_scene;
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: begin materials");
		}
		parseMaterials(root, out.materials);
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: end materials count=%zu begin images",
				out.materials.size());
		}
		if (!geometry_probe && !parseImages(root, views, bin, bin_size, out.images,
				detached ? geometry_bytes : 0)) goto done;
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: end images count=%zu begin material alpha",
				out.images.size());
		}
		markMaterialAlpha(out);
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: end material alpha begin meshes");
		}

		const crude_json::array *meshes = arrayMember(root, "meshes");
		if (!meshes) {
			goto done;
		}
		for (uint32_t i = 0; i < plan.count; i++) {
			const modasset_gltf_scene_instance_t &instance = plan.instances[i];
			if (instance.mesh_index < 0 || static_cast<size_t>(instance.mesh_index) >= meshes->size()) goto done;
			const auto &mesh = (*meshes)[static_cast<size_t>(instance.mesh_index)];
			const crude_json::array *prims =
				arrayMember(mesh, "primitives");
			if (!prims) {
				continue;
			}
			for (const auto &prim : *prims) {
				if (intMember(prim, "mode", 4) != 4) {
					goto done;
				}
				if (!appendPrimitive(prim, accessors, views, bin,
						bin_size, instance, out, vertex_limit)) {
					goto done;
				}
			}
		}
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: built vertices=%zu groups=%zu materials=%zu images=%zu",
				out.vertices.size(), out.groups.size(), out.materials.size(),
				out.images.size());
		}
		ok = !out.vertices.empty() && !out.groups.empty();
	}

done:
	if (!ok) {
		if (plan_error[0]) sysLogPrintf(LOG_WARNING,
			"SCENARIO.RENDER: source scene invalid source=%s reason=%s", path, plan_error);
		for (auto &img : out.images) {
			if (img.rgba) {
				stbi_image_free(img.rgba);
				img.rgba = nullptr;
			}
		}
		out = Scene();
	}
	return ok;
}

static bool buildCpuScene(const char *scenario_id, const char *path, Scene &out)
{
	u32 size = 0;
	const std::unique_ptr<void, decltype(&std::free)> bytes(fsFileLoad(path, &size), std::free);
	return buildCpuSceneBytes(scenario_id, path, static_cast<const uint8_t *>(bytes.get()), size, out,
		SceneBuild::Runtime);
}

/* dyntexTickRoom's first-use shift, per group: the vertices of one preset
 * in one room (dyntex.c groups by room and type as they load). */
static void prepareDyntex(Scene &scene)
{
	struct GroupRange {
		int32_t room;
		int kind;
		int min_s, max_s, min_t, max_t;
	};
	std::vector<GroupRange> groups;
	auto findGroup = [&](int32_t room, int kind) -> GroupRange & {
		for (GroupRange &g : groups) {
			if (g.room == room && g.kind == kind) {
				return g;
			}
		}
		groups.push_back({room, kind, 32767, -32766, 32767, -32766});
		return groups.back();
	};
	for (const DyntexVertex &d : scene.dyntex_vertices) {
		GroupRange &g = findGroup(d.room, d.kind);
		g.min_s = std::min(g.min_s, (int)d.base_s);
		g.max_s = std::max(g.max_s, (int)d.base_s);
		g.min_t = std::min(g.min_t, (int)d.base_t);
		g.max_t = std::max(g.max_t, (int)d.base_t);
	}
	for (DyntexVertex &d : scene.dyntex_vertices) {
		const GroupRange &g = findGroup(d.room, d.kind);
		int add_s = 0;
		int add_t = 0;
		scenarioDyntexGroupShift(g.min_s, g.max_s, g.min_t, g.max_t, &add_s, &add_t);
		d.base_s += add_s;
		d.base_t += add_t;
		if (d.kind > SCENARIO_DYNTEX_NONE && d.kind < SCENARIO_DYNTEX_KIND_COUNT) {
			scene.dyntex_kind_counts[d.kind]++;
		}
	}
	scene.dyntex_groups = groups.size();
}

/* Moves the animated vertices' runtime UVs for this frame. Returns whether
 * any changed, and the vertex range to upload. */
static bool applyDyntexFrame(Scene &scene, uint32_t *out_first, uint32_t *out_last)
{
	uint32_t first = UINT32_MAX;
	uint32_t last = 0;
	for (const DyntexVertex &d : scene.dyntex_vertices) {
		int16_t s = 0;
		int16_t t = 0;
		if (!scenarioDyntexAnimate(d.kind, &scene.dyntex_frame,
				d.base_s, d.base_t, &s, &t)) {
			continue;
		}
		if (d.material < 0 || (size_t)d.material >= scene.materials.size()
				|| d.vertex >= scene.vertices.size()) {
			continue;
		}
		const Material &mat = scene.materials[(size_t)d.material];
		float u = 0.0f;
		float v = 0.0f;
		scenarioDyntexUv(s, t, d.scale_s, d.scale_t,
			mat.anim_u_scale, mat.anim_v_scale, &u, &v);
		Vertex &vertex = scene.vertices[d.vertex];
		if (vertex.u1 == u && vertex.v1 == v) {
			continue;
		}
		vertex.u1 = u;
		vertex.v1 = v;
		first = std::min(first, d.vertex);
		last = std::max(last, d.vertex);
	}
	if (first == UINT32_MAX) {
		return false;
	}
	*out_first = first;
	*out_last = last;
	return true;
}

static void fillProbeResult(const Scene &scene,
	scenario_scene_renderer_probe_t *out_probe)
{
	if (!out_probe) {
		return;
	}

	out_probe->vertices = scene.vertices.size();
	out_probe->groups = scene.groups.size();
	out_probe->materials = scene.materials.size();
	out_probe->images = scene.images.size();
	out_probe->alpha_textures = scene.alpha_texture_count;
	out_probe->alpha_materials = scene.alpha_material_count;
	out_probe->secondary_materials = scene.secondary_material_count;
	out_probe->instances = scene.instances;
	out_probe->selected_scene = scene.selected_scene;
	sha256_ctx hash;
	sha256Init(&hash);
	for (size_t i = 0; i < scene.vertices.size(); i++) {
		const Vertex &vertex = scene.vertices[i];
		const float position[3] = { vertex.x, vertex.y, vertex.z };
		for (size_t axis = 0; axis < 3; axis++) {
			if (i == 0 || position[axis] < out_probe->bounds_min[axis]) out_probe->bounds_min[axis] = position[axis];
			if (i == 0 || position[axis] > out_probe->bounds_max[axis]) out_probe->bounds_max[axis] = position[axis];
			uint32_t bits;
			std::memcpy(&bits, &position[axis], sizeof(bits));
			const uint8_t encoded[4] = { static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8),
				static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24) };
			sha256Update(&hash, encoded, sizeof(encoded));
		}
	}
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Final(&hash, digest);
	sha256ToHex(digest, out_probe->geometry_sha256);
}

static GLuint compileShader(GLenum type, const char *src, const char *label)
{
	GLuint shader = glCreateShader(type);
	if (!shader) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.RENDER: shader create failed stage=%s",
			label ? label : "?");
		return 0;
	}

	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.RENDER: shader compile failed stage=%s log=%s",
			label ? label : "?", log[0] ? log : "<empty>");
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

static bool finiteFloat(float value)
{
	return std::isfinite(value);
}

static float vec3Length(const float v[3])
{
	if (!v) {
		return 0.0f;
	}

	float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (!finiteFloat(len2) || len2 <= 0.0f) {
		return 0.0f;
	}

	return std::sqrt(len2);
}

static bool finiteVec3(const float v[3])
{
	return v && finiteFloat(v[0]) && finiteFloat(v[1])
		&& finiteFloat(v[2]);
}

/* The scene's GLSL source, one copy. ensureShader compiles it; the bgfx path
 * reads it for its developer-only parity experiment
 * (--debug-bgfx-scene-classic-glsl, scenario_scene_renderer_bgfx.cpp). */
static void classicShaderSource(const char **out_vs, const char **out_fs)
{
	static const char *vs =
		"#version 130\n"
		"uniform mat4 u_VP;\n"
		"uniform vec3 u_SkyOffset;\n"
		"in vec3 a_Pos;\n"
		"in vec2 a_Uv0;\n"
		"in vec2 a_Uv1;\n"
		"in vec4 a_Color;\n"
		"in float a_Sky;\n"
		"in float a_Room;\n"
		/* B-1172: roomHighlight's formula (dlights.c), from two texel rows per
		 * room: brightness, flash, flash_black, unlit; tint rgb, greyscale. */
		"uniform sampler2D u_RoomLight;\n"
		"uniform int u_RoomLightCount;\n"
		/* B-1172: x = fog on, y = fog_mul, z = fog_offset (gfx_sp_vertex). */
		"uniform vec3 u_Fog;\n"
		"float pdRoomUnlit() {\n"
		"  int room = int(floor(a_Room + 0.5));\n"
		"  if (room < 0 || room >= u_RoomLightCount) return 0.0;\n"
		"  return texelFetch(u_RoomLight, ivec2(room, 0), 0).w;\n"
		"}\n"
		"vec4 pdRoomLit(vec4 col) {\n"
		"  int room = int(floor(a_Room + 0.5));\n"
		"  if (room < 0 || room >= u_RoomLightCount) return col;\n"
		"  vec4 l0 = texelFetch(u_RoomLight, ivec2(room, 0), 0);\n"
		"  vec4 l1 = texelFetch(u_RoomLight, ivec2(room, 1), 0);\n"
		"  if (l0.w > 0.5) return col;\n"
		"  vec3 c = floor(col.rgb * 255.0 + 0.5);\n"
		"  if (l1.w > 0.5) c = vec3(max(c.r, max(c.g, c.b)));\n"
		"  float m = max(c.r, max(c.g, c.b));\n"
		"  float mult = m > l0.x ? l0.x / m : 1.0;\n"
		"  c = floor(c * mult);\n"
		"  m = floor(m * mult);\n"
		"  float extra = l1.w > 0.5 ? 0.0 : min(l0.y, 285.0 - m);\n"
		"  if (c.r + c.g + c.b != 0.0 || l0.z > 0.5) c += vec3(extra);\n"
		"  c = floor(c * l1.rgb);\n"
		"  return vec4(clamp(c, 0.0, 255.0) / 255.0, col.a);\n"
		"}\n"
		/* B-1268: reflective room surfaces (G_LIGHTING | G_TEXTURE_GEN),
		 * lit and mapped as fast3d does at G_VTX. The vertex's colour
		 * entry is a normal, and roomHighlight has run over it as over
		 * every colour of the room's table (G_COL points at its output),
		 * so pdRoomLit runs first. Light: lightsSetForRoom's ambient,
		 * brightness x 0.5882353, plus one light of the room's brightness
		 * along (0x4d, 0x4d, 0x2e). UVs: the normal against this view's
		 * reflection vectors (guLookAtReflect), (dot + 1) / 4 times the
		 * G_TEXTURE scale, then the material's runtime UV scale. */
		"uniform vec4 u_TexGen;\n"
		"uniform vec4 u_TexGenUv;\n"
		"uniform vec3 u_LookX;\n"
		"uniform vec3 u_LookY;\n"
		"vec3 pdTexGenNormal() {\n"
		"  vec3 b = floor(pdRoomLit(a_Color).rgb * 255.0 + 0.5);\n"
		"  return b - 256.0 * step(128.0, b);\n"
		"}\n"
		"float pdTexGenBrightness() {\n"
		"  int room = int(floor(a_Room + 0.5));\n"
		"  if (room < 0 || room >= u_RoomLightCount) return 255.0;\n"
		"  vec4 l0 = texelFetch(u_RoomLight, ivec2(room, 0), 0);\n"
		"  return clamp(floor(l0.x + l0.y), 0.0, 255.0);\n"
		"}\n"
		"out vec2 v_Uv0;\n"
		"out vec2 v_Uv1;\n"
		"out vec4 v_Color;\n"
		"out float v_Fog;\n"
		"void main() {\n"
		"  vec4 clip = u_VP * vec4(a_Pos + a_Sky * u_SkyOffset, 1.0);\n"
		"  gl_Position = clip;\n"
		"  v_Fog = 0.0;\n"
		"  if (u_Fog.x > 0.5 && a_Sky < 0.5 && pdRoomUnlit() < 0.5) {\n"
		"    float w = abs(clip.w) < 0.001 ? 0.001 : clip.w;\n"
		"    float winv = 1.0 / w;\n"
		"    if (winv < 0.0) winv = 32767.0;\n"
		"    v_Fog = clamp(clip.z * winv * u_Fog.y + u_Fog.z, 0.0, 255.0) / 255.0;\n"
		"  }\n"
		"  v_Uv0 = a_Uv0;\n"
		"  v_Uv1 = a_Uv1;\n"
		"  v_Color = pdRoomLit(a_Color);\n"
		"  if (u_TexGen.x > 0.5) {\n"
		"    vec3 n = pdTexGenNormal();\n"
		"    float b = pdTexGenBrightness();\n"
		"    float lit = floor(floor(b * 0.5882353) + max(dot(n, normalize(vec3(77.0, 77.0, 46.0))) / 127.0, 0.0) * b);\n"
		"    v_Color = vec4(vec3(min(lit, 255.0) / 255.0), 1.0);\n"
		"    vec2 g = (clamp(vec2(dot(n, u_LookX), dot(n, u_LookY)) / 127.0, -1.0, 1.0) + 1.0) / 4.0;\n"
		"    v_Uv0 = vec2(floor(g.x * u_TexGen.y) * u_TexGenUv.x, 1.0 - floor(g.y * u_TexGen.z) * u_TexGenUv.y);\n"
		"    v_Uv1 = v_Uv0;\n"
		"  }\n"
		"}\n";
	static const char *fs =
		"#version 130\n"
		"uniform sampler2D u_Tex;\n"
		"uniform sampler2D u_Tex2;\n"
		"uniform int u_HasTex2;\n"
		"uniform int u_PrimaryTexcoord;\n"
		"uniform int u_SecondaryTexcoord;\n"
		"uniform float u_AlphaCutoff;\n"
		"in vec2 v_Uv0;\n"
		"in vec2 v_Uv1;\n"
		"in vec4 v_Color;\n"
		"in float v_Fog;\n"
		"uniform vec3 u_FogColor;\n"
		"out vec4 fragColor;\n"
		"vec2 pdTexcoord(int channel) {\n"
		"  return channel == 0 ? v_Uv0 : v_Uv1;\n"
		"}\n"
		"void main() {\n"
		"  vec4 sampled = texture(u_Tex, pdTexcoord(u_PrimaryTexcoord));\n"
		"  if (u_HasTex2 != 0) {\n"
		"    vec4 sampled2 = texture(u_Tex2, pdTexcoord(u_SecondaryTexcoord));\n"
		"    sampled = mix(sampled, sampled2, 0.5);\n"
		"  }\n"
		"  vec4 outColor = sampled * v_Color;\n"
		/* c3844 glass fix: MASK passes a real cutoff (crisp cutout edges); OPAQUE and
		 * BLEND pass ~0 so only fully-transparent texels drop. */
		"  if (outColor.a < u_AlphaCutoff) discard;\n"
		"  outColor.rgb = mix(outColor.rgb, u_FogColor, v_Fog);\n"
		"  fragColor = outColor;\n"
		"}\n";
	*out_vs = vs;
	*out_fs = fs;
}

extern "C" void sceneRendererClassicShaderSource(const char **vs, const char **fs)
{
	classicShaderSource(vs, fs);
}

static bool ensureShader(Scene &scene)
{
	if (scene.shader) {
		return true;
	}

	if (scene.shader_failed) {
		return false;
	}

	const char *vs = nullptr;
	const char *fs = nullptr;
	classicShaderSource(&vs, &fs);

	GLuint vert = compileShader(GL_VERTEX_SHADER, vs, "vertex");
	if (!vert) {
		scene.shader_failed = true;
		return false;
	}

	GLuint frag = compileShader(GL_FRAGMENT_SHADER, fs, "fragment");
	if (!frag) {
		scene.shader_failed = true;
		glDeleteShader(vert);
		return false;
	}

	GLuint program = glCreateProgram();
	if (!program) {
		scene.shader_failed = true;
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.RENDER: shader program create failed");
		glDeleteShader(vert);
		glDeleteShader(frag);
		return false;
	}

	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glBindAttribLocation(program, 0, "a_Pos");
	glBindAttribLocation(program, 1, "a_Uv0");
	glBindAttribLocation(program, 2, "a_Uv1");
	glBindAttribLocation(program, 3, "a_Color");
	glBindAttribLocation(program, 4, "a_Sky");
	glBindAttribLocation(program, 5, "a_Room");
	glLinkProgram(program);

	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = {};
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.RENDER: shader link failed log=%s",
			log[0] ? log : "<empty>");
		scene.shader_failed = true;
		glDeleteShader(vert);
		glDeleteShader(frag);
		glDeleteProgram(program);
		return false;
	}

	glDeleteShader(vert);
	glDeleteShader(frag);
	scene.shader = program;
	return true;
}

/* --renderer bgfx: vertices first..first+count in the bgfx layout
 * (scenario_scene_renderer_bgfx.h). bgfx has no 32-bit integer attributes, so
 * the room goes as the float that the GL path's glVertexAttribPointer(GL_INT,
 * GL_FALSE) conversion hands its shader. */
static std::vector<float> bgfxVertexData(const Scene &scene, size_t first, size_t count)
{
	std::vector<float> data(count * SCENE_BGFX_VERTEX_FLOATS);
	for (size_t i = 0; i < count; i++) {
		const Vertex &v = scene.vertices[first + i];
		float *out = &data[i * SCENE_BGFX_VERTEX_FLOATS];
		out[0] = v.x;
		out[1] = v.y;
		out[2] = v.z;
		out[3] = v.u0;
		out[4] = v.v0;
		out[5] = v.u1;
		out[6] = v.v1;
		out[7] = v.r;
		out[8] = v.g;
		out[9] = v.b;
		out[10] = v.a;
		out[11] = v.sky;
		out[12] = (float)v.room;
	}
	return data;
}

/* --renderer bgfx: the same vertices and textures as bgfx resources. */
static void ensureGpuBgfx(Scene &scene)
{
	if (!scene.bgfx_white) {
		const uint8_t white[4] = { 255, 255, 255, 255 };
		scene.bgfx_white = sceneBgfxCreateTexture(1, 1, white);
	}
	for (auto &img : scene.images) {
		if (!img.rgba || img.width <= 0 || img.height <= 0 || img.bgfx_tex) {
			continue;
		}
		img.bgfx_tex = sceneBgfxCreateTexture(img.width, img.height, img.rgba);
	}
	if (!scene.bgfx_vb) {
		const std::vector<float> data = bgfxVertexData(scene, 0, scene.vertices.size());
		scene.bgfx_vb = sceneBgfxCreateVertexBuffer(data.data(), (uint32_t)scene.vertices.size());
	}
	scene.bgfx_ready = scene.bgfx_vb != 0 && scene.bgfx_white != 0;
}

static void ensureGpu(Scene &scene)
{
	if (scene.vertices.empty()) {
		return;
	}

	/* bgfx resources whenever bgfx renders (--renderer bgfx or parity mode);
	 * the GL path's own resources unless bgfx presents. */
	if (gfxBgfxRunning() && !scene.bgfx_ready) {
		ensureGpuBgfx(scene);
	}
	if (gfxBgfxActive() || scene.gpu_ready) {
		return;
	}

	if (!ensureShader(scene)) {
		return;
	}

	if (!scene.white_tex) {
		const uint8_t white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &scene.white_tex);
		glBindTexture(GL_TEXTURE_2D, scene.white_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, white);
	}

	for (auto &img : scene.images) {
		if (!img.rgba || img.width <= 0 || img.height <= 0 || img.gl) {
			continue;
		}
		glGenTextures(1, &img.gl);
		glBindTexture(GL_TEXTURE_2D, img.gl);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, img.rgba);
	}

	glGenVertexArrays(1, &scene.vao);
	glGenBuffers(1, &scene.vbo);
	glBindVertexArray(scene.vao);
	glBindBuffer(GL_ARRAY_BUFFER, scene.vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(scene.vertices.size() * sizeof(Vertex)),
		scene.vertices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void *)(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void *)(5 * sizeof(float)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void *)(7 * sizeof(float)));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		(void *)offsetof(Vertex, sky));
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 1, GL_INT, GL_FALSE, sizeof(Vertex),
		(void *)offsetof(Vertex, room));
	scene.sky_dirty = false;
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	scene.gpu_ready = true;
}

static void freeScene(Scene &scene)
{
	if (scene.vbo) {
		glDeleteBuffers(1, &scene.vbo);
	}
	if (scene.vao) {
		glDeleteVertexArrays(1, &scene.vao);
	}
	if (scene.shader) {
		glDeleteProgram(scene.shader);
	}
	if (scene.white_tex) {
		glDeleteTextures(1, &scene.white_tex);
	}
	if (scene.room_light_tex) {
		glDeleteTextures(1, &scene.room_light_tex);
	}
	sceneBgfxDestroyVertexBuffer(scene.bgfx_vb);
	sceneBgfxDestroyTexture(scene.bgfx_white);
	for (auto &img : scene.images) {
		sceneBgfxDestroyTexture(img.bgfx_tex);
		if (img.gl) {
			glDeleteTextures(1, &img.gl);
		}
		if (img.rgba) {
			stbi_image_free(img.rgba);
		}
	}
	scene = Scene();
}

static bool uploadGridScene(Scene &scene)
{
	if (gfxBgfxActive()) {
		/* --renderer bgfx: bgfx resources only, no GL calls (ensureGpu). */
		ensureGpu(scene);
		return scene.bgfx_ready && std::all_of(scene.images.begin(), scene.images.end(),
			[](const Image &image) { return image.bgfx_tex != 0; });
	}
	if (!glGetString || !glGetString(GL_VERSION) || glGetError() != GL_NO_ERROR) return false;
	GLint vao = 0, buffer = 0, texture = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	ensureGpu(scene);
	const GLenum result = glGetError();
	glBindVertexArray((GLuint)vao);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buffer);
	glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
	return result == GL_NO_ERROR && scene.gpu_ready && scene.vao && scene.vbo && scene.white_tex
		&& std::all_of(scene.images.begin(), scene.images.end(), [](const Image &image) { return image.gl != 0; });
}

static void logTransformProbe(Scene &scene)
{
	if (scene.logged_transform || scene.vertices.empty()) {
		return;
	}

	scene.logged_transform = true;

	uint32_t in_xy = 0;
	uint32_t in_xyz = 0;
	uint32_t positive_w = 0;
	uint32_t finite_xy = 0;
	uint32_t nonfinite_xy = 0;
	float min_ndc_x = FLT_MAX;
	float min_ndc_y = FLT_MAX;
	float min_ndc_z = FLT_MAX;
	float max_ndc_x = -FLT_MAX;
	float max_ndc_y = -FLT_MAX;
	float max_ndc_z = -FLT_MAX;
	float min_w = FLT_MAX;
	float max_w = -FLT_MAX;

	for (const auto &v : scene.vertices) {
		float x0 = v.x;
		float y0 = v.y;
		float z0 = v.z;
		const float (*m)[4] = scene.view_projection;
		float x = x0 * m[0][0] + y0 * m[1][0] + z0 * m[2][0] + m[3][0];
		float y = x0 * m[0][1] + y0 * m[1][1] + z0 * m[2][1] + m[3][1];
		float z = x0 * m[0][2] + y0 * m[1][2] + z0 * m[2][2] + m[3][2];
		float w = x0 * m[0][3] + y0 * m[1][3] + z0 * m[2][3] + m[3][3];
		if (w > 0.0f) {
			positive_w++;
		}
		if (w < min_w) min_w = w;
		if (w > max_w) max_w = w;
		if (w != 0.0f) {
			float inv_w = 1.0f / w;
			float nx = x * inv_w;
			float ny = y * inv_w;
			float nz = z * inv_w;
			if (!std::isfinite(nx) || !std::isfinite(ny)) {
				nonfinite_xy++;
				continue;
			}
			finite_xy++;
			if (nx < min_ndc_x) min_ndc_x = nx;
			if (ny < min_ndc_y) min_ndc_y = ny;
			if (nz < min_ndc_z) min_ndc_z = nz;
			if (nx > max_ndc_x) max_ndc_x = nx;
			if (ny > max_ndc_y) max_ndc_y = ny;
			if (nz > max_ndc_z) max_ndc_z = nz;
			if (nx >= -1.0f && nx <= 1.0f && ny >= -1.0f && ny <= 1.0f) {
				in_xy++;
				if (nz >= -1.0f && nz <= 1.0f) {
					in_xyz++;
				}
			}
		}
	}

	sysLogPrintf(LOG_NOTE,
		"SCENARIO.RENDER.PROBE: scene='%s' vertices=%zu in_xy=%u in_xyz=%u finite_xy=%u nonfinite_xy=%u positive_w=%u w=[%.3f,%.3f] ndc_x=[%.3f,%.3f] ndc_y=[%.3f,%.3f] ndc_z=[%.3f,%.3f] vp0=[%.3f,%.3f,%.3f,%.3f] vp1=[%.3f,%.3f,%.3f,%.3f] vp2=[%.3f,%.3f,%.3f,%.3f] vp3=[%.3f,%.3f,%.3f,%.3f]",
		scene.scenario_id.c_str(), scene.vertices.size(), in_xy, in_xyz,
		finite_xy, nonfinite_xy, positive_w, min_w, max_w, min_ndc_x, max_ndc_x, min_ndc_y,
		max_ndc_y, min_ndc_z, max_ndc_z,
		scene.view_projection[0][0], scene.view_projection[0][1],
		scene.view_projection[0][2], scene.view_projection[0][3],
		scene.view_projection[1][0], scene.view_projection[1][1],
		scene.view_projection[1][2], scene.view_projection[1][3],
		scene.view_projection[2][0], scene.view_projection[2][1],
		scene.view_projection[2][2], scene.view_projection[2][3],
		scene.view_projection[3][0], scene.view_projection[3][1],
		scene.view_projection[3][2], scene.view_projection[3][3]);
}

} // namespace

struct scenario_scene_prepared {
	Scene scene;
	~scenario_scene_prepared() { freeScene(scene); }
};

extern "C" int scenarioSceneRendererProbeSource(const char *scenario_id,
	const char *scene_path, scenario_scene_renderer_probe_t *out_probe)
{
	if (!scene_path || !scene_path[0]) {
		if (out_probe) {
			*out_probe = scenario_scene_renderer_probe_t {};
		}
		return 0;
	}

	Scene probe;
	if (!buildCpuScene(scenario_id, scene_path, probe)) {
		if (out_probe) {
			*out_probe = scenario_scene_renderer_probe_t {};
		}
		return 0;
	}

	fillProbeResult(probe, out_probe);
	freeScene(probe);
	return 1;
}

extern "C" int scenarioSceneRendererProbeGlbBytes(const char *scenario_id, const char *scene_path,
	const void *bytes, u32 size, scenario_scene_renderer_probe_t *out_probe)
{
	if (!scene_path || !scene_path[0] || !out_probe) return 0;
	try {
		Scene probe;
		if (!buildCpuSceneBytes(scenario_id, scene_path,
				static_cast<const uint8_t *>(bytes), size, probe, SceneBuild::GeometryProbe)) return 0;
		scenario_scene_renderer_probe_t summary {};
		fillProbeResult(probe, &summary);
		*out_probe = summary;
		return 1;
	} catch (...) {
		return 0;
	}
}

extern "C" int scenarioSceneRendererPrepareGridGeometry(const char *scenario_id, const char *scene_path,
	const void *bytes, u32 size, scenario_scene_prepared_t **out)
{
	if (!out || *out || !scene_path || !scene_path[0] || !g_scene.active
			|| !(g_scene.gpu_ready || g_scene.bgfx_ready)) return 0;
	try {
		auto prepared = std::make_unique<scenario_scene_prepared_t>();
		Scene &next = prepared->scene;
		if (!buildCpuSceneBytes(scenario_id, scene_path, static_cast<const uint8_t *>(bytes),
				size, next, SceneBuild::GridGeometry)) return 0;
		for (const Material &material : next.materials) {
			if ((material.image >= 0 && (size_t)material.image >= next.images.size())
					|| (material.secondary_image >= 0 && (size_t)material.secondary_image >= next.images.size())) return 0;
		}
		if (g_scene.sky_vertex_count) {
			std::vector<int32_t> sky_rooms;
			for (const Vertex &vertex : g_scene.vertices) {
				if (vertex.sky != 0.0f) sky_rooms.push_back(vertex.room);
			}
			std::sort(sky_rooms.begin(), sky_rooms.end());
			sky_rooms.erase(std::unique(sky_rooms.begin(), sky_rooms.end()), sky_rooms.end());
			for (Vertex &vertex : next.vertices) {
				vertex.sky = std::binary_search(sky_rooms.begin(), sky_rooms.end(), vertex.room) ? 1.0f : 0.0f;
				if (vertex.sky != 0.0f) ++next.sky_vertex_count;
			}
		}
		prepareDyntex(next);
		if (!uploadGridScene(next)) return 0;
		next.active = true;
		*out = prepared.release();
		return 1;
	} catch (...) {
		return 0;
	}
}

extern "C" void scenarioSceneRendererSwapGridGeometry(scenario_scene_prepared_t *prepared)
{
	if (!prepared) return;
	Scene &next = prepared->scene;
	/* Room identities and stage rules are stable across a layout revision.
	 * Keep their current light, fog, and animated surface state. */
	std::swap(g_scene.room_lights, next.room_lights);
	std::swap(g_scene.slot_room_lights, next.slot_room_lights);
	std::swap(g_scene.room_light_tex, next.room_light_tex);
	std::swap(g_scene.room_light_tex_width, next.room_light_tex_width);
	std::swap(g_scene.fog_enabled, next.fog_enabled);
	std::swap(g_scene.fog_mul, next.fog_mul);
	std::swap(g_scene.fog_offset, next.fog_offset);
	std::swap(g_scene.fog_color, next.fog_color);
	std::swap(g_scene.dyntex_frame, next.dyntex_frame);
	next.dyntex_frame_dirty = !next.dyntex_vertices.empty();
	std::swap(g_scene, next);
	sysLogPrintf(LOG_NOTE, "SCENARIO.RENDER: committed prepared Grid scene '%s' vertices=%zu",
		g_scene.scenario_id.c_str(), g_scene.vertices.size());
}

extern "C" void scenarioSceneRendererReleaseGridGeometry(scenario_scene_prepared_t *prepared)
{
	delete prepared;
}

extern "C" int scenarioSceneRendererActivate(const char *scenario_id,
	const char *scene_path)
{
	if (!scene_path || !scene_path[0]) {
		scenarioSceneRendererDeactivate();
		return 0;
	}

	if (g_scene.active && g_scene.source_path == scene_path
			&& g_scene.scenario_id == (scenario_id ? scenario_id : "")) {
		return 1;
	}

	scenarioSceneRendererDeactivate();

	Scene next;
	if (!buildCpuScene(scenario_id, scene_path, next)) {
		sysLogPrintf(LOG_ERROR,
			"SCENARIO.RENDER: failed to build source scene renderer '%s' source=%s",
			scenario_id ? scenario_id : "?", scene_path);
		return 0;
	}

	next.active = true;
	prepareDyntex(next);
	g_scene = std::move(next);
	if (!g_scene.dyntex_vertices.empty() || g_scene.dyntex_unusable_primitives) {
		const size_t *k = g_scene.dyntex_kind_counts;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: animated surfaces '%s' vertices=%zu groups=%zu river=%zu monitor=%zu ocean=%zu arrows=%zu teleportal=%zu powerring=%zu powerjuice=%zu unusable_primitives=%zu",
			scenario_id ? scenario_id : "?", g_scene.dyntex_vertices.size(),
			g_scene.dyntex_groups,
			k[SCENARIO_DYNTEX_RIVER], k[SCENARIO_DYNTEX_MONITOR],
			k[SCENARIO_DYNTEX_OCEAN], k[SCENARIO_DYNTEX_ARROWS],
			k[SCENARIO_DYNTEX_TELEPORTAL], k[SCENARIO_DYNTEX_POWERRING],
			k[SCENARIO_DYNTEX_POWERJUICE], g_scene.dyntex_unusable_primitives);
	}
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.RENDER: activated source scene '%s' source=%s vertices=%zu groups=%zu materials=%zu images=%zu alpha_textures=%zu alpha_materials=%zu secondary_materials=%zu uv=TEXCOORD_1 alpha_modes=opaque:%zu/mask:%zu/blend:%zu dualtex=extras",
		scenario_id ? scenario_id : "?", scene_path,
		g_scene.vertices.size(), g_scene.groups.size(),
		g_scene.materials.size(), g_scene.images.size(),
		g_scene.alpha_texture_count, g_scene.alpha_material_count,
		g_scene.secondary_material_count,
		g_scene.opaque_material_count, g_scene.mask_material_count,
		g_scene.blend_material_count);
	return 1;
}

extern "C" void scenarioSceneRendererDeactivate(void)
{
	freeScene(g_scene);
}

extern "C" int scenarioSceneRendererIsActive(void)
{
	return g_scene.active ? 1 : 0;
}

extern "C" void scenarioSceneRendererSetCameraFrame(
	const float position[3],
	const float look[3],
	const float up[3],
	float fovy_degrees,
	float aspect,
	float znear,
	float zfar,
	float world_scale,
	const float draw_offset[3])
{
	scenario_scene_camera_t camera;

	/* B-1172: built in the game's draw space (scenario_scene_camera.c), so the
	 * scene's depth values are the ones the game's own draws use. */
	if (!scenarioSceneCameraBuild(&camera, position, look, up, fovy_degrees,
				aspect, znear, zfar, world_scale, draw_offset)) {
		g_scene.has_camera = false;
		if (g_scene.active && !g_scene.logged_camera_invalid) {
			g_scene.logged_camera_invalid = true;
			sysLogPrintf(LOG_WARNING,
				"SCENARIO.RENDER: rejected camera frame '%s' pos=%d look_len=%.6f up_len=%.6f fovy=%.3f aspect=%.3f z=[%.3f,%.3f]",
				g_scene.scenario_id.c_str(), finiteVec3(position) ? 1 : 0,
				vec3Length(look), vec3Length(up), fovy_degrees, aspect,
				znear, zfar);
		}
		return;
	}

	g_scene.camera = camera;
	memcpy(g_scene.view_projection, camera.view_projection, sizeof(g_scene.view_projection));
	if (g_scene.camera_slot_count < 8) {
		g_scene.camera_slots[g_scene.camera_slot_count] = camera;
		g_scene.last_camera_slot = g_scene.camera_slot_count++;
	}
	g_scene.has_camera = true;
	g_scene.camera_ever_valid = true;
	g_scene.logged_missing_camera = false;
	g_scene.logged_waiting_camera = false;

	if (!g_scene.logged_camera_ready) {
		g_scene.logged_camera_ready = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: accepted camera frame '%s' look_len=%.6f up_len=%.6f fovy=%.3f aspect=%.3f z=[%.3f,%.3f] world_scale=%.3f",
			g_scene.scenario_id.c_str(), vec3Length(look), vec3Length(up),
			fovy_degrees, aspect, znear, zfar, camera.world_scale);
	}
}

extern "C" int scenarioSceneRendererCurrentCameraSlot(void)
{
	return g_scene.last_camera_slot;
}

extern "C" void scenarioSceneRendererEndFrame(void)
{
	g_scene.camera_slot_count = 0;
	g_scene.last_camera_slot = -1;
}

extern "C" int scenarioSceneRendererSlotCamera(int slot, scenario_scene_camera_t *out)
{
	if (!g_scene.active || !g_scene.camera_ever_valid || !out) {
		return 0;
	}
	*out = (slot >= 0 && slot < g_scene.camera_slot_count)
		? g_scene.camera_slots[slot] : g_scene.camera;
	return 1;
}

extern "C" int scenarioSceneRendererVertexCount(void)
{
	return g_scene.active ? (int)g_scene.vertices.size() : 0;
}

extern "C" int scenarioSceneRendererVertexPosition(int index, float out[3])
{
	if (!g_scene.active || index < 0 || (size_t)index >= g_scene.vertices.size() || !out) {
		return 0;
	}
	out[0] = g_scene.vertices[(size_t)index].x;
	out[1] = g_scene.vertices[(size_t)index].y;
	out[2] = g_scene.vertices[(size_t)index].z;
	return 1;
}

extern "C" void scenarioSceneRendererSetSkyRooms(const int *rooms, int count)
{
	size_t sky_vertices = 0;

	if (!g_scene.active) {
		return;
	}
	for (Vertex &vertex : g_scene.vertices) {
		float sky = 0.0f;
		for (int i = 0; vertex.room >= 0 && rooms && i < count; i++) {
			if (rooms[i] == vertex.room) {
				sky = 1.0f;
				break;
			}
		}
		if (vertex.sky != sky) {
			vertex.sky = sky;
			g_scene.sky_dirty = true;
		}
		if (sky != 0.0f) {
			sky_vertices++;
		}
	}
	g_scene.sky_vertex_count = sky_vertices;
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.RENDER: sky rooms '%s' rooms=%d vertices=%zu (drawn relative to the camera's room)",
		g_scene.scenario_id.c_str(), rooms ? count : 0, sky_vertices);
}

extern "C" void scenarioSceneRendererSetRoomLights(const scenario_scene_room_light_t *lights, int count)
{
	if (!g_scene.active || !lights || count <= 0) {
		return;
	}
	std::vector<float> &texels = g_scene.room_lights;
	texels.assign((size_t)count * 8, 0.0f);
	int unlit = 0;
	for (int i = 0; i < count; i++) {
		float *row0 = &texels[(size_t)i * 4];
		float *row1 = &texels[((size_t)count + (size_t)i) * 4];
		row0[0] = lights[i].brightness;
		row0[1] = lights[i].flash;
		row0[2] = lights[i].flash_black ? 1.0f : 0.0f;
		row0[3] = lights[i].unlit ? 1.0f : 0.0f;
		row1[0] = lights[i].tint[0];
		row1[1] = lights[i].tint[1];
		row1[2] = lights[i].tint[2];
		row1[3] = lights[i].greyscale ? 1.0f : 0.0f;
		unlit += lights[i].unlit ? 1 : 0;
	}
	if (g_scene.last_camera_slot >= 0 && g_scene.last_camera_slot < 8) {
		g_scene.slot_room_lights[g_scene.last_camera_slot] = texels;
	}
	if (!g_scene.logged_room_lights) {
		g_scene.logged_room_lights = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: room lights '%s' rooms=%d unlit=%d (roomHighlight's formula in the scene shader)",
			g_scene.scenario_id.c_str(), count, unlit);
	}
}

extern "C" void scenarioSceneRendererSetDyntexFrame(float interval80,
	int teleports_on, int engines_destroyed)
{
	if (!g_scene.active || g_scene.dyntex_vertices.empty()) {
		return;
	}
	g_scene.dyntex_frame.interval80 = interval80;
	g_scene.dyntex_frame.teleports_on = teleports_on ? 1 : 0;
	g_scene.dyntex_frame.engines_destroyed = engines_destroyed ? 1 : 0;
	g_scene.dyntex_frame_dirty = true;
}

extern "C" void scenarioSceneRendererSetFog(int enabled, float fog_mul, float fog_offset, const float color[3])
{
	g_scene.fog_enabled = enabled ? 1 : 0;
	g_scene.fog_mul = fog_mul;
	g_scene.fog_offset = fog_offset;
	if (color) {
		memcpy(g_scene.fog_color, color, sizeof(g_scene.fog_color));
	}
	if (enabled && g_scene.active && !g_scene.logged_fog) {
		g_scene.logged_fog = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: fog '%s' fog_mul=%.0f fog_offset=%.0f colour=(%.3f,%.3f,%.3f) (the stage fog, applied by the scene shader)",
			g_scene.scenario_id.c_str(), fog_mul, fog_offset, g_scene.fog_color[0],
			g_scene.fog_color[1], g_scene.fog_color[2]);
	}
}

/* B-1172: the room lighting of the view that queued the draw (its camera
 * slot), else of the last camera. */
static const std::vector<float> &viewRoomLights(int light_slot)
{
	return (light_slot >= 0 && light_slot < g_scene.camera_slot_count && light_slot < 8
			&& !g_scene.slot_room_lights[light_slot].empty())
		? g_scene.slot_room_lights[light_slot] : g_scene.room_lights;
}

/* B-1268: the camera of the view that queued the draw, as
 * scenarioSceneRendererSlotCamera picks it. */
static const scenario_scene_camera_t &viewCamera(int slot)
{
	return (slot >= 0 && slot < g_scene.camera_slot_count)
		? g_scene.camera_slots[slot] : g_scene.camera;
}

/* B-1267: once per scene, how much of it is drawn single-sided and which
 * way its front faces read in this view. */
static void logSceneCulling(bool front_counter_clockwise)
{
	size_t single_groups = 0;
	size_t single_vertices = 0;
	size_t vertices = 0;

	for (const auto &group : g_scene.groups) {
		const bool cull_back = group.material >= 0
			&& (size_t)group.material < g_scene.materials.size()
			&& g_scene.materials[(size_t)group.material].cull_back;
		vertices += (size_t)group.count;
		if (cull_back) {
			single_groups++;
			single_vertices += (size_t)group.count;
		}
	}
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.RENDER: culling '%s' single_sided_groups=%zu of %zu triangles=%zu of %zu front=%s",
		g_scene.scenario_id.c_str(), single_groups, g_scene.groups.size(),
		single_vertices / 3, vertices / 3,
		front_counter_clockwise ? "counter_clockwise" : "clockwise");

	/* B-1268: and how much of it is lit and texture-generated. */
	size_t texgen_groups = 0;
	size_t texgen_vertices = 0;
	for (const auto &group : g_scene.groups) {
		if (group.material >= 0 && (size_t)group.material < g_scene.materials.size()
				&& g_scene.materials[(size_t)group.material].texgen) {
			texgen_groups++;
			texgen_vertices += (size_t)group.count;
		}
	}
	sysLogPrintf(LOG_NOTE,
		"SCENARIO.RENDER: reflective '%s' texgen_groups=%zu triangles=%zu",
		g_scene.scenario_id.c_str(), texgen_groups, texgen_vertices / 3);
}

/* --renderer bgfx: the GL path below, draw for draw, through bgfx. The
 * viewport, camera, sky offset, fog, room lighting, material choice and
 * passes are the same; scenario_scene_renderer_bgfx.cpp applies the same
 * state. */
static void renderSceneBgfx(int view_x, int view_y, int width, int height,
	const float *camera, const float *sky_shift, int light_slot, int passes)
{
	const std::vector<float> &texels = viewRoomLights(light_slot);
	scene_bgfx_pass_t pass = {};
	pass.x = view_x;
	pass.y = view_y;
	pass.width = width;
	pass.height = height;
	pass.view_projection = camera;
	memcpy(pass.sky_offset, sky_shift, sizeof(pass.sky_offset));
	pass.fog_enabled = g_scene.fog_enabled;
	pass.fog_mul = g_scene.fog_mul;
	pass.fog_offset = g_scene.fog_offset;
	memcpy(pass.fog_color, g_scene.fog_color, sizeof(pass.fog_color));
	pass.room_texels = texels.empty() ? nullptr : texels.data();
	pass.rooms = (int)(texels.size() / 8);
	{
		/* B-1268: this view's reflection vectors. */
		const scenario_scene_camera_t &view_camera = viewCamera(light_slot);
		memcpy(pass.look_x, view_camera.lookat_x, sizeof(pass.look_x));
		memcpy(pass.look_y, view_camera.lookat_y, sizeof(pass.look_y));
	}
	if (!sceneBgfxBeginPass(&pass)) {
		return;
	}
	/* B-1267: the game's front faces read counter-clockwise from the eye. */
	const int cull_back_faces =
		scenarioSceneMatrixKeepsWinding((const float (*)[4])camera) ? 1 : 2;
	for (int scene_pass = 0; scene_pass < 2; ++scene_pass) {
		if (!(passes & (scene_pass == 0 ? SCENARIO_SCENE_PASS_OPAQUE : SCENARIO_SCENE_PASS_BLEND))) {
			continue;
		}
		for (const auto &group : g_scene.groups) {
			bool group_blend = group.material >= 0
				&& (size_t)group.material < g_scene.materials.size()
				&& g_scene.materials[(size_t)group.material].alpha_mode
					== ALPHA_MODE_BLEND;
			if ((scene_pass == 0) == group_blend) {
				continue;
			}
			uint32_t tex = g_scene.bgfx_white;
			uint32_t tex2 = g_scene.bgfx_white;
			int wrap_s = GL_REPEAT;
			int wrap_t = GL_REPEAT;
			int wrap2_s = GL_REPEAT;
			int wrap2_t = GL_REPEAT;
			int has_tex2 = 0;
			int primary_texcoord = 0;
			int secondary_texcoord = 1;
			float alpha_cutoff = 0.01f;
			int cull = 0;
			float texgen[8] = {};
			if (group.material >= 0
					&& (size_t)group.material < g_scene.materials.size()) {
				const Material &mat = g_scene.materials[(size_t)group.material];
				cull = mat.cull_back ? cull_back_faces : 0;
				if (mat.texgen) {
					texgen[0] = 1.0f;
					texgen[1] = mat.texgen_scale_s;
					texgen[2] = mat.texgen_scale_t;
					texgen[4] = mat.texgen_u_step;
					texgen[5] = mat.texgen_v_step;
				}
				primary_texcoord = mat.primary_texcoord == 0 ? 0 : 1;
				secondary_texcoord = mat.secondary_texcoord == 0 ? 0 : 1;
				wrap_s = mat.wrap_s;
				wrap_t = mat.wrap_t;
				wrap2_s = mat.secondary_wrap_s;
				wrap2_t = mat.secondary_wrap_t;
				if (mat.alpha_mode == ALPHA_MODE_MASK) {
					alpha_cutoff = mat.alpha_cutoff;
				}
				if (mat.image >= 0 && (size_t)mat.image < g_scene.images.size()
						&& g_scene.images[(size_t)mat.image].bgfx_tex) {
					tex = g_scene.images[(size_t)mat.image].bgfx_tex;
				}
				if (mat.has_secondary && mat.secondary_image >= 0
						&& (size_t)mat.secondary_image < g_scene.images.size()
						&& g_scene.images[(size_t)mat.secondary_image].bgfx_tex) {
					tex2 = g_scene.images[(size_t)mat.secondary_image].bgfx_tex;
					has_tex2 = 1;
				}
			}
			sceneBgfxDraw(g_scene.bgfx_vb, group.first, group.count,
				tex, wrap_s, wrap_t, tex2, wrap2_s, wrap2_t,
				has_tex2, primary_texcoord, secondary_texcoord,
				alpha_cutoff, scene_pass == 1 ? 1 : 0, cull, texgen);
		}
	}
	sceneBgfxEndPass();

	if (sysArgCheck("--debug-clear-depth-after-scene")) {
		sceneBgfxClearDepth();
	}

	if (!g_scene.logged_render) {
		g_scene.logged_render = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: rendered native source scene '%s' vertices=%zu groups=%zu textures=%zu (bgfx)",
			g_scene.scenario_id.c_str(), g_scene.vertices.size(),
			g_scene.groups.size(), g_scene.images.size());
		logSceneCulling(cull_back_faces == 1);
	}
}

extern "C" void scenarioSceneRendererRender(int width, int height)
{
	scenarioSceneRendererRenderPasses(0, 0, width, height, NULL, NULL, -1,
		SCENARIO_SCENE_PASS_OPAQUE | SCENARIO_SCENE_PASS_BLEND);
}

extern "C" void scenarioSceneRendererRenderPasses(int view_x, int view_y, int width, int height,
	const float *view_projection, const float *sky_offset, int light_slot, int passes)
{
	if (!g_scene.active || g_scene.vertices.empty()
			|| !(passes & (SCENARIO_SCENE_PASS_OPAQUE | SCENARIO_SCENE_PASS_BLEND))) {
		return;
	}
	const float *camera = view_projection ? view_projection : &g_scene.view_projection[0][0];
	const float *sky_shift = sky_offset ? sky_offset : g_scene.camera.draw_offset;

	if (!g_scene.has_camera) {
		if (!g_scene.camera_ever_valid) {
			if (!g_scene.logged_waiting_camera) {
				g_scene.logged_waiting_camera = true;
				sysLogPrintf(LOG_NOTE,
					"SCENARIO.RENDER: waiting for first camera frame '%s'",
					g_scene.scenario_id.c_str());
			}
			return;
		}
		if (!g_scene.logged_missing_camera) {
			g_scene.logged_missing_camera = true;
			sysLogPrintf(LOG_WARNING,
				"SCENARIO.RENDER: skipping source scene '%s' without camera matrices",
				g_scene.scenario_id.c_str());
		}
		return;
	}

	ensureGpu(g_scene);
	const bool gl_ready = !gfxBgfxActive() && g_scene.gpu_ready && g_scene.shader;
	const bool bgfx_ready = gfxBgfxRunning() && g_scene.bgfx_ready;
	if (!gl_ready && !bgfx_ready) {
		if (!g_scene.logged_gpu_unavailable) {
			g_scene.logged_gpu_unavailable = true;
			sysLogPrintf(LOG_WARNING,
				"SCENARIO.RENDER: GPU upload unavailable '%s' ready=%d shader=%u",
				g_scene.scenario_id.c_str(), g_scene.gpu_ready ? 1 : 0,
				(unsigned)g_scene.shader);
		}
		return;
	}

	logTransformProbe(g_scene);

	/* This frame's vertex changes, uploaded below to each backend that
	 * draws: B-1172 sky flags set after the upload (every vertex), and the
	 * animated surfaces' UVs (the range of vertices that moved). */
	const bool upload_all = g_scene.sky_dirty;
	bool moved = false;
	uint32_t moved_first = 0;
	uint32_t moved_last = 0;
	g_scene.sky_dirty = false;
	if (g_scene.dyntex_frame_dirty) {
		moved = applyDyntexFrame(g_scene, &moved_first, &moved_last);
		if (moved && !g_scene.logged_dyntex_update) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER: animated surfaces moved '%s' vertices %u..%u interval80=%.4f teleports_on=%d engines_destroyed=%d",
				g_scene.scenario_id.c_str(), moved_first, moved_last,
				g_scene.dyntex_frame.interval80,
				g_scene.dyntex_frame.teleports_on,
				g_scene.dyntex_frame.engines_destroyed);
			g_scene.logged_dyntex_update = true;
		}
		g_scene.dyntex_frame_dirty = false;
	}

	if (bgfx_ready) {
		if (upload_all) {
			const std::vector<float> data = bgfxVertexData(g_scene, 0, g_scene.vertices.size());
			sceneBgfxUpdateVertices(g_scene.bgfx_vb, 0, data.data(), (uint32_t)g_scene.vertices.size());
		} else if (moved) {
			const uint32_t moved_count = moved_last - moved_first + 1;
			const std::vector<float> data = bgfxVertexData(g_scene, moved_first, moved_count);
			sceneBgfxUpdateVertices(g_scene.bgfx_vb, moved_first, data.data(), moved_count);
		}
		renderSceneBgfx(view_x, view_y, width, height, camera, sky_shift, light_slot, passes);
	}
	if (!gl_ready) {
		return;
	}

	GLint prev_program = 0;
	GLint prev_texture = 0;
	GLint prev_texture1 = 0;
	GLint prev_active_texture = GL_TEXTURE0;
	GLint prev_vertex_array = 0;
	GLint prev_array_buffer = 0;
	GLint prev_viewport[4] = {};
	GLint prev_depth_func = GL_LESS;
	GLboolean prev_depth_mask = GL_TRUE;
	GLboolean prev_depth_test = GL_FALSE;
	GLboolean prev_cull_face = GL_FALSE;
	GLboolean prev_blend = GL_FALSE;
	GLboolean prev_texture_2d = GL_FALSE;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vertex_array);
	/* B-936 scene-desync FIX: save the GL_ARRAY_BUFFER binding. fast3d binds its
	 * vertex VBO once at init and assumes it stays bound -- gfx_opengl_draw_triangles
	 * uploads via glBufferData(GL_ARRAY_BUFFER,...) with no glBindBuffer. This
	 * renderer binds its own VBO and previously left GL_ARRAY_BUFFER at 0 on return,
	 * so fast3d's next world draw (Joanna, then props) uploaded into buffer 0 and
	 * read stale/garbage vertices -> she was submitted + framed yet invisible
	 * whenever the room was visible. Restoring the entry binding (= fast3d's VBO,
	 * which is why hide-scene worked) makes this renderer transparent to fast3d. */
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
	prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
	prev_cull_face = glIsEnabled(GL_CULL_FACE);
	prev_blend = glIsEnabled(GL_BLEND);
	prev_texture_2d = glIsEnabled(GL_TEXTURE_2D);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture1);
	GLint prev_texture2 = 0;
	glActiveTexture(GL_TEXTURE2);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture2);
	glActiveTexture(GL_TEXTURE0);

	/* B-1267: back faces are culled for the surfaces the game drew that
	 * way (Material::cull_back), group by group below. The game's front
	 * faces read counter-clockwise from the eye; a view-projection that
	 * mirrors (the y flip of a flipped framebuffer) reverses how they
	 * read in clip space. */
	GLint prev_front_face = GL_CCW;
	GLint prev_cull_mode = GL_BACK;
	glGetIntegerv(GL_FRONT_FACE, &prev_front_face);
	glGetIntegerv(GL_CULL_FACE_MODE, &prev_cull_mode);
	const bool front_counter_clockwise =
		scenarioSceneMatrixKeepsWinding((const float (*)[4])camera) != 0;

	glViewport(view_x, view_y, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glFrontFace(front_counter_clockwise ? GL_CCW : GL_CW);
	glCullFace(GL_BACK);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glUseProgram(g_scene.shader);
	GLint vp_loc = glGetUniformLocation(g_scene.shader, "u_VP");
	GLint tex_loc = glGetUniformLocation(g_scene.shader, "u_Tex");
	GLint tex2_loc = glGetUniformLocation(g_scene.shader, "u_Tex2");
	GLint has_tex2_loc = glGetUniformLocation(g_scene.shader, "u_HasTex2");
	GLint primary_texcoord_loc =
		glGetUniformLocation(g_scene.shader, "u_PrimaryTexcoord");
	GLint secondary_texcoord_loc =
		glGetUniformLocation(g_scene.shader, "u_SecondaryTexcoord");
	GLint alpha_cutoff_loc =
		glGetUniformLocation(g_scene.shader, "u_AlphaCutoff");
	/* B-1268: reflective surfaces: the material's texgen per group, this
	 * view's reflection vectors once. */
	GLint texgen_loc = glGetUniformLocation(g_scene.shader, "u_TexGen");
	GLint texgen_uv_loc = glGetUniformLocation(g_scene.shader, "u_TexGenUv");
	{
		const scenario_scene_camera_t &view_camera = viewCamera(light_slot);
		glUniform3fv(glGetUniformLocation(g_scene.shader, "u_LookX"), 1, view_camera.lookat_x);
		glUniform3fv(glGetUniformLocation(g_scene.shader, "u_LookY"), 1, view_camera.lookat_y);
	}
	glUniformMatrix4fv(vp_loc, 1, GL_FALSE, camera);
	glUniform3f(glGetUniformLocation(g_scene.shader, "u_SkyOffset"),
		sky_shift[0], sky_shift[1], sky_shift[2]);
	glUniform3f(glGetUniformLocation(g_scene.shader, "u_Fog"),
		g_scene.fog_enabled ? 1.0f : 0.0f, g_scene.fog_mul, g_scene.fog_offset);
	glUniform3f(glGetUniformLocation(g_scene.shader, "u_FogColor"),
		g_scene.fog_color[0], g_scene.fog_color[1], g_scene.fog_color[2]);
	{
		/* B-1172: this view's room lighting, on texture unit 2. */
		const std::vector<float> &texels = viewRoomLights(light_slot);
		const int rooms = (int)(texels.size() / 8);
		glActiveTexture(GL_TEXTURE2);
		if (rooms > 0) {
			if (!g_scene.room_light_tex) {
				glGenTextures(1, &g_scene.room_light_tex);
			}
			glBindTexture(GL_TEXTURE_2D, g_scene.room_light_tex);
			if (g_scene.room_light_tex_width != rooms) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, rooms, 2, 0, GL_RGBA, GL_FLOAT, texels.data());
				g_scene.room_light_tex_width = rooms;
			} else {
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rooms, 2, GL_RGBA, GL_FLOAT, texels.data());
			}
		}
		glActiveTexture(GL_TEXTURE0);
		glUniform1i(glGetUniformLocation(g_scene.shader, "u_RoomLight"), 2);
		glUniform1i(glGetUniformLocation(g_scene.shader, "u_RoomLightCount"), rooms);
	}
	glUniform1i(tex_loc, 0);
	glUniform1i(tex2_loc, 1);

	glBindVertexArray(g_scene.vao);
	if (upload_all) {
		/* B-1172: the sky flags were set after the upload. The entry
		 * GL_ARRAY_BUFFER binding is restored below. */
		glBindBuffer(GL_ARRAY_BUFFER, g_scene.vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0,
			(GLsizeiptr)(g_scene.vertices.size() * sizeof(Vertex)),
			g_scene.vertices.data());
	}
	if (moved) {
		/* Animated surfaces: this frame's UVs, uploaded for the range of
		 * vertices that moved. The entry GL_ARRAY_BUFFER binding is
		 * restored below. */
		glBindBuffer(GL_ARRAY_BUFFER, g_scene.vbo);
		glBufferSubData(GL_ARRAY_BUFFER,
			(GLintptr)((size_t)moved_first * sizeof(Vertex)),
			(GLsizeiptr)((size_t)(moved_last - moved_first + 1) * sizeof(Vertex)),
			g_scene.vertices.data() + moved_first);
	}
	/* c3844 glass fix: render scene groups via a lambda so we can do two passes --
	 * opaque first, then translucent (uses_alpha) materials with GL_BLEND on -- so
	 * translucent scene materials (e.g. the CI menu glass table) blend instead of
	 * being written opaque-black. The fragment shader already outputs texel alpha. */
	auto renderSceneGroup = [&](const auto &group) {
		GLuint tex = g_scene.white_tex;
		GLuint tex2 = g_scene.white_tex;
		int wrap_s = GL_REPEAT;
		int wrap_t = GL_REPEAT;
		int wrap2_s = GL_REPEAT;
		int wrap2_t = GL_REPEAT;
		int has_tex2 = 0;
		int primary_texcoord = 0;
		int secondary_texcoord = 1;
		/* MASK -> honor the material cutoff for crisp cutout edges; OPAQUE/BLEND ->
		 * tiny floor so only fully-transparent texels drop. */
		float alpha_cutoff = 0.01f;
		bool cull_back = false;
		float texgen[4] = {};
		float texgen_uv[4] = {};
		if (group.material >= 0
				&& (size_t)group.material < g_scene.materials.size()) {
			const Material &mat = g_scene.materials[(size_t)group.material];
			cull_back = mat.cull_back;
			if (mat.texgen) {
				texgen[0] = 1.0f;
				texgen[1] = mat.texgen_scale_s;
				texgen[2] = mat.texgen_scale_t;
				texgen_uv[0] = mat.texgen_u_step;
				texgen_uv[1] = mat.texgen_v_step;
			}
			primary_texcoord = mat.primary_texcoord == 0 ? 0 : 1;
			secondary_texcoord = mat.secondary_texcoord == 0 ? 0 : 1;
			wrap_s = mat.wrap_s;
			wrap_t = mat.wrap_t;
			wrap2_s = mat.secondary_wrap_s;
			wrap2_t = mat.secondary_wrap_t;
			if (mat.alpha_mode == ALPHA_MODE_MASK) {
				alpha_cutoff = mat.alpha_cutoff;
			}
			if (mat.image >= 0 && (size_t)mat.image < g_scene.images.size()
					&& g_scene.images[(size_t)mat.image].gl) {
				tex = g_scene.images[(size_t)mat.image].gl;
			}
			if (mat.has_secondary && mat.secondary_image >= 0
					&& (size_t)mat.secondary_image < g_scene.images.size()
					&& g_scene.images[(size_t)mat.secondary_image].gl) {
				tex2 = g_scene.images[(size_t)mat.secondary_image].gl;
				has_tex2 = 1;
			}
		}
		glUniform1f(alpha_cutoff_loc, alpha_cutoff);
		glUniform1i(has_tex2_loc, has_tex2);
		glUniform1i(primary_texcoord_loc, primary_texcoord);
		glUniform1i(secondary_texcoord_loc, secondary_texcoord);
		glUniform4fv(texgen_loc, 1, texgen);
		glUniform4fv(texgen_uv_loc, 1, texgen_uv);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap2_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap2_t);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
		if (cull_back) {
			glEnable(GL_CULL_FACE);
		} else {
			glDisable(GL_CULL_FACE);
		}
		glDrawArrays(GL_TRIANGLES, (GLint)group.first, (GLsizei)group.count);
	};
	/* c3844 glass fix: alphaMode-driven two-pass. Pass 0 = OPAQUE + MASK materials
	 * in the opaque pass (depth write on, no blend); MASK does its alpha-test cutout
	 * in the shader via u_AlphaCutoff for crisp edges. Pass 1 = BLEND materials,
	 * alpha-blended over the opaque scene with depth writes disabled so glass shows
	 * what's behind it (e.g. Joanna at the terminal). */
	for (int scene_pass = 0; scene_pass < 2; ++scene_pass) {
		/* B-1172: each pass draws where its command was queued. */
		if (!(passes & (scene_pass == 0 ? SCENARIO_SCENE_PASS_OPAQUE : SCENARIO_SCENE_PASS_BLEND))) {
			continue;
		}
		if (scene_pass == 1) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		for (const auto &group : g_scene.groups) {
			bool group_blend = group.material >= 0
				&& (size_t)group.material < g_scene.materials.size()
				&& g_scene.materials[(size_t)group.material].alpha_mode
					== ALPHA_MODE_BLEND;
			if ((scene_pass == 0) == group_blend) {
				continue;
			}
			renderSceneGroup(group);
		}
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBindVertexArray(0);

	/* B-936 scene-desync (--debug-clear-depth-after-scene TEST): the room was just
	 * drawn here with the scene renderer's OWN projection and depth writes. The
	 * fast3d world draws that follow in the same frame (Joanna, desk, PC) use the
	 * GAME projection, so sharing this depth buffer makes them depth-test against
	 * the room's depth values (a different projection space) and get rejected --
	 * she is submitted + framed dead-centre yet invisible whenever the room is
	 * visible. Clearing depth (keeping the room's colour) lets fast3d draw against
	 * a clean depth buffer; the room is the far backdrop so the foreground draws
	 * correctly in front. depthmask is TRUE here so the clear takes effect. */
	if (sysArgCheck("--debug-clear-depth-after-scene")) {
		/* glClear respects GL_SCISSOR_TEST -- disable it so the WHOLE depth buffer
		 * clears, not just whatever stale scissor box is active here. */
		GLboolean wasScissor = glIsEnabled(GL_SCISSOR_TEST);
		glDisable(GL_SCISSOR_TEST);
		glClear(GL_DEPTH_BUFFER_BIT);
		if (wasScissor) {
			glEnable(GL_SCISSOR_TEST);
		}
	}

	if (!g_scene.logged_render) {
		g_scene.logged_render = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: rendered native source scene '%s' vertices=%zu groups=%zu textures=%zu",
			g_scene.scenario_id.c_str(), g_scene.vertices.size(),
			g_scene.groups.size(), g_scene.images.size());
		logSceneCulling(front_counter_clockwise);
	}

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_texture2);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_texture1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_texture);
	glActiveTexture((GLenum)prev_active_texture);
	glBindVertexArray((GLuint)prev_vertex_array);
	/* B-936 scene-desync FIX: restore fast3d's vertex VBO binding (see save site). */
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_array_buffer);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2],
		prev_viewport[3]);
	if (prev_depth_test) {
		glEnable(GL_DEPTH_TEST);
	} else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthFunc((GLenum)prev_depth_func);
	glDepthMask(prev_depth_mask);
	if (prev_cull_face) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}
	glFrontFace((GLenum)prev_front_face);
	glCullFace((GLenum)prev_cull_mode);
	if (prev_blend) {
		glEnable(GL_BLEND);
	} else {
		glDisable(GL_BLEND);
	}
	if (prev_texture_2d) {
		glEnable(GL_TEXTURE_2D);
	} else {
		glDisable(GL_TEXTURE_2D);
	}
	glUseProgram((GLuint)prev_program);

	/* B-936 scene-desync (--debug-scene-glstate): dump the ACTUAL GL state this
	 * raw-GL renderer leaves on return, so we can compare it against what the first
	 * fast3d world draw (Joanna) needs. fast3d caches only depth_mode/alpha_blend/
	 * modulate/additive_blend/shader_program/textures and skips redundant GL sets;
	 * any state left here that differs from that cache but which fast3d does NOT
	 * re-set is the desync. The prior cache-invalidate covered depth/shader/texture
	 * only -- blend and cull were never checked. Throttled. */
	if (sysArgCheck("--debug-scene-glstate")) {
		static int s_glStateLog = 0;
		if (s_glStateLog < 6) {
			GLboolean bBlend = glIsEnabled(GL_BLEND);
			GLboolean bCull = glIsEnabled(GL_CULL_FACE);
			GLboolean bDepth = glIsEnabled(GL_DEPTH_TEST);
			GLboolean bScissor = glIsEnabled(GL_SCISSOR_TEST);
			GLint cullMode = 0, frontFace = 0, blendSrc = 0, blendDst = 0;
			GLint depthFunc = 0, curProg = 0, curVao = 0, curArrBuf = 0, drawFbo = 0;
			GLboolean depthMask = 0; GLboolean colorMask[4] = {0,0,0,0};
			glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
			glGetIntegerv(GL_FRONT_FACE, &frontFace);
			glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
			glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);
			glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
			glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curVao);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curArrBuf);
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
			glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
			sysLogPrintf(LOG_NOTE,
				"SCENEGL: BLEND=%d(src=0x%x dst=0x%x) CULL=%d(mode=0x%x front=0x%x) "
				"DEPTH=%d(func=0x%x mask=%d) SCISSOR=%d COLORMASK=%d%d%d%d "
				"prog=%d vao=%d arrbuf=%d drawfbo=%d",
				(int)bBlend, (unsigned)blendSrc, (unsigned)blendDst,
				(int)bCull, (unsigned)cullMode, (unsigned)frontFace,
				(int)bDepth, (unsigned)depthFunc, (int)depthMask, (int)bScissor,
				(int)colorMask[0], (int)colorMask[1], (int)colorMask[2], (int)colorMask[3],
				(int)curProg, (int)curVao, (int)curArrBuf, (int)drawFbo);
			s_glStateLog++;
		}
	}
}
