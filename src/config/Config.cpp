#include "Config.hpp"

#include <sstream>
#include <filesystem>

std::string NodeConfig::saveNodeChain(std::vector<std::shared_ptr<Node>> &nodeChain, toml::table outputConfig, std::string name)
{
    // 1. Create root table
    toml::table root;

    // 2. Add metadata
    root.insert("config_type", kConfigTypeNodeChain);
    if (!name.empty())
        root.insert("name", name);
    // TODO: Maybe need to add Manifold's version

    // 3. Export all configs from the member of nodeChain
    toml::array chain;  // Create a TOML array for our node chain
    for (auto node : nodeChain)
    {
        toml::table nodeTable;
        nodeTable.insert("id", node->id());  // Store the node's ID (unique factory key) for reconstruction

        auto config = node->exportConfig();
        for (const auto& pair : config)
        {
            nodeTable.insert_or_assign(pair.first, pair.second);
        }
        chain.push_back(nodeTable);
    }
    root.insert_or_assign("chain", chain);

    // 4. Insert output config (if specified)
    if (!outputConfig.empty())
        root.insert_or_assign("output", outputConfig);

    // 5. Render TOML to ostingstream
    // NOTE: TOML++ does not provide any API for directly exporting TOML to std::string (or const char*).
    //       Only stream is supported.
    std::ostringstream outputStream;
    outputStream << root;

    std::string finalOutput = outputStream.str();
    return std::move(finalOutput);
}

toml::table NodeConfig::buildOutputConfig(std::string folder, std::string formatName, std::string subTypeName)
{
    toml::table outputConfig;

    outputConfig.insert("folder", folder);
    outputConfig.insert("format", formatName);
    outputConfig.insert("subtype", subTypeName);

    return std::move(outputConfig);
}

std::string FileConfig::saveFileList(SndFileList &fileList)
{
    // 1. Create root table
    toml::table root;

    // 2. Add metadata
    root.insert("config_type", kConfigTypeFileList);
    root.insert("platform", getPlatform());

    // 3. Export all file paths in fileList as a toml::array
    toml::array files;
    for (auto listItem : fileList)
    {
        toml::table file;
        file.insert("path", listItem->filePath);

        files.push_back(file);
    }
    root.insert_or_assign("file", files);

    // 4. Render TOML to ostingstream
    // NOTE: TOML++ does not provide any API for directly exporting TOML to std::string (or const char*).
    //       Only stream is supported.
    std::ostringstream outputStream;
    outputStream << root;

    std::string finalOutput = outputStream.str();
    return std::move(finalOutput);
}

const char* FileConfig::getPlatform()
{
    switch (std::filesystem::path::preferred_separator)
    {
        case '\\':
            return kPlatformWin32;
        case '/':
            return kPlatformUnixLike;
        default:
            return kPlatformUnknown;
    }
}
