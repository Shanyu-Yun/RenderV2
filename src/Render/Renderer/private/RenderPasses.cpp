#include "RenderPasses.hpp"

#include <stdexcept>

namespace renderer
{

void RenderPassSequence::addPass(RenderPassDefinition pass)
{
    for (const auto &existing : m_passes)
    {
        if (existing.name == pass.name)
        {
            throw std::runtime_error("RenderPass with the same name already exists: " + pass.name);
        }
    }
    m_passes.emplace_back(std::move(pass));
}

const RenderPassDefinition *RenderPassSequence::findPass(const std::string &name) const noexcept
{
    for (const auto &pass : m_passes)
    {
        if (pass.name == name)
        {
            return &pass;
        }
    }
    return nullptr;
}

} // namespace renderer
