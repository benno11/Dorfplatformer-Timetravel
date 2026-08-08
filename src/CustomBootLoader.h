#pragma once

#include <string>
#include <vector>

namespace CustomBoot {

struct Result {
    bool ok = false;
    int exitCode = 1;
    std::string selectedPartition;
    std::vector<std::string> bootOptions;
};

Result Run(int argc, char** argv);

} // namespace CustomBoot

