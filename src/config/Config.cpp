#include "Config.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"

#include <sstream>
#include <fstream>
#include <filesystem>

std::string NodeConfig::saveNodeChain(std::vector<std::shared_ptr<Node>> &nodeChain, std::mutex& nodeChainMutex, toml::table outputConfig, std::string name)
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
    {
        // Protected Node Chain by mutex (passed from application side)
        std::scoped_lock<std::mutex> nodeChainLock(nodeChainMutex);

        for (auto node : nodeChain)
        {
            toml::table nodeTable;
            nodeTable.insert("id", node->id());  // Store the node's ID (unique factory key) for reconstruction
            nodeTable.insert("is_ui_collapsed", node->isUiCollapsed()); // Store the node's UI collapsed state for better UX when importing/exporting node chain

            auto config = node->exportConfig();
            for (const auto& pair : config)
            {
                nodeTable.insert_or_assign(pair.first, pair.second);
            }
            chain.push_back(nodeTable);
        }
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

void NodeConfig::loadNodeChain(std::string_view configFilePath, std::vector<std::shared_ptr<Node>>& targetNodeChain, std::mutex& nodeChainMutex, OutputConfigPref* targetOutputPref)
{
    // 1. Convert configFilePath to canonical format with std::filesystem::path.
    //    u8path() explicitly interprets the input as UTF-8, which is required on
    //    Windows where the default path constructor uses the ANSI (narrow) encoding.
    const auto path = std::filesystem::u8path(configFilePath);

    // 2. Check if file exists
    if (!std::filesystem::exists(path))
        throw "Node Chain config file does not exist";

    try
    {
        // 3. Open the file via std::ifstream using the filesystem::path directly.
        //    On Windows, ifstream(filesystem::path) internally calls the wide-char
        //    (_wfopen) API, so UTF-8 / CJK paths are handled correctly.
        //    toml::parse_file(string) uses fopen(char*) internally, which interprets
        //    the path as ANSI on Windows — silently failing for non-ASCII paths and
        //    returning an empty parse result instead of an error.
        std::ifstream ifs(path, std::ios::in);
        if (!ifs.is_open())
            throw std::runtime_error("Failed to open node chain config file");

        // 4. Parse TOML from stream.
        //    Pass path.u8string() as the source name so toml++ includes it in
        //    any parse-error messages.
        auto result = toml::parse(ifs, path.u8string());

        // 4. Verify config file type
        if (const auto configType = result["config_type"].value<std::string>())
        {
            LOG_DEBUGF(__func__, "configType: %s", configType->c_str());

            if (configType != kConfigTypeNodeChain)
                throw "Invalid Node Chain file: File type mismatch";
        }

        // 5. Parse node chain from TOML, and build our temporary node chain
        //    Temporary node chain can prevent possible parse errors from ruining our current chain.
        std::vector<std::shared_ptr<Node>> tmpNodeChain;

        if (const auto* nodeChainArray = result["chain"].as_array())
        {
            for (int i = 0; i < static_cast<int>(nodeChainArray->size()); ++i)
            {
                if (const toml::table* nodeTable = nodeChainArray->get(i)->as_table())
                {
                    // Intialize Node instance from registry
                    // NOTE: NodeRegistry::create() throws a runtime error if a node is not in the registry.
                    const auto* idNode = nodeTable->get("id");
                    if (!idNode)
                        throw std::runtime_error("Node chain entry is missing required field 'id'");
                    const auto id = idNode->value<std::string>();
                    auto node = NodeRegistry::getInstance().create(id->c_str());

                    // Set UI collapsed state before init() so that the node's drawUI() can adjust accordingly when being initialized.
                    // NOTE: is_ui_collapsed may be absent in older config files; guard against null before dereferencing.
                    if (const auto* isUiCollapsedNode = nodeTable->get("is_ui_collapsed"))
                        if (const auto isUiCollapsed = isUiCollapsedNode->value<bool>())
                            node->collapseUI(*isUiCollapsed);

                    // Parse and load parameters
                    // NOTE: We don't validate parameters here, just pass them to Node as-is.
                    //       It's the Nodes' duty to validate params.
                    std::unordered_map<std::string, std::string> params;
                    for (auto& [key, node] : *nodeTable)
                    {
                        const std::string_view k = key.str();   // toml::key is toml++'s internal type. Implicitly conver to string_view to get its value.
                        // `v` is a std::optional. Cannot be directly passed as unordered_map's value. Convertion is needed as well.
                        // NOTICE: Make sure the conversion is successful, otherwise the program will crash if the value were not std::string.
                        //         (`v` will be nullptr if conversion failed.)
                        if (const auto v = node.value<std::string>())
                            params[k.data()] = v->c_str();
                    }
                    node->init(std::move(params));

                    tmpNodeChain.emplace_back(std::move(node));
                }
            }
        }

        // 5. Build up node chain
        {
            std::scoped_lock<std::mutex> nodeChainLock(nodeChainMutex);

            // Clear current node chain
            targetNodeChain.clear();

            for (auto iter = tmpNodeChain.begin(); iter != tmpNodeChain.end(); iter++)
            {
                targetNodeChain.push_back(*iter);
            }
        }

        // 6. Import output config to UI preference, if needed
        if (targetOutputPref)
        {
            if (const auto* outputConfig = result["output"].as_table())
                targetOutputPref->fromTable(*outputConfig);
        }

        return;
    }
    catch (const std::runtime_error& err)
    {
        throw err.what();
    }
}

toml::table NodeConfig::buildOutputConfig(OutputConfigPref &pref)
{
    return std::move(pref.getTable());
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

std::vector<std::string> FileConfig::loadFileList(std::string_view configFilePath)
{
    // 1. Convert configFilePath to canonical format with std::filesystem::path.
    //    u8path() explicitly interprets the input as UTF-8, which is required on
    //    Windows where the default path constructor uses the ANSI (narrow) encoding.
    const auto path = std::filesystem::u8path(configFilePath);

    // 2. Check if file exists
    if (!std::filesystem::exists(path))
        throw "File list config file does not exist";

    try
    {
        // 3. Open the file via std::ifstream using the filesystem::path directly.
        //    On Windows, ifstream(filesystem::path) internally calls the wide-char
        //    (_wfopen) API, so UTF-8 / CJK paths are handled correctly.
        //    toml::parse_file(string) uses fopen(char*) internally, which interprets
        //    the path as ANSI on Windows — silently failing for non-ASCII paths and
        //    returning an empty parse result instead of an error.
        std::ifstream ifs(path, std::ios::in);
        if (!ifs.is_open())
            throw std::runtime_error("Failed to open file list config file");

        // 4. Parse TOML from stream.
        //    Pass path.u8string() as the source name so toml++ includes it in
        //    any parse-error messages.
        auto result = toml::parse(ifs, path.u8string());

        // 5. Verify config file type
        if (const auto configType = result["config_type"].value<std::string>())
        {
            LOG_DEBUGF(__func__, "configType: %s", configType->c_str());

            if (configType != kConfigTypeFileList)
                throw "Invalid file list config: File type mismatch";
        }

        // 6. Warn if the platform tag does not match the current platform.
        //    Path-separator differences (Windows '\\' vs POSIX '/') may cause the
        //    loaded paths to be invalid on the current system, but we leave it to
        //    the caller to decide how to handle that situation.
        if (const auto platform = result["platform"].value<std::string>())
        {
            if (*platform != getPlatform())
                LOG_WARNF("FileConfig",
                          "Platform mismatch: file was saved on '%s', current platform is '%s'. "
                          "Loaded paths may use a different separator.",
                          platform->c_str(), getPlatform());
        }

        // 7. Extract file paths from the [[file]] array.
        //    Each entry is expected to have a "path" key; entries missing that key
        //    are skipped with a warning rather than aborting the entire load.
        std::vector<std::string> filePaths;
        if (const auto* fileArray = result["file"].as_array())
        {
            for (int i = 0; i < static_cast<int>(fileArray->size()); ++i)
            {
                if (const toml::table* fileTable = fileArray->get(i)->as_table())
                {
                    if (const auto pathValue = (*fileTable)["path"].value<std::string>())
                        filePaths.emplace_back(*pathValue);
                    else
                        LOG_WARNF("FileConfig", "File entry %d is missing required field 'path'; skipped.", i);
                }
            }
        }

        return std::move(filePaths);
    }
    catch (const std::runtime_error& err)
    {
        throw err.what();
    }
}
