#include "scenario_scene_renderer.h"

#include "../external/imgui-node-editor/crude_json.h"
#include "../external/stb_image.h"
#include "glad/glad.h"

extern "C" {
#include "fs.h"
#include "system.h"
}

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

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
	float alpha_cutoff = 0.01f;
};

struct Image {
	const uint8_t *bytes = nullptr;
	uint32_t size = 0;
	std::string name;
	int width = 0;
	int height = 0;
	unsigned char *rgba = nullptr;
	GLuint gl = 0;
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
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint shader = 0;
	GLuint white_tex = 0;
	bool gpu_ready = false;
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
	uint64_t end = start + (uint64_t)stride * (uint64_t)a.count;
	if (elem == 0 || stride < elem || end > bin_size
			|| a.offset > view.length) {
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
			} else if (mode == "MASK") {
				mat.alpha_mode = ALPHA_MODE_MASK;
				mat.alpha_cutoff = floatField(*mat_obj, "alphaCutoff", 0.5f);
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
	uint32_t bin_size, std::vector<Image> &images)
{
	const crude_json::array *arr = arrayMember(root, "images");
	if (!arr) {
		return true;
	}
	for (const auto &v : *arr) {
		Image img;
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
		images.push_back(img);
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
		/* Back-compat + safety: a material the glTF left OPAQUE but whose texture
		 * actually carries non-opaque alpha is a cutout -> promote to MASK so it gets
		 * the alpha-test discard (covers scenes extracted before alphaMode=BLEND, and
		 * any opaque-list alpha texture). BLEND/MASK from the glTF are authoritative. */
		if (mat.alpha_mode == ALPHA_MODE_OPAQUE && texture_alpha) {
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
	uint32_t bin_size, Scene &scene)
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
	if (pos_i < 0 || (uv0_i < 0 && uv1_i < 0)
			|| (size_t)pos_i >= accessors.size()) {
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

	int material = intMember(prim, "material", -1);
	int indices_i = intMember(prim, "indices", -1);
	uint32_t first = (uint32_t)scene.vertices.size();

	if (indices_i >= 0) {
		if ((size_t)indices_i >= accessors.size()) {
			return false;
		}
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
			scene.vertices.push_back(out);
		}
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

static bool buildCpuScene(const char *scenario_id, const char *path,
	Scene &out)
{
	u32 size = 0;
	uint8_t *bytes = (uint8_t *)fsFileLoad(path, &size);
	if (!bytes || size < 28) {
		if (bytes) std::free(bytes);
		return false;
	}
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

	if (std::memcmp(bytes, "glTF", 4) != 0 || readLe32(bytes + 4) != 2u) {
		goto done;
	}

	{
		uint32_t json_len = readLe32(bytes + 12);
		uint32_t json_type = readLe32(bytes + 16);
		uint32_t bin_off = 20u + json_len;
		if (json_type != 0x4e4f534au || bin_off + 8u > size) {
			goto done;
		}
		json_bytes = bytes + 20;
		json_size = json_len;
		uint32_t bin_len = readLe32(bytes + bin_off);
		uint32_t bin_type = readLe32(bytes + bin_off + 4);
		if (bin_type != 0x004e4942u || bin_off + 8u + bin_len > size) {
			goto done;
		}
		bin = bytes + bin_off + 8u;
		bin_size = bin_len;
	}

	{
		std::string json((const char *)json_bytes, (size_t)json_size);
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parse json bytes=%u bin=%u",
				json_size, bin_size);
		}
		crude_json::value root = crude_json::value::parse(json);
		std::vector<BufferView> views;
		std::vector<Accessor> accessors;
		if (!root.is_object() || !parseViews(root, views)
				|| !parseAccessors(root, accessors)) {
			goto done;
		}
		if (probeLoggingEnabled()) {
			sysLogPrintf(LOG_NOTE,
				"SCENARIO.RENDER.CPU_PROBE: parsed json views=%zu accessors=%zu",
				views.size(), accessors.size());
		}

		out.scenario_id = scenario_id ? scenario_id : "";
		out.source_path = path ? path : "";
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
		parseImages(root, views, bin, bin_size, out.images);
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
		for (const auto &mesh : *meshes) {
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
						bin_size, out)) {
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
	std::free(bytes);
	if (!ok) {
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

static void multiplyMatrix(float out[4][4], const float a[4][4],
	const float b[4][4])
{
	float tmp[4][4];
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			tmp[i][j] = a[i][0] * b[0][j]
				+ a[i][1] * b[1][j]
				+ a[i][2] * b[2][j]
				+ a[i][3] * b[3][j];
		}
	}
	std::memcpy(out, tmp, sizeof(tmp));
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

static bool normalizeVec3(float v[3])
{
	float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (!finiteFloat(len2) || len2 <= 0.000001f) {
		return false;
	}

	float inv = 1.0f / std::sqrt(len2);
	v[0] *= inv;
	v[1] *= inv;
	v[2] *= inv;
	return finiteVec3(v);
}

static void loadIdentity(float m[4][4])
{
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			m[row][col] = row == col ? 1.0f : 0.0f;
		}
	}
}

static bool buildViewMatrix(float out[4][4], const float position[3],
	const float look[3], const float up[3])
{
	if (!finiteVec3(position) || !finiteVec3(look) || !finiteVec3(up)) {
		return false;
	}

	float zaxis[3] = { -look[0], -look[1], -look[2] };
	if (!normalizeVec3(zaxis)) {
		return false;
	}

	float xaxis[3] = {
		up[1] * zaxis[2] - up[2] * zaxis[1],
		up[2] * zaxis[0] - up[0] * zaxis[2],
		up[0] * zaxis[1] - up[1] * zaxis[0],
	};
	if (!normalizeVec3(xaxis)) {
		return false;
	}

	float yaxis[3] = {
		zaxis[1] * xaxis[2] - zaxis[2] * xaxis[1],
		zaxis[2] * xaxis[0] - zaxis[0] * xaxis[2],
		zaxis[0] * xaxis[1] - zaxis[1] * xaxis[0],
	};
	if (!normalizeVec3(yaxis)) {
		return false;
	}

	out[0][0] = xaxis[0];
	out[1][0] = xaxis[1];
	out[2][0] = xaxis[2];
	out[3][0] = -(position[0] * xaxis[0] + position[1] * xaxis[1]
		+ position[2] * xaxis[2]);

	out[0][1] = yaxis[0];
	out[1][1] = yaxis[1];
	out[2][1] = yaxis[2];
	out[3][1] = -(position[0] * yaxis[0] + position[1] * yaxis[1]
		+ position[2] * yaxis[2]);

	out[0][2] = zaxis[0];
	out[1][2] = zaxis[1];
	out[2][2] = zaxis[2];
	out[3][2] = -(position[0] * zaxis[0] + position[1] * zaxis[1]
		+ position[2] * zaxis[2]);

	out[0][3] = 0.0f;
	out[1][3] = 0.0f;
	out[2][3] = 0.0f;
	out[3][3] = 1.0f;
	return true;
}

static bool buildProjectionMatrix(float out[4][4], float fovy_degrees,
	float aspect, float znear, float zfar)
{
	if (!finiteFloat(fovy_degrees) || !finiteFloat(aspect)
			|| !finiteFloat(znear) || !finiteFloat(zfar)
			|| fovy_degrees <= 1.0f || fovy_degrees >= 179.0f
			|| aspect <= 0.01f || znear <= 0.0f || zfar <= znear) {
		return false;
	}

	float fovy = fovy_degrees * 3.14159265358979323846f / 180.0f;
	float cot = 1.0f / std::tan(fovy * 0.5f);
	if (!finiteFloat(cot)) {
		return false;
	}

	loadIdentity(out);
	out[0][0] = cot / aspect;
	out[1][1] = cot;
	out[2][2] = (znear + zfar) / (znear - zfar);
	out[2][3] = -1.0f;
	out[3][2] = (2.0f * znear * zfar) / (znear - zfar);
	out[3][3] = 0.0f;
	return true;
}

static bool ensureShader(Scene &scene)
{
	if (scene.shader) {
		return true;
	}

	if (scene.shader_failed) {
		return false;
	}

	static const char *vs =
		"#version 130\n"
		"uniform mat4 u_VP;\n"
		"in vec3 a_Pos;\n"
		"in vec2 a_Uv0;\n"
		"in vec2 a_Uv1;\n"
		"in vec4 a_Color;\n"
		"out vec2 v_Uv0;\n"
		"out vec2 v_Uv1;\n"
		"out vec4 v_Color;\n"
		"void main() {\n"
		"  gl_Position = u_VP * vec4(a_Pos, 1.0);\n"
		"  v_Uv0 = a_Uv0;\n"
		"  v_Uv1 = a_Uv1;\n"
		"  v_Color = a_Color;\n"
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
		"  fragColor = outColor;\n"
		"}\n";

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

static void ensureGpu(Scene &scene)
{
	if (scene.gpu_ready || scene.vertices.empty()) {
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
	for (auto &img : scene.images) {
		if (img.gl) {
			glDeleteTextures(1, &img.gl);
		}
		if (img.rgba) {
			stbi_image_free(img.rgba);
		}
	}
	scene = Scene();
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
	g_scene = std::move(next);
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
	float zfar)
{
	float view[4][4];
	float projection[4][4];

	if (!buildViewMatrix(view, position, look, up)
			|| !buildProjectionMatrix(projection, fovy_degrees, aspect,
				znear, zfar)) {
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

	multiplyMatrix(g_scene.view_projection, view, projection);
	g_scene.has_camera = true;
	g_scene.camera_ever_valid = true;
	g_scene.logged_missing_camera = false;
	g_scene.logged_waiting_camera = false;

	if (!g_scene.logged_camera_ready) {
		g_scene.logged_camera_ready = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: accepted camera frame '%s' look_len=%.6f up_len=%.6f fovy=%.3f aspect=%.3f z=[%.3f,%.3f]",
			g_scene.scenario_id.c_str(), vec3Length(look), vec3Length(up),
			fovy_degrees, aspect, znear, zfar);
	}
}

extern "C" void scenarioSceneRendererRender(int width, int height)
{
	if (!g_scene.active || g_scene.vertices.empty()) {
		return;
	}

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
	if (!g_scene.gpu_ready || !g_scene.shader) {
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
	glActiveTexture(GL_TEXTURE0);

	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
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
	glUniformMatrix4fv(vp_loc, 1, GL_FALSE,
		(const float *)g_scene.view_projection);
	glUniform1i(tex_loc, 0);
	glUniform1i(tex2_loc, 1);

	glBindVertexArray(g_scene.vao);
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
		if (group.material >= 0
				&& (size_t)group.material < g_scene.materials.size()) {
			const Material &mat = g_scene.materials[(size_t)group.material];
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
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap2_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap2_t);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
		glDrawArrays(GL_TRIANGLES, (GLint)group.first, (GLsizei)group.count);
	};
	/* c3844 glass fix: alphaMode-driven two-pass. Pass 0 = OPAQUE + MASK materials
	 * in the opaque pass (depth write on, no blend); MASK does its alpha-test cutout
	 * in the shader via u_AlphaCutoff for crisp edges. Pass 1 = BLEND materials,
	 * alpha-blended over the opaque scene with depth writes disabled so glass shows
	 * what's behind it (e.g. Joanna at the terminal). */
	for (int scene_pass = 0; scene_pass < 2; ++scene_pass) {
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
	}

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
