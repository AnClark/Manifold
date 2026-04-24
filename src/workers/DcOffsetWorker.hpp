#pragma once

#include "base/Worker.hpp"

class DcOffsetWorker : public IWorker
{
protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;
};
