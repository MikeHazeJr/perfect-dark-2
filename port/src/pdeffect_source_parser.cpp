#include "pdeffect_source.h"

#include "asset_archive_policy.h"
#include "fs.h"
#include "modarchive.h"
#include "system.h"

#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Error {
	char *text; size_t cap; bool failed;
};

static void fail(Error &e, const char *fmt, ...)
{
	if (e.failed) return;
	e.failed = true;
	if (!e.text || !e.cap) return;
	va_list ap; va_start(ap, fmt); vsnprintf(e.text, e.cap, fmt, ap); va_end(ap);
}

enum JType { J_NULL, J_BOOL, J_NUMBER, J_STRING, J_OBJECT, J_ARRAY };
struct JValue {
	JType type = J_NULL;
	bool boolean = false;
	double number = 0;
	std::string string;
	std::vector<std::pair<std::string, JValue> > object;
	std::vector<JValue> array;
};

struct JsonParser { const char *p; const char *end; Error &error; };
static void ws(JsonParser &p) { while (p.p < p.end && strchr(" \t\r\n", *p.p)) p.p++; }

static bool jsonString(JsonParser &p, std::string &out)
{
	ws(p); if (p.p >= p.end || *p.p++ != '"') { fail(p.error, "JSON string expected"); return false; }
	while (p.p < p.end && *p.p != '"') {
		unsigned char c = (unsigned char)*p.p++;
		if (c < 0x20) { fail(p.error, "JSON string contains a control character"); return false; }
		if (c == '\\') {
			if (p.p >= p.end) { fail(p.error, "JSON string has a truncated escape"); return false; }
			char x = *p.p++;
			switch (x) {
			case '"': case '\\': case '/': c = (unsigned char)x; break;
			case 'b': c='\b'; break; case 'f': c='\f'; break; case 'n': c='\n'; break;
			case 'r': c='\r'; break; case 't': c='\t'; break;
			default: fail(p.error, "JSON string uses an unsupported escape"); return false;
			}
		}
		out.push_back((char)c);
		if (out.size() > 1024 * 1024) { fail(p.error, "JSON string is too large"); return false; }
	}
	if (p.p >= p.end) { fail(p.error, "JSON string is unterminated"); return false; }
	p.p++; return true;
}

static bool jsonValue(JsonParser &p, JValue &out, unsigned depth)
{
	if (depth > 48) { fail(p.error, "JSON nesting exceeds 48 levels"); return false; }
	ws(p); if (p.p >= p.end) { fail(p.error, "JSON value is missing"); return false; }
	if (*p.p == '"') { out.type=J_STRING; return jsonString(p,out.string); }
	if (*p.p == '{') {
		out.type=J_OBJECT; p.p++; ws(p); if (p.p<p.end && *p.p=='}') {p.p++;return true;}
		for (;;) { std::string key; if(!jsonString(p,key))return false; ws(p);
			if(p.p>=p.end||*p.p++!=':'){fail(p.error,"JSON object key requires ':'");return false;}
			JValue value; if(!jsonValue(p,value,depth+1))return false; out.object.push_back({key,std::move(value)}); ws(p);
			if(p.p<p.end&&*p.p=='}'){p.p++;return true;} if(p.p>=p.end||*p.p++!=','){fail(p.error,"JSON object requires ',' or '}'");return false;}
		}
	}
	if (*p.p == '[') {
		out.type=J_ARRAY; p.p++; ws(p); if(p.p<p.end&&*p.p==']'){p.p++;return true;}
		for (;;) { JValue value; if(!jsonValue(p,value,depth+1))return false; out.array.push_back(std::move(value)); ws(p);
			if(p.p<p.end&&*p.p==']'){p.p++;return true;} if(p.p>=p.end||*p.p++!=','){fail(p.error,"JSON array requires ',' or ']'");return false;}
		}
	}
	if (p.end-p.p>=4 && !memcmp(p.p,"null",4)) { out.type=J_NULL;p.p+=4;return true; }
	if (p.end-p.p>=4 && !memcmp(p.p,"true",4)) { out.type=J_BOOL;out.boolean=true;p.p+=4;return true; }
	if (p.end-p.p>=5 && !memcmp(p.p,"false",5)) { out.type=J_BOOL;out.boolean=false;p.p+=5;return true; }
	const char *start=p.p; if(*p.p=='-')p.p++; if(p.p>=p.end){fail(p.error,"JSON number is invalid");return false;}
	if(*p.p=='0')p.p++; else if(*p.p>='1'&&*p.p<='9')while(p.p<p.end&&*p.p>='0'&&*p.p<='9')p.p++; else {fail(p.error,"JSON value is invalid");return false;}
	if(p.p<p.end&&*p.p=='.'){p.p++;if(p.p>=p.end||*p.p<'0'||*p.p>'9'){fail(p.error,"JSON number is invalid");return false;}while(p.p<p.end&&*p.p>='0'&&*p.p<='9')p.p++;}
	if(p.p<p.end&&(*p.p=='e'||*p.p=='E')){p.p++;if(p.p<p.end&&(*p.p=='+'||*p.p=='-'))p.p++;if(p.p>=p.end||*p.p<'0'||*p.p>'9'){fail(p.error,"JSON number is invalid");return false;}while(p.p<p.end&&*p.p>='0'&&*p.p<='9')p.p++;}
	std::string token(start,p.p); char *after=nullptr; out.number=strtod(token.c_str(),&after); out.type=J_NUMBER;
	if(!after||*after||!std::isfinite(out.number)){fail(p.error,"JSON number must be finite");return false;} return true;
}

static bool parseJson(const char *text,size_t len,JValue &out,Error &error)
{
	if(!text||!len||len>8*1024*1024){fail(error,"effect graph size is invalid");return false;}
	JsonParser p={text,text+len,error}; if(!jsonValue(p,out,0))return false; ws(p);
	if(p.p!=p.end){fail(error,"effect graph has trailing content");return false;} return true;
}

static const JValue *field(const JValue &v,const char *name,Error &e,const char *where,bool required=true)
{
	const JValue *found=nullptr; for(const auto &kv:v.object)if(kv.first==name){if(found){fail(e,"%s has duplicate field '%s'",where,name);return nullptr;}found=&kv.second;}
	if(!found&&required)fail(e,"%s is missing field '%s'",where,name); return found;
}

static bool exactObject(const JValue &v,const char *where,const char *const *keys,size_t count,Error &e)
{
	if(v.type!=J_OBJECT){fail(e,"%s must be an object",where);return false;}
	for(const auto &kv:v.object){size_t i=0;for(;i<count;i++)if(kv.first==keys[i])break;if(i==count){fail(e,"%s has unknown field '%s'",where,kv.first.c_str());return false;}}
	for(size_t i=0;i<count;i++)if(!field(v,keys[i],e,where))return false; return !e.failed;
}

static bool stringIs(const JValue *v,const char *expected,const char *where,Error &e)
{ if(!v||v->type!=J_STRING){fail(e,"%s must be a string",where);return false;} if(expected&&v->string!=expected){fail(e,"%s must be '%s'",where,expected);return false;}return true; }
static bool number(const JValue *v,double lo,double hi,bool integer,const char *where,Error &e)
{ if(!v||v->type!=J_NUMBER||!std::isfinite(v->number)){fail(e,"%s must be a finite number",where);return false;}if(v->number<lo||v->number>hi||(integer&&floor(v->number)!=v->number)){fail(e,"%s is out of range",where);return false;}return true; }
static bool validCatalog(const std::string &s)
{ return pdEffectCatalogIdValid(s.c_str()) != 0; }
static bool validMember(const std::string &s)
{ if(s.empty()||s[0]=='/'||s[0]=='\\'||s.find("::")!=std::string::npos||s.find('\\')!=std::string::npos)return false;size_t pos=0;while(pos<=s.size()){size_t n=s.find('/',pos);std::string seg=s.substr(pos,n==std::string::npos?s.size()-pos:n-pos);if(seg.empty()||seg=="."||seg=="..")return false;if(n==std::string::npos)break;pos=n+1;}return true; }
static bool color(const JValue *v,size_t digits,const char *where,Error &e)
{ if(!v||v->type!=J_STRING||v->string.size()!=digits+1||v->string[0]!='#'){fail(e,"%s must be #%zu-hex",where,digits);return false;}for(size_t i=1;i<v->string.size();i++)if(!isxdigit((unsigned char)v->string[i])){fail(e,"%s must be hexadecimal",where);return false;}return true; }

static bool exactStrings(const JValue &v,const char *where,const char *const *values,size_t count,Error &e)
{
	if(v.type!=J_ARRAY||v.array.size()!=count){fail(e,"%s must contain exactly %zu entries",where,count);return false;}
	for(size_t i=0;i<count;i++)if(!stringIs(&v.array[i],values[i],where,e))return false;return true;
}

static bool validateCoverage(const JValue &v,Error &e)
{
	static const char *keys[]={"native_particle_systems","absent_independent_tables"};
	static const char *native[]={"explosion","spark","smoke"}; static const char *absent[]={"beam","screen"};
	return exactObject(v,"base_module_coverage",keys,2,e)&&exactStrings(*field(v,keys[0],e,"base_module_coverage"),keys[0],native,3,e)&&exactStrings(*field(v,keys[1],e,"base_module_coverage"),keys[1],absent,2,e);
}

static bool validateProvenance(const JValue &v,Error &e)
{
	static const char *keys[]={"stored","source_derived","callsite_owned"}; static const char *owned[]={"target","attachment","priority","effect_enable","owner","scorch_enable"};
	if(!exactObject(v,"field_provenance",keys,3,e))return false;
	if(!stringIs(field(v,"stored",e,"field_provenance"),"values copied field-for-field from the native base table","field_provenance.stored",e))return false;
	if(!stringIs(field(v,"source_derived",e,"field_provenance"),"fixed behavior proven by the native production consumer","field_provenance.source_derived",e))return false;
	return exactStrings(*field(v,"callsite_owned",e,"field_provenance"),"field_provenance.callsite_owned",owned,6,e);
}

static bool validateExactStringObject(const JValue &v,const char *where,const char *k0,const char *v0,const char *k1,const char *v1,Error &e)
{ const char *keys[]={k0,k1};return exactObject(v,where,keys,2,e)&&stringIs(field(v,k0,e,where),v0,k0,e)&&stringIs(field(v,k1,e,where),v1,k1,e); }

static bool validateProfile(const JValue &row,pd_effect_profile_kind_t kind,size_t expected_index,pd_effect_source_info_t &info,Error &e)
{
	const char *id=kind==PD_EFFECT_PROFILE_EXPLOSION?pdEffectSourceExplosionProfileId(expected_index):kind==PD_EFFECT_PROFILE_SPARK?pdEffectSourceSparkProfileId(expected_index):pdEffectSourceSmokeProfileId(expected_index);
	const char *module=kind==PD_EFFECT_PROFILE_EXPLOSION?"effect.explosion":kind==PD_EFFECT_PROFILE_SPARK?"effect.spark":"effect.smoke";
	static const char *ek[]={"id","module","stored","smoke_profile","audio_catalog_id","source_derived"}; static const char *sk[]={"id","module","stored","source_derived"};
	if(!exactObject(row,"profile",kind==PD_EFFECT_PROFILE_EXPLOSION?ek:sk,kind==PD_EFFECT_PROFILE_EXPLOSION?6:4,e)||!stringIs(field(row,"id",e,"profile"),id,"profile.id",e)||!stringIs(field(row,"module",e,"profile"),module,"profile.module",e))return false;
	const JValue *stored=field(row,"stored",e,"profile");
	if(kind==PD_EFFECT_PROFILE_EXPLOSION){
		static const char *keys[]={"range_h","range_v","change_rate_h","change_rate_v","inner_size","blast_radius","damage_radius","duration_ticks","propagation_rate","flare_speed","damage"};
		if(!exactObject(*stored,"profile.stored",keys,11,e))return false;for(size_t i=0;i<11;i++)if(!number(field(*stored,keys[i],e,"profile.stored"),i==7||i==8?-32768:-1000000,i==7||i==8?32767:1000000,i==7||i==8,keys[i],e))return false;
		const JValue *smoke=field(row,"smoke_profile",e,"profile");if(!smoke||smoke->type!=J_STRING){fail(e,"smoke_profile must be a string");return false;}bool smoke_ok=false;for(size_t i=0;pdEffectSourceSmokeProfileId(i);i++)if(smoke->string==pdEffectSourceSmokeProfileId(i))smoke_ok=true;if(!smoke_ok){fail(e,"smoke_profile is unknown");return false;}
		const JValue *audio=field(row,"audio_catalog_id",e,"profile");if(audio->type==J_NULL)info.silent_audio_rows++;else if(audio->type==J_STRING&&validCatalog(audio->string))info.has_audio_rows++;else{fail(e,"audio_catalog_id must be a catalog-ID string or null");return false;}
		const JValue *derived=field(row,"source_derived",e,"profile");static const char *dk[]={"renderer","light","camera_shake","smoke_spawn","scorch"};if(!exactObject(*derived,"profile.source_derived",dk,5,e))return false;
		static const char *rk[]={"backend","max_parts","part_size_random_multiplier","cleanup_flare_speed_multiplier"};const JValue *renderer=field(*derived,"renderer",e,"source_derived");if(!exactObject(*renderer,"renderer",rk,4,e)||!stringIs(field(*renderer,"backend",e,"renderer"),"native_explosion_parts","renderer.backend",e)||!number(field(*renderer,"max_parts",e,"renderer"),40,40,true,"renderer.max_parts",e)||!number(field(*renderer,"cleanup_flare_speed_multiplier",e,"renderer"),16,16,false,"renderer.cleanup",e))return false;const JValue *mult=field(*renderer,"part_size_random_multiplier",e,"renderer");if(!mult||mult->type!=J_ARRAY||mult->array.size()!=2||!number(&mult->array[0],1,1,false,"renderer.multiplier[0]",e)||!number(&mult->array[1],1.5,1.5,false,"renderer.multiplier[1]",e))return false;
		static const char *lk[]={"mode","radius_field","intensity"};const JValue *light=field(*derived,"light",e,"source_derived");if(!exactObject(*light,"light",lk,3,e)||!stringIs(field(*light,"mode",e,"light"),"room_flash","light.mode",e)||!stringIs(field(*light,"radius_field",e,"light"),"range_h","light.radius_field",e)||!number(field(*light,"intensity",e,"light"),255,255,true,"light.intensity",e))return false;
		static const char *ck[]={"duration_ticks","excluded_profiles","intensity_formula"};const JValue *camera=field(*derived,"camera_shake",e,"source_derived");static const char *excluded[]={"bullet_hole","impact_flame_alt"};if(!exactObject(*camera,"camera_shake",ck,3,e)||!number(field(*camera,"duration_ticks",e,"camera_shake"),12,12,true,"camera.duration",e)||!exactStrings(*field(*camera,"excluded_profiles",e,"camera_shake"),"camera.excluded",excluded,2,e)||!stringIs(field(*camera,"intensity_formula",e,"camera_shake"),"inner_size / distance * 15","camera.formula",e))return false;
		static const char *mk[]={"gas_barrel_tick","other_profile_tick_formula","bullet_hole_probability"};const JValue *spawn=field(*derived,"smoke_spawn",e,"source_derived");if(!exactObject(*spawn,"smoke_spawn",mk,3,e)||!number(field(*spawn,"gas_barrel_tick",e,"smoke_spawn"),15,15,true,"smoke_spawn.tick",e)||!stringIs(field(*spawn,"other_profile_tick_formula",e,"smoke_spawn"),"duration_ticks - 20","smoke_spawn.formula",e)||!number(field(*spawn,"bullet_hole_probability",e,"smoke_spawn"),.5,.5,false,"smoke_spawn.probability",e))return false;
		static const char *qk[]={"spawn_tick_formula","size_formula"};const JValue *scorch=field(*derived,"scorch",e,"source_derived");if(!exactObject(*scorch,"scorch",qk,2,e)||!stringIs(field(*scorch,"spawn_tick_formula",e,"scorch"),"duration_ticks / 2","scorch.spawn",e)||!stringIs(field(*scorch,"size_formula",e,"scorch"),"min(2 * inner_size, 100) * random(0.8, 1.0)","scorch.size",e))return false;
	}else if(kind==PD_EFFECT_PROFILE_SPARK){
		static const char *keys[]={"speed_random_range","origin_offset_scale","streak_width_base","streak_length_base","streak_width_growth_per_tick","streak_length_growth_per_tick","gravity_per_tick","max_age_ticks","fade_start_tick","spark_count","flags","color_start_rgba","color_end_rgba","deceleration"};if(!exactObject(*stored,"profile.stored",keys,14,e))return false;
		for(size_t i=0;i<14;i++){const JValue *v=field(*stored,keys[i],e,"profile.stored");if(i==11||i==12){if(!color(v,8,keys[i],e))return false;}else if(i==6||i==13){if(!number(v,-1000000,1000000,false,keys[i],e))return false;}else if(i==1){if(!number(v,-32768,32767,true,keys[i],e))return false;}else if(i==10){if(!number(v,0,4294967295.0,true,keys[i],e))return false;}else if(!number(v,0,65535,true,keys[i],e))return false;}
		if(!validateExactStringObject(*field(row,"source_derived",e,"profile"),"profile.source_derived","renderer","native_spark_streak","motion","native_spark_ballistics",e))return false;
	}else{
		static const char *keys[]={"duration_ticks","fade_speed","spread_interval_ticks","initial_size","background_rotation_speed","color_rgb","foreground_rotation_speed","cloud_count","size_growth_per_tick","rise_per_tick","drift_radius"};if(!exactObject(*stored,"profile.stored",keys,11,e))return false;
		for(size_t i=0;i<11;i++){const JValue *v=field(*stored,keys[i],e,"profile.stored");if(i==5){if(!color(v,6,keys[i],e))return false;}else if(i==4||i==6||i>=8){if(!number(v,-1000000,1000000,false,keys[i],e))return false;}else if(!number(v,-32768,32767,true,keys[i],e))return false;}
		if(!validateExactStringObject(*field(row,"source_derived",e,"profile"),"profile.source_derived","renderer","native_smoke_billboard","motion","native_smoke_drift",e))return false;
	}
	return !e.failed;
}

static bool parseDescriptor(const char *text,size_t len,pd_effect_source_info_t &info,std::string &schema,Error &e)
{
	if(!text||!len||len>64*1024){fail(e,"effect descriptor size is invalid");return false;}std::vector<std::pair<std::string,std::string> > fields;std::string section;
	size_t pos=0;while(pos<len){size_t end=pos;while(end<len&&text[end]!='\n'&&text[end]!='\r')end++;std::string line(text+pos,end-pos);while(end<len&&(text[end]=='\n'||text[end]=='\r'))end++;pos=end;
		size_t a=line.find_first_not_of(" \t"),b=line.find_last_not_of(" \t");if(a==std::string::npos||line[a]==';'||line[a]=='#')continue;line=line.substr(a,b-a+1);
		if(line.front()=='['&&line.back()==']'){if(!section.empty()){fail(e,"effect descriptor has multiple sections");return false;}section=line.substr(1,line.size()-2);continue;}
		size_t eq=line.find('=');if(eq==std::string::npos){fail(e,"effect descriptor line requires '='");return false;}std::string k=line.substr(0,eq),v=line.substr(eq+1);a=k.find_first_not_of(" \t");b=k.find_last_not_of(" \t");k=k.substr(a,b-a+1);a=v.find_first_not_of(" \t");v=a==std::string::npos?"":v.substr(a,v.find_last_not_of(" \t")-a+1);for(const auto &p:fields)if(p.first==k){fail(e,"effect descriptor has duplicate field '%s'",k.c_str());return false;}fields.push_back({k,v});}
	if(section!="effect"){fail(e,"effect descriptor requires one [effect] section");return false;}
	auto get=[&](const char *k,bool req=true)->std::string{for(const auto &p:fields)if(p.first==k)return p.second;if(req)fail(e,"effect descriptor is missing '%s'",k);return {};};
	schema=get("schema",false);bool v2=schema==PD_EFFECT_SOURCE_SCHEMA_V2;if(!schema.empty()&&!v2&&schema!=PD_EFFECT_SOURCE_SCHEMA_V1){fail(e,"effect descriptor schema is unsupported");return false;}
	static const char *v2keys[]={"catalog_id","name","schema","profile_kind","target_policy","attachment_policy","priority_policy","scorch_policy","effect_file"};
	static const char *v1keys[]={"catalog_id","name","schema","effect_key","target_key","effect_file","timeline_file","shader_id","intensity"};const char *const *allowed=v2?v2keys:v1keys;size_t count=v2?9:9;
	for(const auto &p:fields){size_t i=0;for(;i<count;i++)if(p.first==allowed[i])break;if(i==count){fail(e,"effect descriptor has unknown field '%s'",p.first.c_str());return false;}}
	std::string id=get("catalog_id"),name=get("name"),member=get("effect_file",v2);if(!validCatalog(id)){fail(e,"catalog_id must be a valid catalog ID");return false;}if(name.empty()){fail(e,"effect name must not be empty");return false;}if(!member.empty()&&!validMember(member)){fail(e,"effect_file must be a safe relative archive member");return false;}
	snprintf(info.catalog_id,sizeof(info.catalog_id),"%s",id.c_str());snprintf(info.name,sizeof(info.name),"%s",name.c_str());snprintf(info.effect_file,sizeof(info.effect_file),"%s",member.c_str());info.intensity=1.0f;
	if(v2){std::string kind=get("profile_kind");if(kind=="explosion")info.profile_kind=PD_EFFECT_PROFILE_EXPLOSION;else if(kind=="spark")info.profile_kind=PD_EFFECT_PROFILE_SPARK;else if(kind=="smoke")info.profile_kind=PD_EFFECT_PROFILE_SMOKE;else{fail(e,"profile_kind is invalid");return false;}for(const char *k:{"target_policy","attachment_policy","priority_policy","scorch_policy"})if(get(k)!="callsite"){fail(e,"%s must be 'callsite'",k);return false;}info.format=PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY;
	}else{info.format=PD_EFFECT_SOURCE_FORMAT_LEGACY_GRAPH;std::string effect=get("effect_key",false),target=get("target_key",false),timeline=get("timeline_file",false),shader=get("shader_id",false),intensity=get("intensity",false);if(!timeline.empty()&&!validMember(timeline)){fail(e,"timeline_file must be a safe relative archive member");return false;}if(member.empty()&&timeline.empty()){fail(e,"legacy effect requires effect_file, timeline_file, or both");return false;}snprintf(info.effect_key,sizeof(info.effect_key),"%s",effect.c_str());snprintf(info.target_key,sizeof(info.target_key),"%s",target.c_str());snprintf(info.timeline_file,sizeof(info.timeline_file),"%s",timeline.c_str());snprintf(info.shader_id,sizeof(info.shader_id),"%s",shader.c_str());if(!intensity.empty()){char *after=nullptr;double n=strtod(intensity.c_str(),&after);if(!after||*after||!std::isfinite(n)||n<0||n>1000000){fail(e,"intensity is invalid");return false;}info.intensity=(float)n;}}
	return !e.failed;
}

static bool validateGraph(const JValue &root,const std::string &descriptor_schema,pd_effect_source_info_t &info,Error &e)
{
	if(root.type!=J_OBJECT){fail(e,"effect graph root must be an object");return false;}
	const JValue *schema=field(root,"schema",e,"effect graph root");if(!schema||schema->type!=J_STRING){fail(e,"effect graph schema must be a string");return false;}
	if(info.format==PD_EFFECT_SOURCE_FORMAT_LEGACY_GRAPH){if(schema->string!=PD_EFFECT_SOURCE_SCHEMA_V1&&schema->string!="pd2.effect.graph.v1"){fail(e,"legacy effect graph schema is unsupported");return false;}if(!descriptor_schema.empty()&&descriptor_schema!=schema->string){fail(e,"descriptor and graph schema do not match");return false;}const JValue *id=field(root,"asset_id",e,"effect graph root",false);const JValue *catalog=field(root,"catalog_id",e,"effect graph root",false);if((id&&catalog)||(!id&&!catalog)){fail(e,"legacy effect graph requires exactly one asset identity");return false;}const JValue *identity=id?id:catalog;if(identity->type!=J_STRING||identity->string!=info.catalog_id){fail(e,"legacy effect graph identity does not match descriptor catalog_id");return false;}return true;}
	static const char *keys[]={"schema","catalog_id","program_kind","profile_kind","base_module_coverage","field_provenance","profiles"};if(!exactObject(root,"effect graph root",keys,7,e)||!stringIs(schema,PD_EFFECT_SOURCE_SCHEMA_V2,"schema",e)||!stringIs(field(root,"catalog_id",e,"effect graph root"),info.catalog_id,"catalog_id",e)||!stringIs(field(root,"program_kind",e,"effect graph root"),"profile_library","program_kind",e))return false;
	const char *kind=info.profile_kind==PD_EFFECT_PROFILE_EXPLOSION?"explosion":info.profile_kind==PD_EFFECT_PROFILE_SPARK?"spark":"smoke";if(!stringIs(field(root,"profile_kind",e,"effect graph root"),kind,"profile_kind",e)||!validateCoverage(*field(root,"base_module_coverage",e,"effect graph root"),e)||!validateProvenance(*field(root,"field_provenance",e,"effect graph root"),e))return false;
	const JValue *profiles=field(root,"profiles",e,"effect graph root");size_t required=info.profile_kind==PD_EFFECT_PROFILE_EXPLOSION?26:info.profile_kind==PD_EFFECT_PROFILE_SPARK?27:23;if(!profiles||profiles->type!=J_ARRAY||profiles->array.size()!=required){fail(e,"profiles must contain exactly %zu rows",required);return false;}for(size_t i=0;i<required;i++)if(!validateProfile(profiles->array[i],info.profile_kind,i,info,e))return false;info.profile_count=required;return true;
}

static s32 parseBoth(const char *descriptor,size_t descriptor_len,const char *graph,size_t graph_len,const char *expected,pd_effect_source_info_t *out,char *error,size_t error_cap)
{
	if(error&&error_cap)error[0]='\0';Error e={error,error_cap,false};pd_effect_source_info_t info={};std::string descriptor_schema;if(!parseDescriptor(descriptor,descriptor_len,info,descriptor_schema,e))return 0;if(expected&&strcmp(expected,info.catalog_id)){fail(e,"catalog_id '%s' does not match expected '%s'",info.catalog_id,expected);return 0;}if(info.effect_file[0]){JValue root;if(!parseJson(graph,graph_len,root,e)||!validateGraph(root,descriptor_schema,info,e))return 0;}if(out)*out=info;return 1;
}

static u32 parseHexColor(const std::string &text)
{
	return (u32)strtoul(text.c_str() + 1, nullptr, 16);
}

static bool decodeProfiles(const JValue &root,
	const pd_effect_source_info_t &source, pd_effect_profile_library_t &out,
	Error &e)
{
	const JValue *profiles=field(root,"profiles",e,"effect graph root");
	if(!profiles||profiles->type!=J_ARRAY)return false;
	out.kind=source.profile_kind;out.count=profiles->array.size();
	if(out.kind==PD_EFFECT_PROFILE_EXPLOSION){
		out.explosions=(pd_effect_explosion_profile_t*)calloc(out.count,sizeof(*out.explosions));if(!out.explosions){fail(e,"out of memory decoding explosion profiles");return false;}
		for(size_t i=0;i<out.count;i++){const JValue &r=profiles->array[i];const JValue *s=field(r,"stored",e,"profile");auto &d=out.explosions[i];
			snprintf(d.id,sizeof(d.id),"%s",field(r,"id",e,"profile")->string.c_str());
			d.range_h=(float)field(*s,"range_h",e,"stored")->number;d.range_v=(float)field(*s,"range_v",e,"stored")->number;d.change_rate_h=(float)field(*s,"change_rate_h",e,"stored")->number;d.change_rate_v=(float)field(*s,"change_rate_v",e,"stored")->number;d.inner_size=(float)field(*s,"inner_size",e,"stored")->number;d.blast_radius=(float)field(*s,"blast_radius",e,"stored")->number;d.damage_radius=(float)field(*s,"damage_radius",e,"stored")->number;d.duration_ticks=(s32)field(*s,"duration_ticks",e,"stored")->number;d.propagation_rate=(s32)field(*s,"propagation_rate",e,"stored")->number;d.flare_speed=(float)field(*s,"flare_speed",e,"stored")->number;d.damage=(float)field(*s,"damage",e,"stored")->number;
			snprintf(d.smoke_profile,sizeof(d.smoke_profile),"%s",field(r,"smoke_profile",e,"profile")->string.c_str());const JValue *a=field(r,"audio_catalog_id",e,"profile");d.has_audio=a->type==J_STRING;if(d.has_audio)snprintf(d.audio_catalog_id,sizeof(d.audio_catalog_id),"%s",a->string.c_str());
		}
	}else if(out.kind==PD_EFFECT_PROFILE_SPARK){
		out.sparks=(pd_effect_spark_profile_t*)calloc(out.count,sizeof(*out.sparks));if(!out.sparks){fail(e,"out of memory decoding spark profiles");return false;}
		for(size_t i=0;i<out.count;i++){const JValue &r=profiles->array[i];const JValue *s=field(r,"stored",e,"profile");auto &d=out.sparks[i];snprintf(d.id,sizeof(d.id),"%s",field(r,"id",e,"profile")->string.c_str());
			d.speed_random_range=(u32)field(*s,"speed_random_range",e,"stored")->number;d.origin_offset_scale=(s32)field(*s,"origin_offset_scale",e,"stored")->number;d.streak_width_base=(u32)field(*s,"streak_width_base",e,"stored")->number;d.streak_length_base=(u32)field(*s,"streak_length_base",e,"stored")->number;d.streak_width_growth_per_tick=(u32)field(*s,"streak_width_growth_per_tick",e,"stored")->number;d.streak_length_growth_per_tick=(u32)field(*s,"streak_length_growth_per_tick",e,"stored")->number;d.gravity_per_tick=(float)field(*s,"gravity_per_tick",e,"stored")->number;d.max_age_ticks=(u32)field(*s,"max_age_ticks",e,"stored")->number;d.fade_start_tick=(u32)field(*s,"fade_start_tick",e,"stored")->number;d.spark_count=(u32)field(*s,"spark_count",e,"stored")->number;d.flags=(u32)field(*s,"flags",e,"stored")->number;d.color_start_rgba=parseHexColor(field(*s,"color_start_rgba",e,"stored")->string);d.color_end_rgba=parseHexColor(field(*s,"color_end_rgba",e,"stored")->string);d.deceleration=(float)field(*s,"deceleration",e,"stored")->number;
		}
	}else if(out.kind==PD_EFFECT_PROFILE_SMOKE){
		out.smokes=(pd_effect_smoke_profile_t*)calloc(out.count,sizeof(*out.smokes));if(!out.smokes){fail(e,"out of memory decoding smoke profiles");return false;}
		for(size_t i=0;i<out.count;i++){const JValue &r=profiles->array[i];const JValue *s=field(r,"stored",e,"profile");auto &d=out.smokes[i];snprintf(d.id,sizeof(d.id),"%s",field(r,"id",e,"profile")->string.c_str());
			d.duration_ticks=(s32)field(*s,"duration_ticks",e,"stored")->number;d.fade_speed=(s32)field(*s,"fade_speed",e,"stored")->number;d.spread_interval_ticks=(s32)field(*s,"spread_interval_ticks",e,"stored")->number;d.initial_size=(s32)field(*s,"initial_size",e,"stored")->number;d.background_rotation_speed=(float)field(*s,"background_rotation_speed",e,"stored")->number;u32 rgb=parseHexColor(field(*s,"color_rgb",e,"stored")->string);d.color_r=(u8)(rgb>>16);d.color_g=(u8)(rgb>>8);d.color_b=(u8)rgb;d.foreground_rotation_speed=(float)field(*s,"foreground_rotation_speed",e,"stored")->number;d.cloud_count=(s32)field(*s,"cloud_count",e,"stored")->number;d.size_growth_per_tick=(float)field(*s,"size_growth_per_tick",e,"stored")->number;d.rise_per_tick=(float)field(*s,"rise_per_tick",e,"stored")->number;d.drift_radius=(float)field(*s,"drift_radius",e,"stored")->number;
		}
	}else{fail(e,"profile library kind is invalid");return false;}
	return !e.failed;
}

static bool validTimelineProperty(const std::string &value)
{
	if(value.empty()||value.size()>=64)return false;
	for(unsigned char c:value)if(!(isalnum(c)||c=='_'||c=='.'||c=='-'))return false;
	return true;
}

}

extern "C" s32 pdEffectSourceParse(const char *descriptor,size_t descriptor_len,const char *graph,size_t graph_len,const char *expected,pd_effect_source_info_t *out,char *error,size_t error_cap)
{ return parseBoth(descriptor,descriptor_len,graph,graph_len,expected,out,error,error_cap); }

extern "C" s32 pdEffectSourceParseArchiveBytes(const void *bytes,u32 size,const char *expected,pd_effect_source_info_t *out,char *error,size_t error_cap)
{
	if(error&&error_cap)error[0]='\0';u32 dlen=0,glen=0;const char *descriptor_name=nullptr;char *descriptor=assetArchiveExtractDescriptorMemAlloc(bytes,size,"archive.pdeffect",ASSET_ARCHIVE_VALIDATE_MIGRATION,&dlen,&descriptor_name);if(!descriptor){if(error&&error_cap)snprintf(error,error_cap,"effect archive has no unique public descriptor");return 0;}
	pd_effect_source_info_t preliminary={};Error e={error,error_cap,false};std::string schema;if(!parseDescriptor(descriptor,dlen,preliminary,schema,e)){free(descriptor);return 0;}void *graph=nullptr;if(preliminary.effect_file[0]){graph=modArchiveExtractMemAlloc(bytes,size,preliminary.effect_file,&glen);if(!graph||!glen){free(descriptor);free(graph);if(error&&error_cap)snprintf(error,error_cap,"effect archive is missing declared member '%s'",preliminary.effect_file);return 0;}}s32 ok=parseBoth(descriptor,dlen,(const char*)graph,glen,expected,out,error,error_cap);free(descriptor);free(graph);return ok;
}

extern "C" s32 pdEffectSourceParseArchiveFile(const char *path,const char *expected,pd_effect_source_info_t *out,char *error,size_t error_cap)
{
	if(error&&error_cap)error[0]='\0';if(!path||!path[0]){if(error&&error_cap)snprintf(error,error_cap,"effect archive path is empty");return 0;}u32 length=0;void *bytes=nullptr;
	/* Ordinary absolute/local paths must not depend on fsInit (unit tools and
	 * creator validation use them before game boot). Archive-qualified nested
	 * paths fall through to fsFileLoad, which understands the :: chain. */
	if(!strstr(path,"::")){FILE *fp=fopen(path,"rb");if(fp){if(fseek(fp,0,SEEK_END)==0){long n=ftell(fp);if(n>0&&n<=64*1024*1024&&fseek(fp,0,SEEK_SET)==0){bytes=malloc((size_t)n);if(bytes&&fread(bytes,1,(size_t)n,fp)==(size_t)n)length=(u32)n;else{free(bytes);bytes=nullptr;}}}fclose(fp);}}
	bool sysmem_owned=false;if(!bytes){bytes=fsFileLoad(path,&length);sysmem_owned=bytes!=nullptr;}if(!bytes||!length||length>64*1024*1024){if(sysmem_owned)sysMemFree(bytes);else free(bytes);if(error&&error_cap)snprintf(error,error_cap,"cannot load effect archive or size is invalid");return 0;}s32 ok=pdEffectSourceParseArchiveBytes(bytes,length,expected,out,error,error_cap);if(sysmem_owned)sysMemFree(bytes);else free(bytes);return ok;
}

extern "C" void pdEffectSourceFreeProfileLibrary(pd_effect_profile_library_t *library)
{
	if(!library)return;free(library->rows);memset(library,0,sizeof(*library));
}

extern "C" s32 pdEffectSourceDecodeProfileLibrary(const char *graph,size_t graph_len,const pd_effect_source_info_t *source,pd_effect_profile_library_t *out,char *error,size_t error_cap)
{
	if(error&&error_cap)error[0]='\0';if(!source||!out||source->format!=PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY){if(error&&error_cap)snprintf(error,error_cap,"profile decoder requires validated v2 source");return 0;}memset(out,0,sizeof(*out));Error e={error,error_cap,false};JValue root;pd_effect_source_info_t checked=*source;checked.profile_count=checked.has_audio_rows=checked.silent_audio_rows=0;if(!parseJson(graph,graph_len,root,e)||!validateGraph(root,PD_EFFECT_SOURCE_SCHEMA_V2,checked,e)||!decodeProfiles(root,checked,*out,e)){pdEffectSourceFreeProfileLibrary(out);return 0;}return 1;
}

extern "C" void pdEffectTimelineFree(pd_effect_timeline_t *timeline)
{
	if(!timeline)return;free(timeline->keys);memset(timeline,0,sizeof(*timeline));
}

extern "C" s32 pdEffectTimelineParse(const char *json,size_t json_len,pd_effect_timeline_t *out,char *error,size_t error_cap)
{
	if(error&&error_cap)error[0]='\0';if(!out){if(error&&error_cap)snprintf(error,error_cap,"timeline output is null");return 0;}memset(out,0,sizeof(*out));Error e={error,error_cap,false};JValue root;if(!parseJson(json,json_len,root,e))return 0;static const char *rootKeys[]={"schema","tracks"};if(!exactObject(root,"effect timeline",rootKeys,2,e)||!stringIs(field(root,"schema",e,"effect timeline"),"pd2.effect.timeline.v1","timeline.schema",e))return 0;const JValue *tracks=field(root,"tracks",e,"effect timeline");if(!tracks||tracks->type!=J_ARRAY||tracks->array.empty()){fail(e,"timeline tracks must be a non-empty array");return 0;}out->keys=(pd_effect_timeline_key_t*)calloc(tracks->array.size(),sizeof(*out->keys));if(!out->keys){fail(e,"out of memory decoding timeline");return 0;}out->count=tracks->array.size();static const char *trackKeys[]={"time","property","value"};for(size_t i=0;i<out->count;i++){const JValue &track=tracks->array[i];if(!exactObject(track,"timeline track",trackKeys,3,e))goto fail_timeline;const JValue *time=field(track,"time",e,"timeline track"),*property=field(track,"property",e,"timeline track"),*value=field(track,"value",e,"timeline track");if(!number(time,0,1000000,false,"timeline.time",e)||!number(value,-1000000,1000000,false,"timeline.value",e)||!property||property->type!=J_STRING||!validTimelineProperty(property->string)){fail(e,"timeline property is invalid");goto fail_timeline;}for(size_t j=0;j<i;j++)if(out->keys[j].time==(float)time->number&&!strcmp(out->keys[j].property,property->string.c_str())){fail(e,"timeline has duplicate key for property '%s' at time %.9g",property->string.c_str(),time->number);goto fail_timeline;}out->keys[i].time=(float)time->number;out->keys[i].value=(float)value->number;out->keys[i].authored_order=i;snprintf(out->keys[i].property,sizeof(out->keys[i].property),"%s",property->string.c_str());}return 1;
fail_timeline:pdEffectTimelineFree(out);return 0;
}

extern "C" s32 pdEffectTimelineSample(const pd_effect_timeline_t *timeline,const char *property,float time,float *out_value)
{
	if(!timeline||!property||!property[0]||!out_value||!std::isfinite(time))return 0;const pd_effect_timeline_key_t *lo=nullptr,*hi=nullptr;for(size_t i=0;i<timeline->count;i++){const auto *k=&timeline->keys[i];if(strcmp(k->property,property))continue;if(k->time<=time&&(!lo||k->time>lo->time))lo=k;if(k->time>=time&&(!hi||k->time<hi->time))hi=k;}if(!lo&&!hi)return 0;if(!lo)lo=hi;if(!hi)hi=lo;if(lo->time==hi->time){*out_value=lo->value;return 1;}float alpha=(time-lo->time)/(hi->time-lo->time);*out_value=lo->value+(hi->value-lo->value)*alpha;return 1;
}
