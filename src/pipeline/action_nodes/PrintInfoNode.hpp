#pragma once

#include "../Node.hpp"

/**
 * @brief Atomic node that prints processing results to stdout.
 *
 * Reads ctx.currentFilePath and any sideband entries the user cares about
 * (loudness_lufs, loudness_peak_dbfs) and prints a summary.  Useful for
 * verifying end-to-end sideband communication without a GUI.
 */
class PrintInfoNode : public Node, public AtomicNode {
public:
    std::string id() const override { return "print_info"; }
    std::string name() const override { return "PrintInfo"; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    PortType primaryInput()  const override { return PortType::FilePath; }
    PortType primaryOutput() const override { return PortType::None; }

    void execute(NodeContext& ctx) override;
};
