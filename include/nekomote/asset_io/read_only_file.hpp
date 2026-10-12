#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace nekomote::asset_io
{
// Maps an immutable file without copying all of it into an anonymous heap.
// bytes() borrows this owner; the file must not be truncated or overwritten
// while it is in use. Throws with path/source on open or mapping failure.
class ReadOnlyFile
{
  public:
    explicit ReadOnlyFile(const std::filesystem::path& path);
    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;
    std::span<const std::uint8_t> bytes() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::asset_io
