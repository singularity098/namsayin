// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

#include <string_view>
#include <vector>

namespace namsayin {

// The list of block types the engine can create. The Add Block dialog, parameter names and preset loading
// all read from here. To add an effect: write its Block subclass and add its descriptor in BlockRegistry.cpp.
class BlockRegistry {
public:
    explicit BlockRegistry(std::vector<const BlockDescriptor*> types);

    // The block types built into the plugin
    static const BlockRegistry& builtin();

    const BlockDescriptor* find(std::string_view typeId) const;
    const std::vector<const BlockDescriptor*>& types() const { return fTypes; }

private:
    std::vector<const BlockDescriptor*> fTypes;
};

} // namespace namsayin
