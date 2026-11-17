#pragma once

#include <nekomote/host_gpu/gpu.hpp>

#include <charconv>
#include <cstddef>
#include <istream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nekomote::host_gpu::detail
{
// pci.ids describes vendor/device names; subsystem records have two leading tabs.
inline std::vector<std::string> pci_names(std::span<const Device> devices, std::istream& input)
{
    std::vector<std::string> names(devices.size(), "Unknown GPU");
    unsigned vendor{};
    bool have_vendor = false;
    std::string vendor_name;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        const bool device_line = line.front() == '\t';
        std::string_view record(line);
        if (device_line)
            record.remove_prefix(1);
        if (record.size() < 6 || record.front() == '\t' || (record[4] != ' ' && record[4] != '\t'))
        {
            if (!device_line)
                have_vendor = false;
            continue;
        }
        unsigned id{};
        const auto parsed = std::from_chars(record.data(), record.data() + 4, id, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != record.data() + 4)
        {
            if (!device_line)
                have_vendor = false;
            continue;
        }
        const auto begin = record.find_first_not_of(" \t", 4);
        if (begin == std::string_view::npos)
            continue;
        const auto label = record.substr(begin, record.find_last_not_of(" \t\r") - begin + 1);
        if (!device_line)
        {
            vendor = id;
            have_vendor = true;
            vendor_name = label;
        }
        if (!have_vendor)
            continue;
        for (std::size_t i = 0; i < devices.size(); ++i)
        {
            if (devices[i].vendor != vendor)
                continue;
            if (!device_line)
                names[i] = vendor_name + " (unknown GPU)";
            else if (devices[i].device == id)
                names[i] = vendor_name + " " + std::string(label);
        }
    }
    return names;
}
} // namespace nekomote::host_gpu::detail
