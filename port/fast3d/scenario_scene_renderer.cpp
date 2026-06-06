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

struct Material {
	int image = -1;
	int wrap_s = GL_REPEAT;
	int wrap_t = GL_REPEAT;
};

struct Image {
	const uint8_t *bytes = nullptr;
	uint32_t size = 0;
	std::string name;
	int width = 0;
	int height = 0;
	unsigned char *rgba = nullptr;
	GLuint gl = 0;
};

struct Vertex {
	float x;
	float y;
	float z;
	float u;
	float v;
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
	bool logged_missing_camera = false;
	bool logged_transform = false;
	bool has_camera = false;
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

static void parseMaterials(const crude_json::value &root,
	std::vector<Material> &materials)
{
	const crude_json::array *samplers = arrayMember(root, "samplers");
	const crude_json::array *textures = arrayMember(root, "textures");
	const crude_json::array *mats = arrayMember(root, "materials");
	if (!mats) {
		return;
	}

	for (const auto &m : *mats) {
		Material mat;
		const crude_json::object *pbr =
			objectMember(m, "pbrMetallicRoughness");
		if (pbr) {
			auto it = pbr->find("baseColorTexture");
			if (it != pbr->end() && it->second.is_object()) {
				int tex_index = intMember(it->second, "index", -1);
				if (textures && tex_index >= 0
						&& (size_t)tex_index < textures->size()) {
					const crude_json::value &tex =
						(*textures)[(size_t)tex_index];
					mat.image = intMember(tex, "source", -1);
					int sampler = intMember(tex, "sampler", -1);
					if (samplers && sampler >= 0
							&& (size_t)sampler < samplers->size()) {
						const crude_json::value &s =
							(*samplers)[(size_t)sampler];
						mat.wrap_s = glWrap(intMember(s, "wrapS", 10497));
						mat.wrap_t = glWrap(intMember(s, "wrapT", 10497));
					}
				}
			}
		}
		materials.push_back(mat);
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
				int comp = 0;
				img.rgba = stbi_load_from_memory(img.bytes,
					(int)img.size, &img.width, &img.height, &comp, 4);
			}
		}
		images.push_back(img);
	}
	return true;
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
	int uv_i = findAttr("TEXCOORD_1");
	if (uv_i < 0) {
		uv_i = findAttr("TEXCOORD_0");
	}
	if (pos_i < 0 || uv_i < 0 || (size_t)pos_i >= accessors.size()
			|| (size_t)uv_i >= accessors.size()) {
		return false;
	}

	const Accessor &pos = accessors[(size_t)pos_i];
	const Accessor &uv = accessors[(size_t)uv_i];
	if (pos.count != uv.count) {
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
						&out.x, &out.y, &out.z)
					|| !readFloatVec2(uv, views, bin, bin_size, idx,
						&out.u, &out.v)) {
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
					&out.x, &out.y, &out.z)
					|| !readFloatVec2(uv, views, bin, bin_size, i,
						&out.u, &out.v)) {
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
		crude_json::value root = crude_json::value::parse(json);
		std::vector<BufferView> views;
		std::vector<Accessor> accessors;
		if (!root.is_object() || !parseViews(root, views)
				|| !parseAccessors(root, accessors)) {
			goto done;
		}

		out.scenario_id = scenario_id ? scenario_id : "";
		out.source_path = path ? path : "";
		parseMaterials(root, out.materials);
		parseImages(root, views, bin, bin_size, out.images);

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

static GLuint compileShader(GLenum type, const char *src)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);
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

static void ensureShader(Scene &scene)
{
	if (scene.shader) {
		return;
	}

	static const char *vs =
		"#version 130\n"
		"uniform mat4 u_VP;\n"
		"in vec3 a_Pos;\n"
		"in vec2 a_Uv;\n"
		"out vec2 v_Uv;\n"
		"void main() {\n"
		"  gl_Position = u_VP * vec4(a_Pos, 1.0);\n"
		"  v_Uv = a_Uv;\n"
		"}\n";
	static const char *fs =
		"#version 130\n"
		"uniform sampler2D u_Tex;\n"
		"in vec2 v_Uv;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"  fragColor = texture(u_Tex, v_Uv);\n"
		"}\n";

	GLuint vert = compileShader(GL_VERTEX_SHADER, vs);
	GLuint frag = compileShader(GL_FRAGMENT_SHADER, fs);
	scene.shader = glCreateProgram();
	glAttachShader(scene.shader, vert);
	glAttachShader(scene.shader, frag);
	glBindAttribLocation(scene.shader, 0, "a_Pos");
	glBindAttribLocation(scene.shader, 1, "a_Uv");
	glLinkProgram(scene.shader);
	glDeleteShader(vert);
	glDeleteShader(frag);
}

static void ensureGpu(Scene &scene)
{
	if (scene.gpu_ready || scene.vertices.empty()) {
		return;
	}

	ensureShader(scene);

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
		"SCENARIO.RENDER: activated source scene '%s' source=%s vertices=%zu groups=%zu materials=%zu images=%zu uv=TEXCOORD_1",
		scenario_id ? scenario_id : "?", scene_path,
		g_scene.vertices.size(), g_scene.groups.size(),
		g_scene.materials.size(), g_scene.images.size());
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
		return;
	}

	multiplyMatrix(g_scene.view_projection, view, projection);
	g_scene.has_camera = true;
	g_scene.logged_missing_camera = false;
}

extern "C" void scenarioSceneRendererRender(int width, int height)
{
	if (!g_scene.active || g_scene.vertices.empty()) {
		return;
	}

	if (!g_scene.has_camera) {
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
		return;
	}

	logTransformProbe(g_scene);

	GLint prev_program = 0;
	GLint prev_texture = 0;
	GLint prev_vertex_array = 0;
	GLint prev_depth_func = GL_LESS;
	GLboolean prev_depth_mask = GL_TRUE;
	GLboolean prev_depth_test = GL_FALSE;
	GLboolean prev_cull_face = GL_FALSE;
	GLboolean prev_blend = GL_FALSE;
	GLboolean prev_texture_2d = GL_FALSE;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vertex_array);
	glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
	prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
	prev_cull_face = glIsEnabled(GL_CULL_FACE);
	prev_blend = glIsEnabled(GL_BLEND);
	prev_texture_2d = glIsEnabled(GL_TEXTURE_2D);

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
	glUniformMatrix4fv(vp_loc, 1, GL_FALSE,
		(const float *)g_scene.view_projection);
	glUniform1i(tex_loc, 0);

	glBindVertexArray(g_scene.vao);
	for (const auto &group : g_scene.groups) {
		GLuint tex = g_scene.white_tex;
		int wrap_s = GL_REPEAT;
		int wrap_t = GL_REPEAT;
		if (group.material >= 0
				&& (size_t)group.material < g_scene.materials.size()) {
			const Material &mat = g_scene.materials[(size_t)group.material];
			wrap_s = mat.wrap_s;
			wrap_t = mat.wrap_t;
			if (mat.image >= 0 && (size_t)mat.image < g_scene.images.size()
					&& g_scene.images[(size_t)mat.image].gl) {
				tex = g_scene.images[(size_t)mat.image].gl;
			}
		}
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
		glDrawArrays(GL_TRIANGLES, (GLint)group.first, (GLsizei)group.count);
	}
	glBindVertexArray(0);

	if (!g_scene.logged_render) {
		g_scene.logged_render = true;
		sysLogPrintf(LOG_NOTE,
			"SCENARIO.RENDER: rendered native source scene '%s' vertices=%zu groups=%zu textures=%zu",
			g_scene.scenario_id.c_str(), g_scene.vertices.size(),
			g_scene.groups.size(), g_scene.images.size());
	}

	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_texture);
	glBindVertexArray((GLuint)prev_vertex_array);
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
}
