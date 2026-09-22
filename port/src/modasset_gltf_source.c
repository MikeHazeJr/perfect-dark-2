#include "modasset_gltf_source.h"
#include "modasset_json.h"
#include "sha256.h"
#include <stdlib.h>
#include <string.h>

static int hexDigit(unsigned char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static s32 safeSegment(const char *text, size_t size)
{
	char stem[5] = {0};
	size_t stem_size = 0;
	if (!size || text[size - 1] == '.' || text[size - 1] == ' ') return 0;
	while (stem_size < size && text[stem_size] != '.') stem_size++;
	if (stem_size > 4) return 1;
	for (size_t i = 0; i < stem_size; i++) {
		unsigned char c = (unsigned char)text[i];
		stem[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
	}
	return strcmp(stem, "CON") && strcmp(stem, "PRN")
		&& strcmp(stem, "AUX") && strcmp(stem, "NUL")
		&& !(stem_size == 4 && (memcmp(stem, "COM", 3) == 0
			|| memcmp(stem, "LPT", 3) == 0) && stem[3] >= '1' && stem[3] <= '9');
}

static s32 validUtf8(const char *text, size_t size)
{
	const unsigned char *p = (const unsigned char *)text;
	const unsigned char *end = p + size;
	while (p < end) {
		u32 value, minimum;
		unsigned continuation;
		unsigned char first = *p++;
		if (first < 0x80) continue;
		if (first >= 0xc2 && first <= 0xdf) {
			value = first & 0x1f; continuation = 1; minimum = 0x80;
		} else if (first >= 0xe0 && first <= 0xef) {
			value = first & 0x0f; continuation = 2; minimum = 0x800;
		} else if (first >= 0xf0 && first <= 0xf4) {
			value = first & 7; continuation = 3; minimum = 0x10000;
		} else return 0;
		for (unsigned i = 0; i < continuation; i++) {
			if (p >= end || (*p & 0xc0) != 0x80) return 0;
			value = (value << 6) | (*p++ & 0x3f);
		}
		if (value < minimum || value > 0x10ffff
				|| (value >= 0xd800 && value <= 0xdfff)) return 0;
	}
	return 1;
}

s32 modAssetGltfBufferPath(const char *source_path, const char *uri,
		char *out, size_t out_size)
{
	const char *member, *scan, *separator = NULL;
	size_t prefix, used, segment;
	if (out && out_size) out[0] = '\0';
	if (!source_path || !source_path[0] || !uri || !uri[0]
			|| !out || !out_size) return 0;
	member = source_path;
	for (scan = source_path; (scan = strstr(scan, "::")) != NULL; scan += 2)
		member = scan + 2;
	for (scan = member; *scan; scan++)
		if (*scan == '/' || *scan == '\\') separator = scan;
	prefix = separator ? (size_t)(separator + 1 - source_path)
		: (size_t)(member - source_path);
	if (prefix >= out_size) return 0;
	memcpy(out, source_path, prefix);
	used = prefix;
	segment = used;
	for (scan = uri; *scan; scan++) {
		unsigned char c = (unsigned char)*scan;
		if (c == '%') {
			int hi, lo;
			if (!scan[1] || !scan[2] || (hi = hexDigit(scan[1])) < 0
					|| (lo = hexDigit(scan[2])) < 0) goto bad;
			c = (unsigned char)((hi << 4) | lo);
			scan += 2;
			/* Encoded separators may not change the authored directory shape. */
			if (c == '/' || c == '\\') goto bad;
		}
		if (c <= 31 || c == 127 || c == ':' || c == '\\' || c == '?'
				|| c == '#' || c == '$' || c == '*' || c == '|' || c == '<'
				|| c == '>' || c == '"' || c == '%') goto bad;
		if (c == '/') {
			size_t n = used - segment;
			if (!safeSegment(out + segment, n)) goto bad;
			segment = used + 1;
		}
		if (used + 1 >= out_size) goto bad;
		out[used++] = (char)c;
	}
	if (!safeSegment(out + segment, used - segment)) goto bad;
	if (!validUtf8(out + prefix, used - prefix)) goto bad;
	out[used] = '\0';
	return 1;
bad:
	out[0] = '\0';
	return 0;
}

s32 modAssetGltfBufferSize(u32 declared_size, u32 actual_size, s32 glb)
{
	return declared_size > 0 && actual_size >= declared_size
		&& (glb ? actual_size - declared_size <= 3 : actual_size == declared_size);
}

static int base64Digit(unsigned char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

s32 modAssetGltfDataUriSize(const char *uri, u32 declared_size)
{
	static const char octet[] = "data:application/octet-stream;base64,";
	static const char gltf[] = "data:application/gltf-buffer;base64,";
	const char *encoded;
	size_t size, padding = 0;
	if (!uri || !declared_size) return 0;
	if (strncmp(uri, octet, sizeof(octet) - 1) == 0) encoded = uri + sizeof(octet) - 1;
	else if (strncmp(uri, gltf, sizeof(gltf) - 1) == 0) encoded = uri + sizeof(gltf) - 1;
	else return 0;
	size = strlen(encoded);
	if (!size || size % 4) return 0;
	if (encoded[size - 1] == '=') padding++;
	if (encoded[size - 2] == '=') padding++;
	if ((u64)(size / 4) * 3 - padding != declared_size) return 0;
	for (size_t i = 0; i < size - padding; i++)
		if (base64Digit((unsigned char)encoded[i]) < 0) return 0;
	if (padding == 2 && (base64Digit((unsigned char)encoded[size - 3]) & 15)) return 0;
	if (padding == 1 && (base64Digit((unsigned char)encoded[size - 2]) & 3)) return 0;
	return 1;
}

static u32 le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

s32 modAssetGltfGlbChunks(const u8 *data, u32 size,
		modasset_gltf_glb_chunks_t *out)
{
	modasset_gltf_glb_chunks_t candidate = {0};
	u32 offset = 12, chunk_index = 0;
	if (out) memset(out, 0, sizeof(*out));
	if (!data || !out || size < 20 || memcmp(data, "glTF", 4)
			|| le32(data + 4) != 2 || le32(data + 8) != size) return 0;
	while (offset < size) {
		u32 length, type;
		if (size - offset < 8) return 0;
		length = le32(data + offset);
		type = le32(data + offset + 4);
		offset += 8;
		if (length % 4 || length > size - offset) return 0;
		if (type == 0x4e4f534a) {
			if (chunk_index != 0 || candidate.json || !length) return 0;
			candidate.json = data + offset;
			candidate.json_size = length;
		} else if (type == 0x004e4942) {
			if (chunk_index != 1 || !candidate.json || candidate.bin) return 0;
			candidate.bin = data + offset;
			candidate.bin_size = length;
		} else if (!candidate.json) return 0;
		offset += length;
		chunk_index++;
	}
	if (!candidate.json) return 0;
	*out = candidate;
	return 1;
}

s32 modAssetGltfAccessorBounds(u32 buffer_size, u32 view_offset, u32 view_size,
		u32 accessor_offset, u32 count, u32 stride, u32 element_size)
{
	u64 extent;
	if (!count || !element_size || stride < element_size
			|| view_offset > buffer_size || view_size > buffer_size - view_offset
			|| accessor_offset > view_size) return 0;
	extent = (u64)accessor_offset + (u64)(count - 1) * stride + element_size;
	return extent <= view_size;
}

s32 modAssetGltfSourceHash(const void *json, u32 json_size,
		const void *buffer, u32 buffer_size, u8 out_digest[32])
{
	static const char domain[] = "pd2.gltf.public-source-and-buffer.v1";
	sha256_ctx hash;
	u8 lengths[8];
	if (!json || !json_size || (!buffer && buffer_size) || !out_digest) return 0;
	for (s32 i = 0; i < 4; i++) {
		lengths[i] = (u8)(json_size >> (8 * i));
		lengths[i + 4] = (u8)(buffer_size >> (8 * i));
	}
	sha256Init(&hash);
	sha256Update(&hash, domain, sizeof(domain));
	sha256Update(&hash, lengths, sizeof(lengths));
	sha256Update(&hash, json, json_size);
	if (buffer_size) sha256Update(&hash, buffer, buffer_size);
	sha256Final(&hash, out_digest);
	return 1;
}

static const char *skipWs(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
	return p;
}

static s32 takeChar(const char **p, const char *end, char c)
{
	*p = skipWs(*p, end);
	if (*p >= end || **p != c) return 0;
	(*p)++;
	return 1;
}

static s32 frameValue(const char **p, const char *end, s16 *out)
{
	s32 value;
	const char *next;
	*p = skipWs(*p, end);
	if (!modAssetJsonParseS32Token(*p, end, &value, &next)
			|| value < 0 || value > 32767) return 0;
	*out = (s16)value;
	*p = next;
	return 1;
}

static s32 repeatRange(const char **p, const char *end,
		modasset_gltf_repeat_range_t *out)
{
	unsigned seen = 0;
	if (!takeChar(p, end, '{')) return 0;
	for (int i = 0; i < 2; i++) {
		const char *name;
		size_t n;
		unsigned field;
		if (i && !takeChar(p, end, ',')) return 0;
		if (!takeChar(p, end, '"')) return 0;
		name = *p;
		while (*p < end && **p != '"') (*p)++;
		n = (size_t)(*p - name);
		if (!takeChar(p, end, '"') || !takeChar(p, end, ':')) return 0;
		if (n == 15 && memcmp(name, "repeat_to_frame", n) == 0) field = 1;
		else if (n == 17 && memcmp(name, "repeat_from_frame", n) == 0) field = 2;
		else return 0;
		if (seen & field) return 0;
		seen |= field;
		if (!frameValue(p, end, field == 1 ? &out->repeattoframe : &out->repeatfromframe))
			return 0;
	}
	return seen == 3 && takeChar(p, end, '}');
}

static s32 tailArray(const char *json, size_t size, s32 repeats,
		void **out, s32 *out_count)
{
	const char *p = json, *end;
	void *values = NULL;
	s32 count = 0, capacity = 0;
	size_t width = repeats ? sizeof(modasset_gltf_repeat_range_t) : sizeof(s16);
	/* One negative terminator plus at least one descriptor byte must fit. */
	s32 max_count = repeats ? (65535 - 3) / 4 : (65535 - 3) / 2;
	if (out) *out = NULL;
	if (out_count) *out_count = 0;
	if (!json || !out || !out_count) return 0;
	end = json + size;
	if (!takeChar(&p, end, '[')) return 0;
	p = skipWs(p, end);
	if (p < end && *p == ']') p++;
	else for (;;) {
		if (count >= max_count) goto bad;
		if (count == capacity) {
			s32 next = capacity ? capacity * 2 : 16;
			void *grown;
			if (next > max_count) next = max_count;
			grown = realloc(values, (size_t)next * width);
			if (!grown) goto bad;
			values = grown;
			capacity = next;
		}
		if (repeats) {
			if (!repeatRange(&p, end, &((modasset_gltf_repeat_range_t *)values)[count])) goto bad;
		} else if (!frameValue(&p, end, &((s16 *)values)[count])) goto bad;
		count++;
		p = skipWs(p, end);
		if (p < end && *p == ']') { p++; break; }
		if (!takeChar(&p, end, ',')) goto bad;
	}
	if (skipWs(p, end) != end) goto bad;
	*out = values;
	*out_count = count;
	return 1;
bad:
	free(values);
	return 0;
}

s32 modAssetGltfRepeatRanges(const char *json, size_t size,
		modasset_gltf_repeat_range_t **out, s32 *out_count)
{
	void *values = NULL;
	s32 ok;
	if (!out) return 0;
	*out = NULL;
	ok = tailArray(json, size, 1, &values, out_count);
	if (ok) *out = values;
	return ok;
}

s32 modAssetGltfCutSkipFrames(const char *json, size_t size,
		s16 **out, s32 *out_count)
{
	void *values = NULL;
	s32 ok;
	if (!out) return 0;
	*out = NULL;
	ok = tailArray(json, size, 0, &values, out_count);
	if (ok) *out = values;
	return ok;
}
