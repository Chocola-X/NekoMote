#pragma once

#include <zlib.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <streambuf>

namespace nekomote::asset_io
{
inline constexpr std::array<char, 8> kModelMagic{'N', 'K', 'M', 'M', 'P', 'K', '0', '1'};
inline constexpr std::uint64_t kModelDecodedLimit = 512ULL * 1024 * 1024;

// Loading only: two fixed 32 KiB buffers and zlib's window, never a complete
// compressed or decompressed document. One checked zlib stream, no trailing data.
class ModelInput final : public std::streambuf
{
  public:
    explicit ModelInput(const std::filesystem::path& file);
    ~ModelInput() override;
    ModelInput(const ModelInput&) = delete;
    ModelInput& operator=(const ModelInput&) = delete;

  private:
    int_type underflow() override;
    std::ifstream file_;
    z_stream codec_{};
    std::array<char, 32768> input_{}, output_{};
    std::uint64_t expected_{0}, produced_{0};
    bool complete_{false};
};

// Offline only. finish() is explicit so disk/codec failures remain observable.
class ModelOutput final : public std::streambuf
{
  public:
    explicit ModelOutput(const std::filesystem::path& file);
    ~ModelOutput() override;
    ModelOutput(const ModelOutput&) = delete;
    ModelOutput& operator=(const ModelOutput&) = delete;
    void finish();

  private:
    int_type overflow(int_type value) override;
    int sync() override;
    void flush(int mode);
    std::ofstream file_;
    z_stream codec_{};
    std::array<char, 32768> input_{}, output_{};
    std::uint64_t consumed_{0};
    bool complete_{false};
};
} // namespace nekomote::asset_io
