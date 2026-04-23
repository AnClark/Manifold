#include "ProcessorRegistry.hpp"

#include <stdexcept>

ProcessorRegistry& ProcessorRegistry::getInstance()
{
    static ProcessorRegistry s_instance;
    return s_instance;
}

void ProcessorRegistry::reg(std::string_view id, Factory f)
{
    _factories.emplace(std::string(id), std::move(f));
}

std::unique_ptr<IAudioProcessor> ProcessorRegistry::create(std::string_view id) const
{
    auto it = _factories.find(std::string(id));
    if (it == _factories.end())
        throw std::runtime_error("ProcessorRegistry: unknown processor id '" + std::string(id) + "'");

    return it->second();    // Return the result of invoking the factory function, which is a unique_ptr to a new processor instance.
}

std::vector<std::string> ProcessorRegistry::listAll() const
{
    std::vector<std::string> ids;
    ids.reserve(_factories.size());

    for (const auto& [id, _] : _factories)
        ids.push_back(id);

    return ids;
}
