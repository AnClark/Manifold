#pragma once

#include "base/Worker.hpp"

class SndFileWorker : public IWorker
{
protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;
};
