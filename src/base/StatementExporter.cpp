#include "StatementExporter.hpp"
#include "utils/Timestamp.hpp"

const char* BaseStatementExporter::getStatusLabel(FileRunRecord::Status st)
{
    switch (st)
    {
        case FileRunRecord::Status::Processing: return " RUN ";
        case FileRunRecord::Status::Done:       return "  OK ";
        case FileRunRecord::Status::Error:      return " ERR ";
        case FileRunRecord::Status::Cancelled:  return " CXL ";
        default:                                return " ... ";
    }
}

std::string BaseStatementExporter::fetchContextInfoColumn(std::shared_ptr<FileRunRecord> record)
{
    const auto  status  = record->status.load(std::memory_order_relaxed);

    if (status == FileRunRecord::Status::Processing)
    {
        std::scoped_lock<std::mutex> rlock(record->progressMutex);

        char prog[128];
        std::snprintf(prog, sizeof(prog), "[%02zu] %s",
                    record->currentNodeIndex,
                    !record->currentNodeName.empty() ? record->currentNodeName.c_str() : "Processing by an unknown node");
        return prog;
    }
    else if (status == FileRunRecord::Status::Error)
    {
        std::scoped_lock<std::mutex> rlock(record->progressMutex);

        return !record->errorMessage.empty() ? record->errorMessage : "Unknown error";
    }
    else if (status == FileRunRecord::Status::Done)
    {
        return TimestampUtils::fmtElapsed(record->timestampStarted, record->timestampFinished);
    }
    else if (status == FileRunRecord::Status::Cancelled)
    {
        return "Cancelled";
    }
    else
    {
        return "Unrecognized state";
    }
}
