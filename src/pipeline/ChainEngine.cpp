#include "ChainEngine.hpp"

#include "utils/LogManager.hpp"
#include <stdexcept>

ChainEngine::ChainEngine(std::vector<Node*> nodes)
    : nodes_(std::move(nodes))
{
    validateChain();
}

void ChainEngine::validateChain() const
{
    size_t i = 0;
    while (i < nodes_.size()) {
        const Node* n = nodes_[i];

        if (dynamic_cast<const SourceNode*>(n)) {
            // Expect zero or more StreamProcessorNodes, then exactly one StreamSinkNode
            i++;
            while (i < nodes_.size() &&
                   dynamic_cast<const StreamProcessorNode*>(nodes_[i]) &&
                   !dynamic_cast<const StreamSinkNode*>(nodes_[i])) {
                i++;
            }
            if (i >= nodes_.size() || !dynamic_cast<const StreamSinkNode*>(nodes_[i])) {
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
    // FIXME: updateProgess() does not show the real progress, because the real processing happens in StreamSinkNode::consume(),
    //        which is called at the end of the segment.
    //        To show real progress, we may need to do more jobs.
    auto updateProgress = [&](size_t idx) {
        if (onProgress)
            onProgress(idx, nodes_[idx]->name());
    };

    size_t i = 0;
    while (i < nodes_.size()) {
        Node* n = nodes_[i];

        // NOTE: "dynamic_cast" is used here to determine the node type at runtime.
        //       It returns nullptr if the cast fails, allowing us to safely check the node type.
        if (auto* src = dynamic_cast<SourceNode*>(n)) {
            updateProgress(i);  // Set progress callback for UI updates (if set)

            // Build the stream pipeline for this file
            auto stream = src->create(ctx);
            ctx.currentFormat = stream->format();  // initialise from source
            i++;

            // Wrap through each StreamProcessorNode
            while (i < nodes_.size()) {
                // NOTE: The first "if" statement conforms to C++17's "if with initializer" syntax,
                //       allowing us to declare and initialize "proc" within the statement, just like "for" loops.
                if (auto* proc = dynamic_cast<StreamProcessorNode*>(nodes_[i]);
                    proc && !dynamic_cast<StreamSinkNode*>(nodes_[i])) {
                    updateProgress(i);  // Set progress callback for UI updates (if set)

                    stream = proc->wrap(std::move(stream), ctx);
                    ctx.currentFormat = stream->format();  // sync after channel/rate-changing wrap
                    i++;
                } else if (auto* sink = dynamic_cast<StreamSinkNode*>(nodes_[i])) {
                    updateProgress(i);  // Set progress callback for UI updates (if set)

                    sink->consume(std::move(stream), ctx);
                    i++;
                    break;
                } else {
                    // Non-stream node encountered — close the segment
                    break;
                }
            }
        } else if (auto* atomic = dynamic_cast<AtomicNode*>(n)) {
            updateProgress(i);  // Set progress callback for UI updates (if set)

            atomic->execute(ctx);
            i++;
        } else {
            LOG_WARNF("ChainEngine", "Skipping unknown node type: %s", n->name().c_str());
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
        LOG_ERRORF("ChainEngine", "DIRECT PROCESSING: Error processing '%s': %s", input.c_str(), e.what());
    }
}

void ChainEngine::processFile(const SndFileInfo& info,
                               const std::string& outputDir)
{
    NodeContext ctx;
    ctx.sourcePath = info.filePath;
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
        LOG_ERRORF("ChainEngine", "Error processing '%s': %s", info.filePath.c_str(), e.what());

        // Report error back to UI via callback (if set)
        if (onError)
            onError(e.what());
    }
}

std::vector<Node*> ChainEngine::toView(const std::vector<std::unique_ptr<Node>>& nodes)
{
    std::vector<Node*> view;
    view.reserve(nodes.size());
    for (const auto& n : nodes)
        view.push_back(n.get());
    return view;
}
