#include "model_container.hpp"

#include <algorithm>
#include <stdexcept>

namespace nekomote::asset_io
{
namespace
{
void write_length(std::ostream& stream, std::uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        stream.put(static_cast<char>((value >> (i * 8)) & 255));
}
} // namespace

ModelInput::ModelInput(const std::filesystem::path& file) : file_(file, std::ios::binary)
{
    if (!file_)
        throw std::runtime_error("cannot open model");
    if (!std::filesystem::is_regular_file(file) ||
        std::filesystem::file_size(file) > kModelDecodedLimit + 1024 * 1024)
        throw std::runtime_error("encoded model size exceeds limit or is not a regular file");
    std::array<char, 8> magic{};
    file_.read(magic.data(), magic.size());
    if (!file_ || magic != kModelMagic)
        throw std::runtime_error("unsupported model container; rebuild with nekomote_import or "
                                 "convert with tools/migrate_model_assets.py");
    for (unsigned i = 0; i < 8; ++i)
    {
        const auto byte = file_.get();
        if (byte == std::char_traits<char>::eof())
            throw std::runtime_error("truncated model header");
        expected_ |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8);
    }
    if (expected_ == 0 || expected_ > kModelDecodedLimit)
        throw std::runtime_error("decoded model size exceeds limit");
    if (inflateInit(&codec_) != Z_OK)
        throw std::runtime_error("cannot initialize model decompressor");
}
ModelInput::~ModelInput()
{
    inflateEnd(&codec_);
}
ModelInput::int_type ModelInput::underflow()
{
    if (gptr() && gptr() < egptr())
        return traits_type::to_int_type(*gptr());
    if (complete_)
        return traits_type::eof();
    codec_.next_out = reinterpret_cast<Bytef*>(output_.data());
    codec_.avail_out = static_cast<uInt>(output_.size());
    while (codec_.avail_out == output_.size())
    {
        if (codec_.avail_in == 0)
        {
            file_.read(input_.data(), static_cast<std::streamsize>(input_.size()));
            const auto size = file_.gcount();
            if (size == 0)
                throw std::runtime_error("truncated model compressed stream");
            codec_.next_in = reinterpret_cast<Bytef*>(input_.data());
            codec_.avail_in = static_cast<uInt>(size);
        }
        const auto status = inflate(&codec_, Z_NO_FLUSH);
        const auto size = output_.size() - codec_.avail_out;
        if (produced_ + size > expected_)
            throw std::runtime_error("model decoded length mismatch");
        if (status == Z_STREAM_END)
        {
            if (produced_ + size != expected_)
                throw std::runtime_error("model decoded length mismatch");
            if (codec_.avail_in != 0 || file_.peek() != traits_type::eof() || file_.bad())
                throw std::runtime_error("trailing model compressed data");
            complete_ = true;
        }
        else if (status != Z_OK)
            throw std::runtime_error("invalid model compressed stream or checksum");
        if (size != 0)
        {
            produced_ += size;
            setg(output_.data(), output_.data(), output_.data() + size);
            return traits_type::to_int_type(*gptr());
        }
        if (complete_)
            return traits_type::eof();
    }
    return traits_type::eof();
}

ModelOutput::ModelOutput(const std::filesystem::path& file) : file_(file, std::ios::binary)
{
    if (!file_)
        throw std::runtime_error("cannot create compressed model");
    file_.exceptions(std::ios::badbit | std::ios::failbit);
    file_.write(kModelMagic.data(), kModelMagic.size());
    write_length(file_, 0);
    if (deflateInit(&codec_, Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("cannot initialize model compressor");
    setp(input_.data(), input_.data() + input_.size());
}
ModelOutput::~ModelOutput()
{
    deflateEnd(&codec_);
}
void ModelOutput::flush(int mode)
{
    if (complete_)
        throw std::runtime_error("model compressor already finished");
    const auto size = static_cast<std::size_t>(pptr() - pbase());
    if (consumed_ + size > kModelDecodedLimit)
        throw std::runtime_error("encoded model size exceeds limit");
    consumed_ += size;
    codec_.next_in = reinterpret_cast<Bytef*>(input_.data());
    codec_.avail_in = static_cast<uInt>(size);
    do
    {
        codec_.next_out = reinterpret_cast<Bytef*>(output_.data());
        codec_.avail_out = static_cast<uInt>(output_.size());
        const auto status = deflate(&codec_, mode);
        if (status != Z_OK && status != Z_STREAM_END)
            throw std::runtime_error("model compression failed");
        file_.write(output_.data(),
                    static_cast<std::streamsize>(output_.size() - codec_.avail_out));
        if (status == Z_STREAM_END)
        {
            complete_ = true;
            break;
        }
    } while (codec_.avail_in != 0 || codec_.avail_out == 0 || mode == Z_FINISH);
    setp(input_.data(), input_.data() + input_.size());
}
ModelOutput::int_type ModelOutput::overflow(int_type value)
{
    flush(Z_NO_FLUSH);
    if (!traits_type::eq_int_type(value, traits_type::eof()))
    {
        *pptr() = traits_type::to_char_type(value);
        pbump(1);
    }
    return traits_type::not_eof(value);
}
int ModelOutput::sync()
{
    if (pptr() != pbase())
        flush(Z_NO_FLUSH);
    return 0;
}
void ModelOutput::finish()
{
    flush(Z_FINISH);
    file_.seekp(8);
    write_length(file_, consumed_);
    file_.close();
}
} // namespace nekomote::asset_io
