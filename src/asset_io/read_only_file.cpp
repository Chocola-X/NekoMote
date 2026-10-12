#include <nekomote/asset_io/read_only_file.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace nekomote::asset_io
{
struct ReadOnlyFile::Impl
{
    void* data{MAP_FAILED};
    std::size_t size{0};
    ~Impl()
    {
        if (data != MAP_FAILED)
            ::munmap(data, size);
    }
};
ReadOnlyFile::ReadOnlyFile(const std::filesystem::path& path) : impl_(std::make_unique<Impl>())
{
    struct Descriptor
    {
        int value;
        ~Descriptor()
        {
            if (value >= 0)
                ::close(value);
        }
    } file{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    const auto fail = [&path](const char* operation)
    {
        const auto error = errno;
        throw std::system_error(error, std::generic_category(), path.string() + ": " + operation);
    };
    if (file.value < 0)
        fail("open read-only file");
    struct stat info{};
    if (::fstat(file.value, &info) != 0)
        fail("read file size");
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        static_cast<std::uintmax_t>(info.st_size) > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error(path.string() + ": expected a nonempty regular file");
    impl_->size = static_cast<std::size_t>(info.st_size);
    impl_->data = ::mmap(nullptr, impl_->size, PROT_READ, MAP_PRIVATE, file.value, 0);
    if (impl_->data == MAP_FAILED)
        fail("map read-only file");
}
ReadOnlyFile::~ReadOnlyFile() = default;
std::span<const std::uint8_t> ReadOnlyFile::bytes() const noexcept
{
    return {static_cast<const std::uint8_t*>(impl_->data), impl_->size};
}
} // namespace nekomote::asset_io
