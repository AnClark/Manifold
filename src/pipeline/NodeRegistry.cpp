#include "NodeRegistry.hpp"

#include <algorithm>
#include <stdexcept>

NodeRegistry& NodeRegistry::getInstance()
{
    static NodeRegistry s_instance;
    return s_instance;
}

void NodeRegistry::reg(NodeDescriptor descriptor, Factory factory)
{
    std::string id = descriptor.id;
    // Overwrite if the id already exists (last registration wins)
    factories_[id] = std::move(factory);
    // Remove existing descriptor with same id, then append the new one
    descriptors_.erase(
        std::remove_if(descriptors_.begin(), descriptors_.end(),
                       [&id](const NodeDescriptor& d) { return d.id == id; }),
        descriptors_.end());
    descriptors_.push_back(std::move(descriptor));
}

std::unique_ptr<Node> NodeRegistry::create(std::string_view id) const
{
    auto it = factories_.find(std::string(id));
    if (it == factories_.end())
        throw std::runtime_error(
            "NodeRegistry: unknown node id '" + std::string(id) + "'");
    return it->second();
}

const std::vector<NodeDescriptor>& NodeRegistry::listAll() const
{
    return descriptors_;
}

std::vector<const NodeDescriptor*> NodeRegistry::listByCategory(std::string_view category) const
{
    std::vector<const NodeDescriptor*> result;
    for (const auto& d : descriptors_)
        if (d.category == category)
            result.push_back(&d);
    return result;
}

std::vector<std::string> NodeRegistry::listCategories() const
{
    std::vector<std::string> cats;
    for (const auto& d : descriptors_) {
        if (std::find(cats.begin(), cats.end(), d.category) == cats.end())
            cats.push_back(d.category);
    }
    return cats;
}

const NodeDescriptor* NodeRegistry::findById(std::string_view id) const
{
    for (const auto& d : descriptors_)
        if (d.id == id)
            return &d;
    return nullptr;
}
