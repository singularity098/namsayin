// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/BlockRegistry.hpp"

#include "engine/blocks/NamBlock.hpp"

namespace namsayin {

BlockRegistry::BlockRegistry(std::vector<const BlockDescriptor*> types)
    : fTypes(std::move(types))
{
}

const BlockRegistry& BlockRegistry::builtin()
{
    // One line per block type, in Add Block dialog order within each category
    static const BlockRegistry registry({
        &kNamBlockDescriptor,
    });
    return registry;
}

const BlockDescriptor* BlockRegistry::find(std::string_view typeId) const
{
    for (const BlockDescriptor* type : fTypes)
    {
        if (typeId == type->typeId)
            return type;
    }
    return nullptr;
}

} // namespace namsayin
