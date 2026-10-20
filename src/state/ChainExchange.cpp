// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/ChainExchange.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace namsayin {

namespace fs = std::filesystem;

namespace {

fs::path exchangeDir()
{
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec)
        dir = ".";
    return dir / "namsayin";
}

} // namespace

std::string chainExchangePath(uint32_t instanceId)
{
    return (exchangeDir() / ("chain-" + std::to_string(instanceId) + ".json")).string();
}

bool writeChainExchange(uint32_t instanceId, const std::string& json)
{
    std::error_code ec;
    fs::create_directories(exchangeDir(), ec);

    const fs::path target = chainExchangePath(instanceId);
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << json;
        if (!out)
            return false;
    }
    fs::rename(temp, target, ec);
    return !ec;
}

std::optional<std::string> readChainExchange(uint32_t instanceId)
{
    std::ifstream in(chainExchangePath(instanceId), std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace namsayin
