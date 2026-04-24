#pragma once

#include "base/Worker.hpp"

class EBUR128Worker : public IWorker
{
protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;
};
