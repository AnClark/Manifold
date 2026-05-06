#include "ChainEngine.hpp"

#include <iostream>
#include <stdexcept>

ChainEngine::ChainEngine(std::vector<std::unique_ptr<Node>>& nodes)
    : nodes_(nodes)
{
    validateChain();
}

void ChainEngine::validateChain() const
{
    size_t i = 0;
    while (i < nodes_.size()) {
        const Node* n = nodes_[i].get();

        if (dynamic_cast<const SourceNode*>(n)) {
            // Expect zero or more StreamProcessorNodes, then exactly one StreamSinkNode
            i++;
            while (i < nodes_.size() &&
                   dynamic_cast<const StreamProcessorNode*>(nodes_[i].get()) &&
                   !dynamic_cast<const StreamSinkNode*>(nodes_[i].get())) {
                i++;
            }
            if (i >= nodes_.size() || !dynamic_cast<const StreamSinkNode*>(nodes_[i].get())) {
                throw std::runtime_error(
                    "ChainEngine: stream segment starting with SourceNode has no terminating StreamSinkNode");
            }
            i++;  // consume the sink
        } else if (dynamic_cast<const AtomicNode*>(n)) {
            i++;
        } else {
            throw std::runtime_error(
                "ChainEngine: node '" + n->name() +
                "' at chain root must be a SourceNode or AtomicNode");
        }
    }
}

void ChainEngine::executeFor(NodeContext& ctx)
{
    size_t i = 0;
    while (i < nodes_.size()) {
        Node* n = nodes_[i].get();

        // NOTE: "dynamic_cast" is used here to determine the node type at runtime.
        //       It returns nullptr if the cast fails, allowing us to safely check the node type.
        if (auto* src = dynamic_cast<SourceNode*>(n)) {
            // Build the stream pipeline for this file
            auto stream = src->create(ctx);
            i++;

            // Wrap through each StreamProcessorNode
            while (i < nodes_.size()) {
                // NOTE: The first "if" statement conforms to C++17's "if with initializer" syntax,
                //       allowing us to declare and initialize "proc" within the statement, just like "for" loops.
                if (auto* proc = dynamic_cast<StreamProcessorNode*>(nodes_[i].get());
                    proc && !dynamic_cast<StreamSinkNode*>(nodes_[i].get())) {
                    stream = proc->wrap(std::move(stream), ctx);
                    i++;
                } else if (auto* sink = dynamic_cast<StreamSinkNode*>(nodes_[i].get())) {
                    sink->consume(std::move(stream), ctx);
                    i++;
                    break;
                } else {
                    // Non-stream node encountered — close the segment
                    break;
                }
            }
        } else if (auto* atomic = dynamic_cast<AtomicNode*>(n)) {
            atomic->execute(ctx);
            i++;
        } else {
            std::cerr << "[ChainEngine] Skipping unknown node type: " << n->name() << "\n";
            i++;
        }
    }
}

void ChainEngine::executeFor(const std::string& input,
                             const std::string& outputDir)
{
    NodeContext ctx;
    ctx.sourcePath = input;
    ctx.outputDir  = outputDir;
    executeFor(ctx);
}

void ChainEngine::processFile(const std::string& input,
                               const std::string& outputDir)
{
    try {
        executeFor(input, outputDir);
    } catch (const std::exception& e) {
        std::cerr << "[ChainEngine] Error processing '" << input
                  << "': " << e.what() << "\n";
    }
}

void ChainEngine::processBatch(const std::vector<std::string>& inputs,
                                const std::string& outputDir)
{
    for (const auto& input : inputs) {
        processFile(input, outputDir);
    }
}

void ChainEngine::processFile(const SndFileInfo& info,
                               const std::string& outputDir)
{
    NodeContext ctx;
    ctx.sourcePath = info.fileName;
    ctx.outputDir  = outputDir;

    // Inject Worker-analysed loudness data into sideband so that
    // LoudnessNormalizeNode can use it directly without re-measuring.
    if (info.isR128ParsedOK) {
        ctx.setSideband("loudness_lufs",      info.lufsI);
        ctx.setSideband("loudness_peak_dbfs", info.maxSamplePeak_dBFS);
    }

    // Inject per-channel DC offsets measured by DcOffsetWorker.
    if (info.isDcOffsetCalculatedOK) {
        ctx.setSideband("dc_offsets", info.dcOffsets);
    }

    try {
        executeFor(ctx);
    } catch (const std::exception& e) {
        std::cerr << "[ChainEngine] Error processing '" << info.fileName
                  << "': " << e.what() << "\n";
    }
}

void ChainEngine::processBatch(const SndFileList& inputs,
                                const std::string& outputDir)
{
    for (const auto& info : inputs) {
        if (info) {
            processFile(*info, outputDir);
        }
    }
}
