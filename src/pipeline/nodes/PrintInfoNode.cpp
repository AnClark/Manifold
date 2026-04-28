#include "PrintInfoNode.hpp"

#include <iomanip>
#include <iostream>

void PrintInfoNode::execute(NodeContext& ctx)
{
    std::cout << "------------------------------------------------------------\n";
    std::cout << "Source  : " << ctx.sourcePath << "\n";
    std::cout << "Output  : "
              << (ctx.currentFilePath.empty() ? "(none)" : ctx.currentFilePath)
              << "\n";

    std::cout << "Format  : "
              << ctx.sourceFormat.sampleRate << " Hz, "
              << ctx.sourceFormat.channels   << " ch, "
              << ctx.sourceFormat.bitDepth   << "-bit\n";

    auto lufs = ctx.getSideband<double>("loudness_lufs");
    if (lufs.has_value()) {
        std::cout << "Loudness: " << std::fixed << std::setprecision(2)
                  << *lufs << " LUFS-I\n";
    } else {
        std::cout << "Loudness: (not measured)\n";
    }

    auto peak = ctx.getSideband<double>("loudness_peak_dbfs");
    if (peak.has_value()) {
        std::cout << "Peak    : " << std::fixed << std::setprecision(2)
                  << *peak << " dBFS\n";
    }

    // Dump all other sideband keys for diagnostics
    bool hasExtra = false;
    for (const auto& [k, v] : ctx.sideband) {
        if (k == "loudness_lufs" || k == "loudness_peak_dbfs") continue;
        if (!hasExtra) {
            std::cout << "Sideband extras:\n";
            hasExtra = true;
        }
        std::cout << "  " << k << " = <any>\n";
    }

    std::cout << "------------------------------------------------------------\n";
}
