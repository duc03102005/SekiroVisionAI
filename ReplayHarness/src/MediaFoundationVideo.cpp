#ifdef _WIN32
#include <SekiroVisionAI/Replay.h>
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sekiro::replay {
using Microsoft::WRL::ComPtr;
namespace {
constexpr DWORD first_video_stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD all_streams=static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
void checked(HRESULT hr,const char* operation) {
    if(FAILED(hr)){std::ostringstream text;text<<operation<<" failed (HRESULT 0x"<<std::hex<<static_cast<unsigned long>(hr)<<')';throw std::runtime_error(text.str());}
}
class Platform {
    bool com_{},mf_{};
public:
    Platform() {
        checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize replay COM");com_=true;
        const HRESULT hr=MFStartup(MF_VERSION);
        if(FAILED(hr)){CoUninitialize();com_=false;checked(hr,"Start Windows Media Foundation");}
        mf_=true;
    }
    ~Platform(){if(mf_)MFShutdown();if(com_)CoUninitialize();}
};
class NativeReader final:public VideoReader {
    // Destruction order releases the reader before MFShutdown and CoUninitialize.
    Platform platform_;
    ComPtr<IMFSourceReader> reader_;
    int width_{},height_{};LONG stride_{};
    std::uint64_t sequence_{},generation_{1};
    bool ended_{};
    double previous_pts_{-std::numeric_limits<double>::infinity()};
    void update_type() {
        ComPtr<IMFMediaType> type;checked(reader_->GetCurrentMediaType(first_video_stream,&type),"Read decoded video type");
        GUID subtype{};checked(type->GetGUID(MF_MT_SUBTYPE,&subtype),"Read RGB subtype");
        if(subtype!=MFVideoFormat_RGB32)throw std::runtime_error("Replay decoder changed away from requested RGB32");
        UINT32 width=0,height=0;checked(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read decoded dimensions");
        if(width<16||height<16||width>8192||height>8192||std::uint64_t(width)*height>3840ull*2160)
            throw std::runtime_error("Decoded replay exceeds bounded 4K frame contract");
        UINT32 stride_bits=0;
        if(SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride_bits)))stride_=static_cast<LONG>(stride_bits);
        else checked(MFGetStrideForBitmapInfoHeader(subtype.Data1,width,&stride_),"Get default RGB stride");
        if(std::abs(static_cast<long long>(stride_))<static_cast<long long>(width)*4)
            throw std::runtime_error("Decoded RGB stride is smaller than a row");
        width_=static_cast<int>(width);height_=static_cast<int>(height);
    }
    std::shared_ptr<const ColorFrame> copy_frame(IMFSample* sample) {
        ComPtr<IMFMediaBuffer> buffer;checked(sample->ConvertToContiguousBuffer(&buffer),"Get decoded frame buffer");
        auto color=std::make_shared<ColorFrame>();color->width=width_;color->height=height_;color->stride=width_*4;
        color->bgra.resize(static_cast<std::size_t>(color->stride)*height_);
        auto rows=[&](const BYTE* scanline,LONG pitch) {
            if(std::abs(static_cast<long long>(pitch))<color->stride)throw std::runtime_error("Locked RGB pitch is too short");
            for(int y=0;y<height_;++y) {
                auto* destination=color->bgra.data()+static_cast<std::size_t>(y)*color->stride;
                std::memcpy(destination,scanline+static_cast<std::ptrdiff_t>(y)*pitch,color->stride);
                // RGB32's unused byte is not guaranteed to contain opaque alpha.
                for(int x=0;x<width_;++x)destination[x*4+3]=255;
            }
        };
        ComPtr<IMF2DBuffer> buffer2d;
        if(SUCCEEDED(buffer.As(&buffer2d))) {
            BYTE* first=nullptr;LONG pitch=0;checked(buffer2d->Lock2D(&first,&pitch),"Lock decoded 2D frame");
            try {rows(first,pitch);}catch(...){buffer2d->Unlock2D();throw;}
            checked(buffer2d->Unlock2D(),"Unlock decoded 2D frame");
        } else {
            BYTE* bytes=nullptr;DWORD maximum=0,current=0;checked(buffer->Lock(&bytes,&maximum,&current),"Lock decoded RGB frame");
            try {
                const auto absolute=static_cast<std::uint64_t>(std::abs(static_cast<long long>(stride_)));
                const auto needed=absolute*static_cast<std::uint64_t>(height_-1)+color->stride;
                if(needed>current||needed>maximum)throw std::runtime_error("Decoded RGB buffer is truncated");
                const BYTE* first=bytes;
                if(stride_<0)first+=absolute*static_cast<std::uint64_t>(height_-1);
                rows(first,stride_);
            }catch(...){buffer->Unlock();throw;}
            checked(buffer->Unlock(),"Unlock decoded RGB frame");
        }
        return color;
    }
public:
    explicit NativeReader(const std::filesystem::path& path) {
        if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Replay accepts a local media file only");
        ComPtr<IMFAttributes> attributes;checked(MFCreateAttributes(&attributes,2),"Create source-reader attributes");
        checked(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE),"Enable native RGB conversion");
        checked(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE),"Allow installed hardware transforms");
        checked(MFCreateSourceReaderFromURL(path.c_str(),attributes.Get(),&reader_),"Open native media file");
        checked(reader_->SetStreamSelection(all_streams,FALSE),"Deselect media streams");
        checked(reader_->SetStreamSelection(first_video_stream,TRUE),"Select video stream");
        ComPtr<IMFMediaType> output;checked(MFCreateMediaType(&output),"Create RGB media type");
        checked(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Set video type");
        checked(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),"Set decoded RGB32 type");
        checked(reader_->SetCurrentMediaType(first_video_stream,nullptr,output.Get()),"Configure native RGB decoder");
        update_type();
    }
    bool next(VideoFrame& out) override {
        if(ended_)return false;
        // Bound no-sample notifications. A malformed source cannot spin forever.
        for(int attempt=0;attempt<1024;++attempt) {
            DWORD stream=0,flags=0;LONGLONG pts=0;ComPtr<IMFSample> sample;
            checked(reader_->ReadSample(first_video_stream,0,&stream,&flags,&pts,&sample),"Decode replay sample");
            if(flags&MF_SOURCE_READERF_ERROR)throw std::runtime_error("Media Foundation reported a source error");
            if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){update_type();++generation_;}
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM)ended_=true;
            if(!sample){if(ended_)return false;continue;}
            UINT32 discontinuity=FALSE;
            if(SUCCEEDED(sample->GetUINT32(MFSampleExtension_Discontinuity,&discontinuity))&&discontinuity)++generation_;
            const double pts_ms=static_cast<double>(pts)/10000.0;
            if(pts_ms<=previous_pts_)throw std::runtime_error("Decoded video presentation timestamps are not strictly monotonic");
            previous_pts_=pts_ms;out={copy_frame(sample.Get()),pts_ms,++sequence_,generation_};return true;
        }
        throw std::runtime_error("Native decoder exceeded bounded no-sample notifications");
    }
};
}
std::unique_ptr<VideoReader> open_video(const std::filesystem::path& path) {
    if(path.extension()==".svr")return open_raw_video(path);
    return std::make_unique<NativeReader>(path);
}
}
#endif
