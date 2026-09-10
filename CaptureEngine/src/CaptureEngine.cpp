#include <SekiroVisionAI/CaptureEngine.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#ifndef SVAI_BUILD_REVISION
#define SVAI_BUILD_REVISION "unknown"
#endif

namespace sekiro {
namespace {
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using Size = winrt::Windows::Graphics::SizeInt32;
using namespace std::chrono_literals;
constexpr unsigned pool_size = 4, ring_size = 3;
constexpr std::size_t trace_capacity = 120000;
constexpr std::size_t no_row = static_cast<std::size_t>(-1);

struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::uint64_t filetime_value(FILETIME t) noexcept {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

std::optional<WindowTarget> inspect_window(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return {};
    DWORD pid{};
    GetWindowThreadProcessId(hwnd, &pid);
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) return {};
    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &length)) return {};
    const auto name = std::filesystem::path(std::wstring(path.data(), length)).filename().wstring();
    if (_wcsicmp(name.c_str(), L"sekiro.exe") != 0) return {};
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &creation, &exit, &kernel, &user)) return {};
    DWORD code{};
    if (!GetExitCodeProcess(process.value, &code) || code != STILL_ACTIVE) return {};
    std::array<wchar_t, 1024> title{};
    GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    return WindowTarget{hwnd, pid, filetime_value(creation), title.data()};
}

void check_size(Size size) {
    if (size.Width <= 0 || size.Height <= 0 || size.Width > 8192 || size.Height > 8192 ||
        static_cast<std::int64_t>(size.Width) * size.Height > 3840LL * 2160)
        throw winrt::hresult_error(E_INVALIDARG, L"M1 supports a positive SDR content size up to 8.3 megapixels.");
}

struct FrameLease {
    wgc::Direct3D11CaptureFrame frame{nullptr};
    FrameLease() = default;
    explicit FrameLease(wgc::Direct3D11CaptureFrame value) : frame(std::move(value)) {}
    FrameLease(FrameLease&& other) noexcept : frame(std::exchange(other.frame, nullptr)) {}
    FrameLease& operator=(FrameLease&& other) noexcept {
        if (this != &other) { close(); frame = std::exchange(other.frame, nullptr); }
        return *this;
    }
    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;
    ~FrameLease() { close(); }
    void close() noexcept {
        if (frame) { try { frame.Close(); } catch (...) {} frame = nullptr; }
    }
};

enum class Outcome { pending, copied, superseded, busy, resize, invalid, abandoned };
const char* outcome_name(Outcome v) {
    switch (v) {
    case Outcome::copied: return "copied";
    case Outcome::superseded: return "superseded";
    case Outcome::busy: return "ring_busy";
    case Outcome::resize: return "resize";
    case Outcome::invalid: return "invalid";
    case Outcome::abandoned: return "abandoned";
    default: return "pending";
    }
}

struct Row {
    std::uint64_t sequence{}, generation{};
    double source{}, dequeue{}, submit{unavailable}, ready{unavailable}, gpu_ms{unavailable};
    int width{}, height{}, slot{-1};
    Outcome outcome{Outcome::pending};
};

struct Slot {
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::com_ptr<ID3D11ShaderResourceView> view;
    winrt::com_ptr<ID3D11Texture2D> reduced, staging;
    winrt::com_ptr<ID3D11RenderTargetView> small_target;
    winrt::com_ptr<ID3D11Query> event, disjoint, begin, end;
    FrameLease source;
    Row row;
    std::size_t trace_index{no_row};
    bool busy{};
};

struct Runtime {
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    wd3d::IDirect3DDevice winrt_device{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token arrived{}, closed{};
    bool arrived_registered{}, closed_registered{};
    Size size{};
    bool readback{};
    winrt::com_ptr<ID3D11VertexShader> resize_vertex;
    winrt::com_ptr<ID3D11PixelShader> resize_pixel;
    winrt::com_ptr<ID3D11SamplerState> sampler;
    std::array<Slot, ring_size> slots;

    void initialize_readback() {
        static constexpr char shader[] = R"hlsl(
Texture2D source : register(t0);
SamplerState linearSampler : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2,-2) + float2(-1,1),0,1); return v;
}
float4 ps(Vertex v) : SV_Target {
    float g = dot(source.Sample(linearSampler,v.uv).rgb,float3(0.299,0.587,0.114));
    return float4(g,g,g,1);
}
)hlsl";
        winrt::com_ptr<ID3DBlob> vs, ps, errors;
        winrt::check_hresult(D3DCompile(shader, sizeof(shader)-1, "MVP_downsample", nullptr, nullptr,
            "vs", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vs.put(), errors.put()));
        errors = nullptr;
        winrt::check_hresult(D3DCompile(shader, sizeof(shader)-1, "MVP_downsample", nullptr, nullptr,
            "ps", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ps.put(), errors.put()));
        winrt::check_hresult(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, resize_vertex.put()));
        winrt::check_hresult(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, resize_pixel.put()));
        D3D11_SAMPLER_DESC desc{}; desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER; desc.MaxLOD = D3D11_FLOAT32_MAX;
        winrt::check_hresult(device->CreateSamplerState(&desc, sampler.put()));
    }

    void downsample(Slot& slot) {
        ID3D11RenderTargetView* target = slot.small_target.get();
        ID3D11ShaderResourceView* input = slot.view.get();
        ID3D11SamplerState* sample = sampler.get();
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(vision_width),static_cast<float>(vision_height),0,1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, &target, nullptr);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(resize_vertex.get(), nullptr, 0);
        context->PSSetShader(resize_pixel.get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, &sample);
        context->Draw(3,0);
        input = nullptr;
        context->PSSetShaderResources(0,1,&input);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->CopyResource(slot.staging.get(), slot.reduced.get());
    }

    void end_capture() noexcept {
        try { if (arrived_registered) pool.FrameArrived(arrived); } catch (...) {}
        try { if (closed_registered) item.Closed(closed); } catch (...) {}
        arrived_registered = closed_registered = false;
        try { if (session) session.Close(); } catch (...) {}
        session = nullptr;
    }
    ~Runtime() {
        end_capture();
        // On a fault/timeout, stop pool reuse before releasing source leases.
        try { if (pool) pool.Close(); } catch (...) {}
        pool = nullptr;
    }
    unsigned in_flight() const noexcept {
        unsigned count = 0;
        for (const auto& slot : slots) if (slot.busy) ++count;
        return count;
    }
    void allocate_ring(Size next) {
        check_size(next);
        if (in_flight()) throw winrt::hresult_error(E_UNEXPECTED, L"Ring reuse attempted before GPU completion.");
        // Release the old generation before allocating a replacement.
        for (auto& slot : slots) slot = Slot{};
        size = next;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(size.Width); desc.Height = static_cast<UINT>(size.Height);
        desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        for (auto& slot : slots) {
            winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, slot.texture.put()));
            if (readback) {
                winrt::check_hresult(device->CreateShaderResourceView(slot.texture.get(), nullptr, slot.view.put()));
                auto small_desc = desc; small_desc.Width = vision_width; small_desc.Height = vision_height;
                small_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; small_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                winrt::check_hresult(device->CreateTexture2D(&small_desc, nullptr, slot.reduced.put()));
                winrt::check_hresult(device->CreateRenderTargetView(slot.reduced.get(), nullptr, slot.small_target.put()));
                small_desc.BindFlags = 0; small_desc.Usage = D3D11_USAGE_STAGING; small_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                winrt::check_hresult(device->CreateTexture2D(&small_desc, nullptr, slot.staging.put()));
            }
            auto query = [&](D3D11_QUERY type, winrt::com_ptr<ID3D11Query>& result) {
                D3D11_QUERY_DESC q{type, 0};
                winrt::check_hresult(device->CreateQuery(&q, result.put()));
            };
            query(D3D11_QUERY_EVENT, slot.event);
            query(D3D11_QUERY_TIMESTAMP_DISJOINT, slot.disjoint);
            query(D3D11_QUERY_TIMESTAMP, slot.begin); query(D3D11_QUERY_TIMESTAMP, slot.end);
        }
    }
};

std::string json_string(const std::wstring& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : winrt::to_string(value)) {
        if (ch == '"' || ch == '\\') out << '\\' << ch;
        else if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
        else out << ch;
    }
    out << '"';
    return out.str();
}
} // namespace

double qpc_ms() noexcept {
    static const double frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) * 1000.0 / frequency;
}

bool gpu_readback_self_test(std::wstring& error) {
    try {
        Runtime rt; D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels,1,D3D11_SDK_VERSION,rt.device.put(),&level,rt.context.put()));
        rt.readback=true;rt.initialize_readback();rt.allocate_ring({512,288});
        std::vector<std::uint32_t> pixels(512*288);
        for(int y=0;y<288;++y)for(int x=0;x<512;++x)
            pixels[static_cast<std::size_t>(y*512+x)]=y<144?(x<256?0xffff0000u:0xff00ff00u):(x<256?0xff0000ffu:0xffffffffu);
        auto& slot=rt.slots[0];rt.context->UpdateSubresource(slot.texture.get(),0,nullptr,pixels.data(),512*4,0);
        rt.downsample(slot);rt.context->End(slot.event.get());rt.context->Flush();
        const double deadline=qpc_ms()+3000;
        BOOL done=FALSE;
        while(!done) {
            winrt::check_hresult(rt.context->GetData(slot.event.get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH));
            if(qpc_ms()>deadline)throw winrt::hresult_error(E_FAIL,L"GPU test completion timeout");
            if(!done)Sleep(1);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};winrt::check_hresult(rt.context->Map(slot.staging.get(),0,D3D11_MAP_READ,0,&mapped));
        bool valid=true;
        const int expected[]{76,150,29,255};
        for(int i=0;i<4;++i) {
            const int x=(i%2)?192:64,y=(i/2)?108:36;
            const auto* p=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
            for(int c=0;c<3;++c)valid=valid&&std::abs(static_cast<int>(p[c])-expected[i])<=2;
        }
        rt.context->Unmap(slot.staging.get(),0);
        if(!valid){error=L"Readback pixels differ from independently calculated grayscale quadrants";return false;}
        return true;
    }catch(const winrt::hresult_error& e){error=e.message().c_str();return false;}
    catch(...){error=L"GPU readback self-test failed";return false;}
}

std::vector<WindowTarget> find_sekiro_windows() {
    std::vector<WindowTarget> targets;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        // Exceptions must never cross a Win32 callback boundary.
        try {
            if (auto target = inspect_window(hwnd))
                reinterpret_cast<std::vector<WindowTarget>*>(data)->push_back(std::move(*target));
        } catch (...) {}
        return TRUE;
    }, reinterpret_cast<LPARAM>(&targets));
    return targets;
}

bool target_is_current(const WindowTarget& target) {
    auto current = inspect_window(target.hwnd);
    return current && current->pid == target.pid && current->process_creation_time == target.process_creation_time;
}

const wchar_t* state_name(CaptureState state) noexcept {
    switch (state) {
    case CaptureState::starting: return L"Starting";
    case CaptureState::capturing: return L"Capturing";
    case CaptureState::stopping: return L"Stopping";
    case CaptureState::stopped: return L"Stopped";
    case CaptureState::faulted: return L"Faulted";
    default: return L"Idle";
    }
}

struct CaptureEngine::Impl : std::enable_shared_from_this<CaptureEngine::Impl> {
    mutable std::mutex metrics_mutex;
    CaptureSnapshot metrics;
    Series<> arrivals, completions, intervals, dequeue_ages, ready_ages, gpu_times;
    double last_source{}, last_dequeue{}, start_time{}, end_time{};
    std::atomic_bool active{}, stop{}, capture_closed{};
    std::atomic_uint64_t callbacks{}, callback_gaps{};
    std::atomic<double> last_callback{};
    std::mutex signal_mutex;
    std::condition_variable signal;
    bool signaled{};
    std::thread worker;
    std::vector<Row> trace; // Worker-owned; read/export only after active becomes false and join.
    bool record{};
    std::function<void(const SmallFrame&)> frame_sink;
    std::function<void()> discontinuity;

    void notify() noexcept {
        { std::lock_guard lock(signal_mutex); signaled = true; }
        signal.notify_one();
    }
    void state(CaptureState value, const std::wstring& detail) {
        if (discontinuity && (value == CaptureState::stopping || value == CaptureState::faulted)) discontinuity();
        std::lock_guard lock(metrics_mutex);
        metrics.state = value; metrics.detail = detail;
    }
    std::size_t add_row(const Row& row) {
        if (!record) return no_row;
        if (trace.size() >= trace_capacity) {
            std::lock_guard lock(metrics_mutex); metrics.trace_full = true; return no_row;
        }
        trace.push_back(row);
        { std::lock_guard lock(metrics_mutex); metrics.trace_rows = trace.size(); }
        return trace.size() - 1;
    }
    void update_row(std::size_t index, const Row& row) {
        if (index != no_row) trace[index] = row;
    }
    void drop_row(std::size_t index, Outcome outcome) {
        if (index != no_row) trace[index].outcome = outcome;
        std::lock_guard lock(metrics_mutex);
        switch (outcome) {
        case Outcome::superseded: ++metrics.dropped_newest; break;
        case Outcome::busy: ++metrics.dropped_busy; break;
        case Outcome::resize: ++metrics.dropped_resize; break;
        default: ++metrics.dropped_invalid; break;
        }
    }

    void initialize(Runtime& rt, const WindowTarget& target) {
        if (!target_is_current(target)) throw winrt::hresult_error(E_INVALIDARG, L"Sekiro window identity changed. Refresh and select it again.");
        if (!wgc::GraphicsCaptureSession::IsSupported()) throw winrt::hresult_error(E_NOTIMPL, L"Windows Graphics Capture is not supported on this system.");
        auto monitor = MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST);
        winrt::com_ptr<IDXGIFactory1> factory;
        winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()));
        winrt::com_ptr<IDXGIAdapter1> selected;
        DXGI_ADAPTER_DESC1 selected_desc{};
        for (UINT a = 0; !selected; ++a) {
            winrt::com_ptr<IDXGIAdapter1> adapter;
            auto hr = factory->EnumAdapters1(a, adapter.put());
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            winrt::check_hresult(hr);
            DXGI_ADAPTER_DESC1 desc{};
            winrt::check_hresult(adapter->GetDesc1(&desc));
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            for (UINT o = 0; ; ++o) {
                winrt::com_ptr<IDXGIOutput> output;
                hr = adapter->EnumOutputs(o, output.put());
                if (hr == DXGI_ERROR_NOT_FOUND) break;
                winrt::check_hresult(hr);
                DXGI_OUTPUT_DESC out{};
                winrt::check_hresult(output->GetDesc(&out));
                if (out.Monitor == monitor) { selected = adapter; selected_desc = desc; break; }
            }
        }
        if (!selected) throw winrt::hresult_error(E_FAIL, L"No hardware adapter owns the selected window's monitor. No software fallback is used.");
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        winrt::check_hresult(D3D11CreateDevice(selected.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            rt.device.put(), &level, rt.context.put()));
        auto dxgi = rt.device.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        rt.winrt_device = inspectable.as<wd3d::IDirect3DDevice>();
        rt.readback = static_cast<bool>(frame_sink);
        if (rt.readback) rt.initialize_readback();
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(target.hwnd,
            winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(rt.item)));
        rt.allocate_ring(rt.item.Size());
        rt.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(rt.winrt_device,
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, pool_size, rt.size);
        auto weak = weak_from_this();
        rt.arrived = rt.pool.FrameArrived([weak](auto const&, auto const&) noexcept {
            if (auto self = weak.lock()) {
                const double now = qpc_ms();
                const double previous = self->last_callback.exchange(now);
                ++self->callbacks;
                if (previous > 0 && now - previous > 100.0) ++self->callback_gaps;
                self->notify();
            }
        });
        rt.arrived_registered = true;
        rt.closed = rt.item.Closed([weak](auto const&, auto const&) noexcept {
            if (auto self = weak.lock()) { self->capture_closed = true; self->notify(); }
        });
        rt.closed_registered = true;
        rt.session = rt.pool.CreateCaptureSession(rt.item);
        {
            std::lock_guard lock(metrics_mutex);
            metrics.adapter = selected_desc.Description;
            metrics.adapter_luid_low = selected_desc.AdapterLuid.LowPart;
            metrics.adapter_luid_high = selected_desc.AdapterLuid.HighPart;
            metrics.width = rt.size.Width; metrics.height = rt.size.Height; metrics.generation = 1;
        }
        rt.session.StartCapture();
        state(CaptureState::capturing, rt.readback ? L"Capture + GPU downsample feeding the CV detector." : L"SDR BGRA8 capture diagnostics.");
    }

    void poll_completed(Runtime& rt) {
        for (auto& slot : rt.slots) {
            if (!slot.busy) continue;
            if (qpc_ms() - slot.row.submit > 2000.0)
                throw winrt::hresult_error(DXGI_ERROR_DEVICE_HUNG, L"GPU completion or timestamp results exceeded the two-second timeout.");
            BOOL done = FALSE;
            auto hr = rt.context->GetData(slot.event.get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            winrt::check_hresult(hr);
            if (hr == S_FALSE || !done) continue;
            // Record the first observed event completion before reading ancillary timing results.
            if (!std::isfinite(slot.row.ready)) slot.row.ready = qpc_ms();
            UINT64 begin{}, end{};
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            auto a = rt.context->GetData(slot.begin.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            auto b = rt.context->GetData(slot.end.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            auto c = rt.context->GetData(slot.disjoint.get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            winrt::check_hresult(a); winrt::check_hresult(b); winrt::check_hresult(c);
            if (a == S_FALSE || b == S_FALSE || c == S_FALSE) continue;
            if (!disjoint.Disjoint && disjoint.Frequency && end >= begin)
                slot.row.gpu_ms = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
            if (rt.readback) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                auto mapped_hr = rt.context->Map(slot.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
                if (mapped_hr == DXGI_ERROR_WAS_STILL_DRAWING) continue;
                winrt::check_hresult(mapped_hr);
                SmallFrame reduced;
                reduced.sequence = slot.row.sequence; reduced.generation = slot.row.generation;
                reduced.source_ms = slot.row.source; reduced.ready_ms = qpc_ms();
                for (int y=0; y<vision_height; ++y) {
                    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData) + y*mapped.RowPitch;
                    for (int x=0; x<vision_width; ++x) reduced.gray[static_cast<std::size_t>(y*vision_width+x)] = pixels[x*4];
                }
                rt.context->Unmap(slot.staging.get(), 0);
                frame_sink(reduced);
            }
            slot.row.outcome = Outcome::copied;
            update_row(slot.trace_index, slot.row);
            {
                std::lock_guard lock(metrics_mutex);
                ++metrics.completed;
                metrics.copied_bytes += static_cast<std::uint64_t>(slot.row.width) * slot.row.height * 4;
                completions.add(slot.row.ready);
                ready_ages.add(age_ms(slot.row.source, slot.row.ready));
                gpu_times.add(slot.row.gpu_ms);
                if (!std::isfinite(slot.row.gpu_ms)) ++metrics.invalid_gpu_times;
            }
            slot.source.close(); // Event confirms the GPU no longer reads this pool surface.
            slot.busy = false;
        }
        { std::lock_guard lock(metrics_mutex); metrics.in_flight = rt.in_flight(); }
    }

    void drain(Runtime& rt) {
        const double deadline = qpc_ms() + 2000.0;
        while (rt.in_flight()) {
            poll_completed(rt);
            if (!rt.in_flight()) break;
            winrt::check_hresult(rt.device->GetDeviceRemovedReason());
            if (qpc_ms() > deadline) throw winrt::hresult_error(DXGI_ERROR_DEVICE_HUNG, L"GPU completion timed out; session invalidated. Restart capture after checking the driver.");
            std::this_thread::sleep_for(2ms); // Worker only, bounded shutdown/resize, never the UI.
        }
    }

    void ingest(Runtime& rt) {
        FrameLease newest;
        Row row;
        std::size_t index = no_row;
        for (unsigned i = 0; i < pool_size; ++i) {
            FrameLease next{rt.pool.TryGetNextFrame()};
            if (!next.frame) break;
            if (newest.frame) drop_row(index, Outcome::superseded);
            newest = std::move(next);
            const auto size = newest.frame.ContentSize();
            row = {};
            row.source = static_cast<double>(newest.frame.SystemRelativeTime().count()) / 10000.0;
            row.dequeue = qpc_ms(); row.width = size.Width; row.height = size.Height;
            {
                std::lock_guard lock(metrics_mutex);
                row.sequence = ++metrics.received; row.generation = metrics.generation;
                arrivals.add(row.dequeue); last_dequeue = row.dequeue;
                dequeue_ages.add(age_ms(row.source, row.dequeue));
                if (last_source > 0 && row.source > last_source) intervals.add(row.source - last_source);
                last_source = row.source;
            }
            index = add_row(row);
        }
        if (!newest.frame) return;
        if (!std::isfinite(age_ms(row.source, row.dequeue)) || row.width <= 0 || row.height <= 0) {
            drop_row(index, Outcome::invalid); return;
        }
        if (age_ms(row.source, row.dequeue) > 2000.0) {
            drop_row(index, Outcome::invalid);
            throw winrt::hresult_error(E_ABORT, L"WGC source timestamp is stale. Restart capture with fresh game content.");
        }
        if (row.width != rt.size.Width || row.height != rt.size.Height) {
            if (discontinuity) discontinuity();
            drop_row(index, Outcome::resize);
            const Size next{row.width, row.height};
            check_size(next);
            newest.close();
            drain(rt);
            rt.pool.Recreate(rt.winrt_device, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, pool_size, next);
            rt.allocate_ring(next);
            std::lock_guard lock(metrics_mutex);
            ++metrics.generation; metrics.width = next.Width; metrics.height = next.Height;
            return;
        }
        unsigned slot_index = 0;
        while (slot_index < ring_size && rt.slots[slot_index].busy) ++slot_index;
        if (slot_index == ring_size) { drop_row(index, Outcome::busy); return; }
        auto surface = newest.frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> source;
        winrt::check_hresult(surface->GetInterface(__uuidof(ID3D11Texture2D), source.put_void()));
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        winrt::com_ptr<ID3D11Device> owner;
        source->GetDevice(owner.put());
        if (owner.get() != rt.device.get() || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            desc.Width < static_cast<UINT>(row.width) || desc.Height < static_cast<UINT>(row.height)) {
            drop_row(index, Outcome::invalid);
            throw winrt::hresult_error(E_UNEXPECTED, L"Capture surface device, format or dimensions differ from the M1 contract.");
        }
        auto& slot = rt.slots[slot_index];
        slot.source = std::move(newest);
        row.slot = static_cast<int>(slot_index); row.submit = qpc_ms();
        slot.row = row; slot.trace_index = index; slot.busy = true;
        D3D11_BOX box{0, 0, 0, static_cast<UINT>(row.width), static_cast<UINT>(row.height), 1};
        rt.context->Begin(slot.disjoint.get());
        rt.context->End(slot.begin.get());
        rt.context->CopySubresourceRegion(slot.texture.get(), 0, 0, 0, 0, source.get(), 0, &box);
        if (rt.readback) rt.downsample(slot);
        rt.context->End(slot.end.get());
        rt.context->End(slot.disjoint.get());
        rt.context->End(slot.event.get());
        rt.context->Flush(); // Submit headless commands; does not wait for completion.
        update_row(index, row);
        std::lock_guard lock(metrics_mutex);
        metrics.in_flight = rt.in_flight();
        metrics.peak_in_flight = std::max(metrics.peak_in_flight, metrics.in_flight);
    }

    void run(WindowTarget target) noexcept {
        bool apartment_initialized = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartment_initialized = true;
            {
                Runtime rt;
                initialize(rt, target);
                const auto monitor = MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST);
                double last_health = qpc_ms();
                while (!stop) {
                    if (capture_closed) throw winrt::hresult_error(RO_E_CLOSED, L"Sekiro capture item closed. Refresh and restart after the game is available.");
                    poll_completed(rt);
                    ingest(rt);
                    const double now = qpc_ms();
                    if (now - last_health >= 250.0) {
                        winrt::check_hresult(rt.device->GetDeviceRemovedReason());
                        if (!target_is_current(target)) throw winrt::hresult_error(RO_E_CLOSED, L"Sekiro exited or window/process identity changed.");
                        if (MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST) != monitor)
                            throw winrt::hresult_error(E_ABORT, L"Game moved to another monitor. Restart capture to reselect the adapter.");
                        if (IsIconic(target.hwnd)) throw winrt::hresult_error(E_ABORT, L"Game minimized. Restore it and restart capture for a fresh session.");
                        if ((last_dequeue > 0 && now - last_dequeue > 2000.0) || (last_dequeue == 0 && now - start_time > 5000.0))
                            throw winrt::hresult_error(E_ABORT, L"No fresh capture frames. Check game visibility/presentation mode and restart.");
                        last_health = now;
                    }
                    std::unique_lock lock(signal_mutex);
                    signal.wait_for(lock, rt.in_flight() ? 2ms : 50ms, [&] { return signaled || stop || capture_closed; });
                    signaled = false;
                }
                state(CaptureState::stopping, L"Stopping capture and draining GPU copies...");
                rt.end_capture();
                drain(rt);
            }
            state(CaptureState::stopped, L"Capture stopped. Recorded traces can now be exported.");
        } catch (const winrt::hresult_error& error) {
            std::wostringstream message;
            message << error.message().c_str() << L" (HRESULT 0x" << std::hex << static_cast<std::uint32_t>(error.code().value) << L")";
            state(CaptureState::faulted, message.str());
        } catch (const std::exception& error) {
            state(CaptureState::faulted, winrt::to_hstring(error.what()).c_str());
        } catch (...) {
            state(CaptureState::faulted, L"Unexpected capture failure; session invalidated.");
        }
        for (auto& row : trace) if (row.outcome == Outcome::pending) row.outcome = Outcome::abandoned;
        end_time = qpc_ms();
        { std::lock_guard lock(metrics_mutex); metrics.in_flight = 0; }
        if (apartment_initialized) winrt::uninit_apartment();
        active = false; // Teardown and trace writes finish before UI can export/join.
    }
};

CaptureEngine::CaptureEngine() : impl_(std::make_shared<Impl>()) {}
CaptureEngine::~CaptureEngine() {
    request_stop();
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool CaptureEngine::start(const WindowTarget& target, bool record_trace) {
    auto& s = *impl_;
    if (s.active) return false;
    if (s.worker.joinable()) s.worker.join();
    s.trace.clear();
    if (record_trace) s.trace.reserve(trace_capacity);
    s.record = record_trace;
    s.stop = false; s.capture_closed = false; s.callbacks = 0; s.callback_gaps = 0; s.last_callback = 0;
    {
        std::lock_guard lock(s.metrics_mutex);
        s.metrics = {}; s.metrics.recording = record_trace;
        s.metrics.state = CaptureState::starting; s.metrics.detail = L"Opening the selected Sekiro window...";
        s.arrivals = {}; s.completions = {}; s.intervals = {}; s.dequeue_ages = {}; s.ready_ages = {}; s.gpu_times = {};
        s.last_source = s.last_dequeue = 0; s.start_time = qpc_ms(); s.end_time = 0;
    }
    { std::lock_guard lock(s.signal_mutex); s.signaled = false; }
    s.active = true;
    try { s.worker = std::thread([self = impl_, target] { self->run(target); }); }
    catch (...) { s.active = false; s.state(CaptureState::faulted, L"Could not start capture worker."); return false; }
    return true;
}

bool CaptureEngine::set_frame_sink(std::function<void(const SmallFrame&)> sink, std::function<void()> discontinuity) {
    if (impl_->active) return false;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->frame_sink = std::move(sink); impl_->discontinuity = std::move(discontinuity);
    return true;
}

void CaptureEngine::request_stop() noexcept { impl_->stop = true; impl_->notify(); }

CaptureSnapshot CaptureEngine::snapshot() const {
    auto& s = *impl_;
    CaptureSnapshot result;
    Series<> intervals, dequeue, ready, gpu;
    {
        std::lock_guard lock(s.metrics_mutex);
        result = s.metrics;
        const double now = qpc_ms();
        result.capture_fps = s.arrivals.rate(now); result.copy_fps = s.completions.rate(now);
        result.source_silence_ms = s.last_dequeue > 0 ? now - s.last_dequeue : unavailable;
        intervals = s.intervals; dequeue = s.dequeue_ages; ready = s.ready_ages; gpu = s.gpu_times;
    }
    // Sort copies on the UI caller, outside the capture lock.
    result.source_intervals = intervals.percentiles(); result.dequeue_age = dequeue.percentiles();
    result.ready_age = ready.percentiles(); result.gpu_copy = gpu.percentiles();
    result.active = s.active; result.callback_notifications = s.callbacks; result.callback_gaps = s.callback_gaps;
    return result;
}

bool CaptureEngine::export_trace(const std::filesystem::path& path, std::wstring& error) {
    auto& s = *impl_;
    if (s.active) { error = L"Stop capture before exporting."; return false; }
    if (s.worker.joinable()) s.worker.join();
    if (!s.record || s.trace.empty()) { error = L"No trace recorded. Enable Record trace before Start."; return false; }
    try {
        std::ofstream out(path, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.imbue(std::locale::classic()); out << std::setprecision(15);
        out << "sequence,generation,source_qpc_ms,dequeue_qpc_ms,submit_qpc_ms,ready_observed_qpc_ms,gpu_copy_ms,width,height,slot,outcome\n";
        auto number = [&](double value) { if (std::isfinite(value)) out << value; };
        for (const auto& row : s.trace) {
            out << row.sequence << ',' << row.generation << ','; number(row.source); out << ',';
            number(row.dequeue); out << ','; number(row.submit); out << ','; number(row.ready); out << ',';
            number(row.gpu_ms); out << ',' << row.width << ',' << row.height << ',' << row.slot << ',' << outcome_name(row.outcome) << '\n';
        }
        out.close();
        auto meta_path = path; meta_path += L".meta.json";
        std::ofstream meta(meta_path, std::ios::binary);
        meta.exceptions(std::ios::badbit | std::ios::failbit); meta.imbue(std::locale::classic());
        const auto m = snapshot();
        meta << std::setprecision(15) << "{\n  \"schema_version\": 1,\n  \"build_revision\": \"" << SVAI_BUILD_REVISION
             << "\",\n  \"clock\": \"QPC milliseconds; WGC TimeSpan converted from 100 ns\",\n  \"pixel_format\": \"SDR BGRA8; alpha ignored\",\n  \"adapter\": " << json_string(m.adapter)
             << ",\n  \"adapter_luid_high\": " << m.adapter_luid_high << ",\n  \"adapter_luid_low\": " << m.adapter_luid_low
             << ",\n  \"start_qpc_ms\": " << s.start_time << ",\n  \"end_qpc_ms\": " << s.end_time
             << ",\n  \"final_state\": " << json_string(state_name(m.state)) << ",\n  \"detail\": " << json_string(m.detail)
             << ",\n  \"pool_buffers\": " << pool_size << ",\n  \"ring_slots\": " << ring_size << ",\n  \"peak_in_flight\": " << m.peak_in_flight
             << ",\n  \"received\": " << m.received << ",\n  \"completed\": " << m.completed << ",\n  \"copied_bytes\": " << m.copied_bytes
             << ",\n  \"callback_notifications\": " << m.callback_notifications << ",\n  \"callback_gaps_over_100_ms\": " << m.callback_gaps
             << ",\n  \"trace_capacity\": " << trace_capacity << ",\n  \"trace_full\": " << (m.trace_full ? "true" : "false") << "\n}\n";
        meta.close();
        return true;
    } catch (const std::exception& ex) { error = winrt::to_hstring(ex.what()).c_str(); return false; }
}
} // namespace sekiro
