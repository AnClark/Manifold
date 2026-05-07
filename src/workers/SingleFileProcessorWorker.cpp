#include "SingleFileProcessorWorker.hpp"
#include "pipeline/ChainEngine.hpp"

void SingleFileProcessorWorker::processItem(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    if (!fileInfoInstance || fileInfoInstance->aboutToBeRemoved || shouldCancelProcessing)
        return;
    
    if (!nodeChain_ || nodeChain_->size() <= 0)
        return;

    // Set processing state for UI progress reporting
    {
        std::scoped_lock<std::mutex> lock(stateMutex);
        currentState.fileName = fileInfoInstance->fileName;
        currentState.nodeIndex = 0;
        currentState.nodeName = "";
    }
    isProcessing.store(true);

    // Create a local copy of outputDir in case of unexpected nasty situations
    bool isLocked = outputDirMutex.try_lock();
    std::string outputDir_Copied = std::string(this->outputDir_);
    if (isLocked) outputDirMutex.unlock();

    // Build a non-owning view with sourceNode_ implicitly prepended
    std::vector<Node*> localView;
    localView.reserve(1 + nodeChain_->size());
    localView.push_back(&sourceNode_);
    for (auto& n : *nodeChain_)
        localView.push_back(n.get());
    
    outputNode_.init({{"format", "wav"}, {"subtype", "pcm16"}});
    localView.push_back(&outputNode_);

    // Remember to clear error message from previous runs
    fileInfoInstance->errorMsgNodeChain.clear();

    // Construct the engine and set up callbacks for progress and error reporting
    ChainEngine engine(std::move(localView));
    engine.setProgressCallback([this](size_t idx, std::string_view name) {
        std::scoped_lock<std::mutex> lock(stateMutex);
        currentState.nodeIndex = idx;
        currentState.nodeName  = name;
    });
    engine.setErrorCallback([&](std::string_view msg) {
        fileInfoInstance->errorMsgNodeChain = msg;
    });

    // Now everything is set up, let's go!
    engine.processFile(*fileInfoInstance, outputDir_Copied);

    // Remember to reset isProcessing flag after done
    isProcessing.store(false);
}
