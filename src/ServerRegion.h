#pragma once

#include <memory>
#include <string>
#include <vector>

struct ServerRegion {
    std::string                                    code;
    std::string                                    name;
    std::shared_ptr<const std::vector<std::string>> ips;
    int                                            ping   = -1;
    double                                         jitter = 0.0;
};
