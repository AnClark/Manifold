#pragma once

#include "base/Worker.hpp"
#include "pipeline/Node.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/base_nodes/OutputSinkNode.hpp"

class SingleFileProcessorWorker : public IWorker
{
public:
    void setNodeChain(std::vector<std::unique_ptr<Node>>& nodeChain) { nodeChain_ = &nodeChain; }
    void setOutputDir(std::string outputDir) 
    {
        std::scoped_lock<std::mutex> lock(outputDirMutex);
        this->outputDir_ = std::move(outputDir);
    }

protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;

private:
    std::vector<std::unique_ptr<Node>>* nodeChain_ = nullptr;
    std::string outputDir_;

    FileSourceNode sourceNode_;  ///< Implicitly prepended to the user-supplied chain
    OutputSinkNode outputNode_;  ///< Implicitly prepended to the user-supplied chain as well

    std::mutex outputDirMutex;
};
