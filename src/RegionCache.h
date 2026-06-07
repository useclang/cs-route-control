#pragma once

#include "ServerRegion.h"

#include <unordered_map>

class RegionCache {
public:
    using RegionMap = std::unordered_map<std::string, ServerRegion>;

    static bool save(const RegionMap &regions);
    static bool load(RegionMap &out);
    static bool hasCache();
};
