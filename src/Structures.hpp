#pragma once

#include <sndfile.h>

#include <string>
#include <vector>

#if _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#endif

struct SndFileInfo
{
    std::string fileName;
    SNDFILE *handle = nullptr;

    SF_INFO info = {};
    float lufsI = 0.0f;
    float truePeak = 0.0f;

    bool isParseOK = false;
    std::string errorMsg;

    bool selected = false;    // Mark if selected in File List
    
    void parseSndFile()
    {
        info = {};
        isParseOK = false;
        handle = nullptr;
        errorMsg.clear();

#ifdef _WIN32
        // UTF-8 转 UTF-16 (Windows 宽字符)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, &wpath[0], wlen);
        
        handle = sf_wchar_open(wpath.c_str(), SFM_READ, &info);
#else
        handle = sf_open(fileName, SFM_READ, &info);
#endif

        if (handle == nullptr)
        {
            errorMsg = sf_error_number(sf_error(nullptr));
            isParseOK = false;

            return;
        }
        
        sf_close(handle);
        handle = nullptr;
        isParseOK = true;
    }

    void parseSndFile(const char* newFileName)
    {
        this->fileName = newFileName;
        parseSndFile();
    }
};

typedef std::vector<SndFileInfo> SndFileList;
