#pragma once

#include "base/Worker.hpp"
#include "pipeline/Node.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/base_nodes/OutputSinkNode.hpp"

class SingleFileProcessorWorker : public IWorker
{
public:
    void setNodeChain(std::vector<std::shared_ptr<Node>>& nodeChain, std::mutex& mutex)
    {
        nodeChain_ = &nodeChain;
        nodeChainMutex_ = &mutex;
    }

    void setOutputDir(std::string outputDir) 
    {
        std::scoped_lock<std::mutex> lock(outputDirMutex);
        this->outputDir_ = std::move(outputDir);
    }

    bool queryIfProcessing() {
        return isProcessing.load();
    }

    void queryProcessingState(std::string& filePath, size_t& nodeIndex, std::string& nodeName)
    {
        std::scoped_lock<std::mutex> lock(stateMutex);
        filePath = currentState.filePath;
        nodeIndex = currentState.nodeIndex;
        nodeName = currentState.nodeName;
    }

    bool queryIfProcessingFile(std::shared_ptr<SndFileInfo> fileInfo)
    {
        return (isProcessing.load() && reinterpret_cast<uintptr_t>(fileInfo.get()) == currentState.currentFileInfoPtr);
    }

protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;

private:
    std::vector<std::shared_ptr<Node>>* nodeChain_ = nullptr;
    std::mutex*                          nodeChainMutex_ = nullptr;
    std::string outputDir_;

    FileSourceNode sourceNode_;  ///< Implicitly prepended to the user-supplied chain
    OutputSinkNode outputNode_;  ///< Implicitly prepended to the user-supplied chain as well

    std::mutex outputDirMutex;

    // ------------------------------------------------------------------------
    // Processing state for the current file. Only accessed by the worker thread, so no mutex needed.

    struct ProcessingState {
        std::string filePath;       // Currently processing file
        size_t      nodeIndex  = 0; // Currently processing node index
        std::string nodeName;       // Currently processing node name
        uintptr_t   currentFileInfoPtr; // Pointer of current SndFileInfo which is being processed (for comparing in queryIfProcessingFile())
    };

    std::atomic<bool> isProcessing{false};

    // Use mutex + struct to protect fine-grained state
    std::mutex              stateMutex;
    ProcessingState         currentState;
};
