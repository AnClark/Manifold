#include "SingleFileProcessorWorker.hpp"
#include "pipeline/ChainEngine.hpp"

void SingleFileProcessorWorker::processItem(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    if (!fileInfoInstance || fileInfoInstance->aboutToBeRemoved || shouldCancelProcessing)
        return;
    
    if (!nodeChain_ || nodeChain_->size() <= 0)
        return;
    
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

    ChainEngine engine(std::move(localView));
    engine.processFile(*fileInfoInstance, outputDir_Copied);
}
