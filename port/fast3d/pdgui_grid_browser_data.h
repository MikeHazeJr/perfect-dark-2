#ifndef PDGUI_GRID_BROWSER_DATA_H
#define PDGUI_GRID_BROWSER_DATA_H

#include <cstddef>
#include <string>
#include <vector>

enum class GridBrowserSource {
    BuiltIn,
    Original,
    LocalOnly,
    Unverified,
    Installed,
};

struct GridBrowserDependency {
    std::string id;
    std::string name;
    bool available = false;
};

struct GridBrowserRoomFootprint {
    double minX;
    double minZ;
    double maxX;
    double maxZ;
};

struct GridBrowserMetadata {
    std::string idHash;
    std::string parentIdHash;
    std::string parentRevision;
    std::string description;
    std::string dependencyPlan;
    std::string previewLocator;
    std::string thumbnailLocator;
    std::vector<GridBrowserDependency> dependencies;
    GridBrowserSource source = GridBrowserSource::Installed;
    bool hasNativeDescriptor = false;
    bool dependenciesKnown = false;
};

// Main-thread reads for catalog-snapshot preparation. Call only after the
// catalog iteration lock has been released. Browser rows consume the copies.
GridBrowserMetadata gridBrowserReadMetadata(const char *arenaId);

// Prepare an image outside row drawing. Only native PNG members are accepted.
// Keep renderer texture ownership and final preview cache in the browser.
std::vector<unsigned char> gridBrowserReadPreview(const std::string &locator, std::size_t maxBytes);

// Optional top-down bounds from the same native scenario source the map uses.
std::vector<GridBrowserRoomFootprint> gridBrowserReadRoomLayout(const char *arenaId);

#endif
