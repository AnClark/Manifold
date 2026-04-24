#include "SndFileWorker.hpp"

void SndFileWorker::processItem(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    if (!fileInfoInstance || fileInfoInstance->aboutToBeRemoved)
        return;
    fileInfoInstance->parseSndFile();
}
