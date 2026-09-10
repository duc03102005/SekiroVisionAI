#include <SekiroVisionAI/Replay.h>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace sekiro::replay {
namespace {
template<class T> T read_integer(std::istream& stream) {
    using U=std::make_unsigned_t<T>;
    std::array<unsigned char,sizeof(T)> bytes{};
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
        throw std::runtime_error("Truncated .svr frame/header");
    U value{};
    for(std::size_t i=0;i<bytes.size();++i)value|=U(bytes[i])<<(i*8);
    if constexpr(std::is_signed_v<T>)return std::bit_cast<T>(value);
    else return value;
}
class RawReader final:public VideoReader {
    std::ifstream stream_;
    int width_{},height_{};
    double previous_pts_{-std::numeric_limits<double>::infinity()};
    std::uint64_t previous_sequence_{},previous_generation_{};
public:
    explicit RawReader(const std::filesystem::path& path):stream_(path,std::ios::binary) {
        if(!stream_)throw std::runtime_error("Cannot open replay file");
        char magic[8]{};
        if(!stream_.read(magic,8)||std::string(magic,8)!="SVRRAW01")
            throw std::runtime_error("Invalid .svr magic (expected SVRRAW01)");
        const auto w=read_integer<std::uint32_t>(stream_),h=read_integer<std::uint32_t>(stream_);
        if(w<16||h<16||w>8192||h>8192||std::uint64_t(w)*h>3840ull*2160)
            throw std::runtime_error("Replay dimensions exceed bounded 4K frame contract");
        width_=static_cast<int>(w);height_=static_cast<int>(h);
    }
    bool next(VideoFrame& out) override {
        if(stream_.peek()==std::char_traits<char>::eof()) {
            if(!stream_.eof())throw std::runtime_error("Replay read failed");
            return false;
        }
        const auto pts=read_integer<std::int64_t>(stream_);
        const auto generation=read_integer<std::uint64_t>(stream_);
        const auto sequence=read_integer<std::uint64_t>(stream_);
        const double pts_ms=static_cast<double>(pts)/10000.0;
        if(pts_ms<=previous_pts_||generation==0||sequence==0||generation<previous_generation_||
           (generation==previous_generation_&&sequence<=previous_sequence_))
            throw std::runtime_error("Replay source PTS/identity is not strictly monotonic");
        auto color=std::make_shared<ColorFrame>();
        color->width=width_;color->height=height_;color->stride=width_*4;
        color->bgra.resize(static_cast<std::size_t>(color->stride)*height_);
        if(!stream_.read(reinterpret_cast<char*>(color->bgra.data()),color->bgra.size()))
            throw std::runtime_error("Truncated .svr BGRA frame");
        previous_pts_=pts_ms;previous_generation_=generation;previous_sequence_=sequence;
        out={color,pts_ms,sequence,generation};return true;
    }
};
}
std::unique_ptr<VideoReader> open_raw_video(const std::filesystem::path& path) {
    return std::make_unique<RawReader>(path);
}
#ifndef _WIN32
std::unique_ptr<VideoReader> open_video(const std::filesystem::path& path) {
    if(path.extension()==".svr")return open_raw_video(path);
    throw std::runtime_error("Native MP4 decoding uses Windows Media Foundation; Linux contract tests use .svr fixtures");
}
#endif
}
