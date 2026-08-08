#include "pdtheme_source.h"

#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Parser {
	const char *cur;
	const char *end;
	char *error;
	size_t error_cap;
	bool failed;
};

static void fail(Parser &p, const char *fmt, ...)
{
	if (p.failed) return;
	p.failed = true;
	if (!p.error || p.error_cap == 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p.error, p.error_cap, fmt, ap);
	va_end(ap);
}

static void ws(Parser &p)
{
	while (p.cur < p.end && (*p.cur == ' ' || *p.cur == '\t' ||
			*p.cur == '\r' || *p.cur == '\n')) p.cur++;
}

static bool take(Parser &p, char c)
{
	ws(p);
	if (p.cur >= p.end || *p.cur != c) return false;
	p.cur++;
	return true;
}

static bool string_value(Parser &p, char *out, size_t cap, const char *field)
{
	ws(p);
	if (p.cur >= p.end || *p.cur != '"') {
		fail(p, "%s must be a string", field);
		return false;
	}
	p.cur++;
	size_t used = 0;
	while (p.cur < p.end && *p.cur != '"') {
		unsigned char c = (unsigned char)*p.cur++;
		if (c < 0x20) {
			fail(p, "%s contains a control character", field);
			return false;
		}
		if (c == '\\') {
			if (p.cur >= p.end) {
				fail(p, "%s has a truncated escape", field);
				return false;
			}
			char e = *p.cur++;
			switch (e) {
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case '/': c = '/'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			default:
				fail(p, "%s uses an unsupported escape", field);
				return false;
			}
		}
		if (out) {
			if (used + 1 >= cap) {
				fail(p, "%s exceeds its capacity", field);
				return false;
			}
			out[used] = (char)c;
		}
		used++;
	}
	if (p.cur >= p.end || *p.cur != '"') {
		fail(p, "%s is unterminated", field);
		return false;
	}
	p.cur++;
	if (out) out[used] = '\0';
	return true;
}

static bool boolean_value(Parser &p, bool &out, const char *field)
{
	ws(p);
	if (p.end - p.cur >= 4 && memcmp(p.cur, "true", 4) == 0) {
		p.cur += 4; out = true; return true;
	}
	if (p.end - p.cur >= 5 && memcmp(p.cur, "false", 5) == 0) {
		p.cur += 5; out = false; return true;
	}
	fail(p, "%s must be boolean", field);
	return false;
}

static bool number_value(Parser &p, double &out, const char *field,
	double min_value, double max_value, bool integer)
{
	ws(p);
	if (p.cur >= p.end) { fail(p, "%s is missing", field); return false; }
	const char *start = p.cur;
	if (*p.cur == '-') p.cur++;
	if (p.cur >= p.end) { fail(p, "%s must be a finite number", field); return false; }
	if (*p.cur == '0') {
		p.cur++;
	} else if (*p.cur >= '1' && *p.cur <= '9') {
		do { p.cur++; } while (p.cur < p.end && *p.cur >= '0' && *p.cur <= '9');
	} else {
		fail(p, "%s must be a finite number", field);
		return false;
	}
	if (p.cur < p.end && *p.cur == '.') {
		p.cur++;
		if (p.cur >= p.end || *p.cur < '0' || *p.cur > '9') {
			fail(p, "%s must be a finite number", field);
			return false;
		}
		do { p.cur++; } while (p.cur < p.end && *p.cur >= '0' && *p.cur <= '9');
	}
	if (p.cur < p.end && (*p.cur == 'e' || *p.cur == 'E')) {
		p.cur++;
		if (p.cur < p.end && (*p.cur == '+' || *p.cur == '-')) p.cur++;
		if (p.cur >= p.end || *p.cur < '0' || *p.cur > '9') {
			fail(p, "%s must be a finite number", field);
			return false;
		}
		do { p.cur++; } while (p.cur < p.end && *p.cur >= '0' && *p.cur <= '9');
	}
	const size_t length = (size_t)(p.cur - start);
	if (length >= 96) {
		fail(p, "%s must be a finite number", field);
		return false;
	}
	char token[96];
	memcpy(token, start, length);
	token[length] = '\0';
	char *after = nullptr;
	out = strtod(token, &after);
	if (!after || after != token + length || !std::isfinite(out)) {
		fail(p, "%s must be a finite number", field);
		return false;
	}
	if (out < min_value || out > max_value || (integer && floor(out) != out)) {
		fail(p, "%s is out of range", field);
		return false;
	}
	return true;
}

static bool valid_catalog_id(const char *s)
{
	if (!s || !s[0]) return false;
	const char *colon = strchr(s, ':');
	if (!colon || colon == s || !colon[1] || strchr(colon + 1, ':')) return false;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':'))
			return false;
	}
	return true;
}

static bool valid_semantic_id(const char *s)
{
	if (!s || !s[0]) return false;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return false;
	}
	return true;
}

static bool valid_color(const char *s)
{
	if (!s || strlen(s) != 8) return false;
	for (int i = 0; i < 8; i++) {
		char c = s[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
				(c >= 'A' && c <= 'F'))) return false;
	}
	return true;
}

static int palette_index(const char *key)
{
	static const char *const names[] = {
		"dialog_border1", "dialog_titlebg", "dialog_border2",
		"dialog_titlefg", "dialog_bodybg", "unused14", "item_unfocused",
		"item_disabled", "item_focused_inner", "checkbox_checked",
		"item_focused_outer", "listgroup_headerbg", "listgroup_headerfg",
		"unused34", "unused38", "toolbar_tint", "text_positive",
		"text_warning", "button_hover", "button_active", "title_glow",
		"tint_success", "tint_danger", "tint_info"
	};
	for (int i = 0; i < 24; i++) if (strcmp(key, names[i]) == 0) return i;
	return -1;
}

static bool object_begin(Parser &p, const char *field)
{
	if (!take(p, '{')) { fail(p, "%s must be an object", field); return false; }
	return true;
}

static bool array_begin(Parser &p, const char *field)
{
	if (!take(p, '[')) { fail(p, "%s must be an array", field); return false; }
	return true;
}

static bool next_key(Parser &p, bool &first, char *key, size_t cap,
	const char *field)
{
	ws(p);
	if (p.cur < p.end && *p.cur == '}') { p.cur++; return false; }
	if (!first && !take(p, ',')) { fail(p, "%s requires commas", field); return false; }
	first = false;
	if (!string_value(p, key, cap, field) || !take(p, ':')) {
		if (!p.failed) fail(p, "%s key requires ':'", field);
		return false;
	}
	return true;
}

static bool parse_palette(Parser &p, pdtheme_source_info_t &info)
{
	if (!object_begin(p, "palette")) return false;
	bool first = true; unsigned int seen = 0; char key[64];
	while (!p.failed && next_key(p, first, key, sizeof(key), "palette")) {
		int idx = palette_index(key);
		if (idx < 0 || (seen & (1u << idx))) {
			fail(p, idx < 0 ? "palette has unknown field '%s'" :
				"palette has duplicate field '%s'", key); break;
		}
		seen |= 1u << idx;
		char value[9];
		if (!string_value(p, value, sizeof(value), key) || !valid_color(value)) {
			if (!p.failed) fail(p, "palette.%s must be 8 hex digits", key);
			break;
		}
		info.palette_fields++;
	}
	return !p.failed;
}

static bool parse_catalog_map(Parser &p, pdtheme_source_info_t &info)
{
	if (!object_begin(p, "textures")) return false;
	bool first = true; char keys[PDTHEME_SOURCE_MAX_TEXTURES][64] = {{0}};
	char key[64];
	while (!p.failed && next_key(p, first, key, sizeof(key), "textures")) {
		if (!valid_semantic_id(key)) { fail(p, "textures has invalid role '%s'", key); break; }
		if (info.texture_roles >= PDTHEME_SOURCE_MAX_TEXTURES) {
			fail(p, "textures exceeds capacity %d", PDTHEME_SOURCE_MAX_TEXTURES); break;
		}
		for (int i = 0; i < info.texture_roles; i++) if (strcmp(keys[i], key) == 0) {
			fail(p, "textures has duplicate role '%s'", key); break;
		}
		if (p.failed) break;
		char id[64];
		if (!string_value(p, id, sizeof(id), key) || !valid_catalog_id(id)) {
			if (!p.failed) fail(p, "textures.%s must be a catalog ID", key); break;
		}
		strncpy(keys[info.texture_roles], key, sizeof(keys[0]) - 1);
		info.texture_roles++;
	}
	return !p.failed;
}

static bool parse_simple_object(Parser &p, const char *field, int kind)
{
	if (!object_begin(p, field)) return false;
	bool first = true; unsigned int seen = 0; char key[64];
	while (!p.failed && next_key(p, first, key, sizeof(key), field)) {
		int bit = -1; int type = 0; double lo = 0, hi = 1; bool integer = false;
		if (kind == 0) { /* scanline */
			if (!strcmp(key,"enabled")) { bit=0; type=1; }
			else if (!strcmp(key,"alpha")) { bit=1; type=2; }
			else if (!strcmp(key,"interval")) { bit=2; type=2; lo=1; hi=16; integer=true; }
		} else if (kind == 1) { /* textGlow */
			if (!strcmp(key,"enabled")) { bit=0; type=1; }
			else if (!strcmp(key,"intensity")) { bit=1; type=2; hi=4; }
			else if (!strcmp(key,"color")) { bit=2; type=3; }
		} else if (kind == 2) { /* fontShadow */
			if (!strcmp(key,"offsetX")) { bit=0; type=2; lo=-32; hi=32; }
			else if (!strcmp(key,"offsetY")) { bit=1; type=2; lo=-32; hi=32; }
			else if (!strcmp(key,"color")) { bit=2; type=3; }
		} else { /* fontGlow */
			if (!strcmp(key,"radius")) { bit=0; type=2; hi=32; }
			else if (!strcmp(key,"intensity")) { bit=1; type=2; hi=4; }
			else if (!strcmp(key,"color")) { bit=2; type=3; }
			else if (!strcmp(key,"passes")) { bit=3; type=2; lo=1; hi=8; integer=true; }
		}
		if (bit < 0 || (seen & (1u << bit))) { fail(p, "%s has %s field '%s'", field,
			bit < 0 ? "unknown" : "duplicate", key); break; }
		seen |= 1u << bit;
		if (type == 1) { bool v; boolean_value(p, v, key); }
		else if (type == 2) { double v; number_value(p, v, key, lo, hi, integer); }
		else { char color[9]; if (!string_value(p,color,sizeof(color),key) || !valid_color(color))
			if (!p.failed) fail(p, "%s.%s must be 8 hex digits", field,key); }
	}
	return !p.failed;
}

static bool parse_effect_array(Parser &p, const char *field, int kind,
	pdtheme_source_info_t &info)
{
	if (!array_begin(p, field)) return false;
	int &count = kind == 0 ? info.nineslices : kind == 1 ? info.caustics : info.border_effects;
	int cap = kind == 0 ? PDTHEME_SOURCE_MAX_NINESLICES : PDTHEME_SOURCE_MAX_EFFECTS;
	bool first_item = true;
	while (!p.failed) {
		ws(p); if (p.cur < p.end && *p.cur == ']') { p.cur++; break; }
		if (!first_item && !take(p, ',')) { fail(p, "%s requires commas", field); break; }
		first_item = false;
		if (++count > cap) { fail(p, "%s exceeds capacity %d", field, cap); break; }
		if (!object_begin(p, field)) break;
		bool first = true; unsigned int seen = 0; char key[64];
		while (!p.failed && next_key(p, first, key, sizeof(key), field)) {
			int bit=-1, type=0; double lo=0, hi=4096; bool integer=false;
			if (kind == 0) {
				if (!strcmp(key,"id")) {bit=0;type=1;}
				else if (!strcmp(key,"left")){bit=1;type=2;integer=true;}
				else if (!strcmp(key,"right")){bit=2;type=2;integer=true;}
				else if (!strcmp(key,"top")){bit=3;type=2;integer=true;}
				else if (!strcmp(key,"bottom")){bit=4;type=2;integer=true;}
				else if (!strcmp(key,"edgeMode")){bit=5;type=3;}
				else if (!strcmp(key,"centerMode")){bit=6;type=3;}
			} else if (kind == 1) {
				if (!strcmp(key,"elementId")){bit=0;type=1;}
				else if (!strcmp(key,"textureId")){bit=1;type=4;}
				else if (!strcmp(key,"frameCount")){bit=2;type=2;lo=1;hi=1024;integer=true;}
				else if (!strcmp(key,"speed")){bit=3;type=2;hi=1000;}
				else if (!strcmp(key,"opacity")){bit=4;type=2;hi=1;}
				else if (!strcmp(key,"scale")){bit=5;type=2;lo=0.01;hi=100;}
				else if (!strcmp(key,"blendMode")){bit=6;type=3;}
			} else {
				if (!strcmp(key,"elementId")){bit=0;type=1;}
				else if (!strcmp(key,"maskTextureId")){bit=1;type=4;}
				else if (!strcmp(key,"opacity")){bit=2;type=2;hi=1;}
				else if (!strcmp(key,"blendMode")){bit=3;type=3;}
				else if (!strcmp(key,"tintColor")){bit=4;type=5;}
				else if (!strcmp(key,"scrollX")){bit=5;type=2;lo=-1000;hi=1000;}
				else if (!strcmp(key,"scrollY")){bit=6;type=2;lo=-1000;hi=1000;}
			}
			if(bit<0 || (seen&(1u<<bit))){fail(p,"%s has %s field '%s'",field,bit<0?"unknown":"duplicate",key);break;}
			seen|=1u<<bit;
			if(type==1){char s[64]; if(!string_value(p,s,sizeof(s),key)||!valid_semantic_id(s)) if(!p.failed)fail(p,"%s.%s is invalid",field,key);}
			else if(type==4){char s[64]; if(!string_value(p,s,sizeof(s),key)||!valid_catalog_id(s)) if(!p.failed)fail(p,"%s.%s must be a catalog ID",field,key);}
			else if(type==2){double v;number_value(p,v,key,lo,hi,integer);}
			else if(type==5){char s[9];if(!string_value(p,s,sizeof(s),key)||!valid_color(s))if(!p.failed)fail(p,"%s.%s must be 8 hex digits",field,key);}
			else {char s[16];if(!string_value(p,s,sizeof(s),key)||
				(strcmp(s,"stretch")&&strcmp(s,"tile")&&strcmp(s,"multiply")&&strcmp(s,"additive")&&strcmp(s,"screen")))
				if(!p.failed)fail(p,"%s.%s has invalid mode",field,key);}
		}
	}
	return !p.failed;
}

}

extern "C" s32 pdthemeSourceParse(const char *json, size_t json_size,
	const char *expected_catalog_id, pdtheme_source_info_t *out,
	char *error, size_t error_cap)
{
	if (error && error_cap) error[0] = '\0';
	if (!json || json_size == 0 || json_size > 1024 * 1024) {
		if (error && error_cap) snprintf(error,error_cap,"theme source size is invalid");
		return 0;
	}
	pdtheme_source_info_t info = {};
	Parser p = {json, json + json_size, error, error_cap, false};
	if (!object_begin(p, "theme root")) return 0;
	bool first = true; unsigned int seen = 0; char key[64];
	while (!p.failed && next_key(p, first, key, sizeof(key), "theme root")) {
		int bit=-1;
		if(!strcmp(key,"schema"))bit=0; else if(!strcmp(key,"catalog_id"))bit=1;
		else if(!strcmp(key,"name"))bit=2; else if(!strcmp(key,"author"))bit=3;
		else if(!strcmp(key,"version"))bit=4; else if(!strcmp(key,"palette"))bit=5;
		else if(!strcmp(key,"textures"))bit=6; else if(!strcmp(key,"scanline"))bit=7;
		else if(!strcmp(key,"textGlow"))bit=8; else if(!strcmp(key,"soundPack"))bit=9;
		else if(!strcmp(key,"menuStyle"))bit=10; else if(!strcmp(key,"font"))bit=11;
		else if(!strcmp(key,"nineslices"))bit=12; else if(!strcmp(key,"caustics"))bit=13;
		else if(!strcmp(key,"borderEffects"))bit=14; else if(!strcmp(key,"fontShadow"))bit=15;
		else if(!strcmp(key,"fontGlow"))bit=16;
		if(bit<0 || (seen&(1u<<bit))){fail(p,"theme root has %s field '%s'",bit<0?"unknown":"duplicate",key);break;}
		seen|=1u<<bit;
		if(bit==0){char s[32];if(string_value(p,s,sizeof(s),key)&&strcmp(s,PDTHEME_SOURCE_SCHEMA))fail(p,"schema must be %s",PDTHEME_SOURCE_SCHEMA);}
		else if(bit==1){if(string_value(p,info.catalog_id,sizeof(info.catalog_id),key)&&!valid_catalog_id(info.catalog_id))fail(p,"catalog_id is invalid");}
		else if(bit==2)string_value(p,info.name,sizeof(info.name),key);
		else if(bit==3)string_value(p,info.author,sizeof(info.author),key);
		else if(bit==4)string_value(p,info.version,sizeof(info.version),key);
		else if(bit==5)parse_palette(p,info); else if(bit==6)parse_catalog_map(p,info);
		else if(bit==7)parse_simple_object(p,key,0); else if(bit==8)parse_simple_object(p,key,1);
		else if(bit==9||bit==10||bit==11){char id[64];if(string_value(p,id,sizeof(id),key)&&!valid_catalog_id(id))fail(p,"%s must be a catalog ID",key);}
		else if(bit==12)parse_effect_array(p,key,0,info); else if(bit==13)parse_effect_array(p,key,1,info);
		else if(bit==14)parse_effect_array(p,key,2,info); else if(bit==15)parse_simple_object(p,key,2);
		else if(bit==16)parse_simple_object(p,key,3);
	}
	ws(p);
	if (!p.failed && p.cur != p.end) fail(p,"theme root has trailing content");
	const unsigned int required=(1u<<0)|(1u<<1)|(1u<<2)|(1u<<4);
	if(!p.failed && (seen&required)!=required) fail(p,"theme root requires schema, catalog_id, name, and version");
	if(!p.failed && (!info.name[0]||!info.version[0])) fail(p,"name and version must not be empty");
	if(!p.failed && expected_catalog_id && strcmp(expected_catalog_id,info.catalog_id))
		fail(p,"catalog_id '%s' does not match '%s'",info.catalog_id,expected_catalog_id);
	if(p.failed) return 0;
	if(out)*out=info;
	return 1;
}
