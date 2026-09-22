#include "scenario_scene_renderer.h"
#include "system.h"

#include <cstdio>
#include <cstring>

static void usage(const char *argv0)
{
	std::fprintf(stderr,
		"Usage: %s <scenario-id> <scene-path>\n"
		"       %s <scene-path>\n"
		"\n"
		"scene-path may be a GLB/glTF file or archive member path such as\n"
		"data/ntsc-final/scenarios/base_scenario_mp_chicago.pdscenario::scene.glb\n",
		argv0, argv0);
}

int main(int argc, char **argv)
{
	const char *scenario_id = "probe";
	const char *scene_path = nullptr;
	sysInitArgs(argc, (const char **)argv);

	int arg = 1;
	if (arg < argc && std::strcmp(argv[arg], "--debug-scenario-render-probe") == 0) {
		arg++;
	}

	if (argc - arg == 1) {
		scene_path = argv[arg];
	} else if (argc - arg == 2) {
		scenario_id = argv[arg];
		scene_path = argv[arg + 1];
	} else {
		usage(argv[0]);
		return 2;
	}

	scenario_scene_renderer_probe_t probe {};
	if (!scenarioSceneRendererProbeSource(scenario_id, scene_path, &probe)) {
		std::fprintf(stderr,
			"SCENARIO.RENDER.CPU_PROBE: ok=0 scenario=%s source=%s\n",
			scenario_id, scene_path);
		return 1;
	}

	std::printf(
		"SCENARIO.RENDER.CPU_PROBE: ok=1 scenario=%s source=%s "
		"vertices=%zu groups=%zu materials=%zu images=%zu "
		"alpha_textures=%zu alpha_materials=%zu secondary_materials=%zu "
		"instances=%zu selected_scene=%d bounds_min=[%.9g,%.9g,%.9g] "
		"bounds_max=[%.9g,%.9g,%.9g] geometry_sha256=%s\n",
		scenario_id, scene_path, probe.vertices, probe.groups,
		probe.materials, probe.images, probe.alpha_textures,
		probe.alpha_materials, probe.secondary_materials,
		probe.instances, probe.selected_scene,
		static_cast<double>(probe.bounds_min[0]), static_cast<double>(probe.bounds_min[1]), static_cast<double>(probe.bounds_min[2]),
		static_cast<double>(probe.bounds_max[0]), static_cast<double>(probe.bounds_max[1]), static_cast<double>(probe.bounds_max[2]),
		probe.geometry_sha256);
	return 0;
}
