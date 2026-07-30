// Standalone debug CLI for O2RMerger, sharing the exact same merge implementation as the
// in-game "Extract ROM" flow (BenPort.cpp) - no reimplementation, no drift.
//
// Usage:
//   merge_o2r_cli <mm.o2r> <oot.o2r> <output-merged.o2r>

#include "../O2RMerger.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <mm.o2r> <oot.o2r> <output-merged.o2r>\n", argv[0]);
        return 1;
    }

    std::string mmPath = argv[1];
    std::string ootPath = argv[2];
    std::string outPath = argv[3];
    std::string error;

    if (!O2RMerger::MergeOotIntoMm(mmPath, ootPath, outPath, &error)) {
        std::fprintf(stderr, "merge failed: %s\n", error.c_str());
        return 1;
    }

    std::printf("merged '%s' + '%s' -> '%s'\n", mmPath.c_str(), ootPath.c_str(), outPath.c_str());
    return 0;
}
