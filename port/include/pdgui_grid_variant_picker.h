#ifndef PDGUI_GRID_VARIANT_PICKER_H
#define PDGUI_GRID_VARIANT_PICKER_H

#include <array>
#include <vector>

#include "forge_level_doc.h"
#include "grid_match_sources.h"

struct pdgui_grid_variant_picker_t {
	std::vector<u8> keep;
	std::vector<s32> power_weapon;
};

struct pdgui_grid_variant_selection_t {
	std::vector<u16> omitted;
	std::vector<forge_level_object_t> records;
	std::vector<std::array<char, CATALOG_ID_LEN>> pickup_models;
	std::vector<forge_level_placement_fallback_t> fallbacks;
};

void pdguiBeginGridVariantPicker(pdgui_grid_variant_picker_t &picker,
	const forge_level_doc_t *doc, const grid_match_sources_t &sources);

/* Render against the same document and plan used at Begin, with an empty
 * selection. Placement strings borrow from selection records through Save. */
bool pdguiDrawGridVariantPicker(pdgui_grid_variant_picker_t &picker,
	const forge_level_doc_t *doc, const grid_match_sources_t &sources,
	pdgui_grid_variant_selection_t &selection, char *err, size_t err_cap);

#endif
