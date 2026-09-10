#pragma once
#include <SekiroVisionAI/CaptureEngine.h>
#include <shlobj.h>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <stdexcept>

namespace sekiro {
inline std::filesystem::path mvp_data_directory() {
    PWSTR path{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path)))
        throw std::runtime_error("Could not find LocalAppData");
    auto result = std::filesystem::path(path) / L"SekiroVisionAI";
    CoTaskMemFree(path);
    std::filesystem::create_directories(result);
    return result;
}
inline std::string json_quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c=='"' || c=='\\') out << '\\' << c;
        else if (c<32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}

class EventLog {
public:
    EventLog() {
        auto dir=mvp_data_directory()/L"logs"; std::filesystem::create_directories(dir);
        SYSTEMTIME time{}; GetSystemTime(&time);
        std::wostringstream name;
        name << L"autododge-" << std::setfill(L'0') << std::setw(4) << time.wYear << std::setw(2) << time.wMonth << std::setw(2) << time.wDay
             << L"-" << std::setw(2) << time.wHour << std::setw(2) << time.wMinute << std::setw(2) << time.wSecond << L"-" << GetCurrentProcessId() << L".jsonl";
        path_=dir/name.str();
        worker_=std::thread([this] { run(); });
    }
    ~EventLog() { { std::lock_guard lock(mutex_); quit_=true; } cv_.notify_one(); if(worker_.joinable()) worker_.join(); }
    void emit(const std::string& event, const std::string& detail, std::uint64_t episode=0) {
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << std::fixed << std::setprecision(3) << "{\"qpc_ms\":" << qpc_ms() << ",\"event\":" << json_quote(event)
             << ",\"episode\":" << episode << ",\"detail\":" << json_quote(detail) << "}";
        {
            std::lock_guard lock(mutex_);
            recent_.push_back(event+" | "+detail);
            if(recent_.size()>60) recent_.pop_front();
            if(queue_.size()>=4096) { ++dropped_; return; }
            queue_.push_back(line.str());
        }
        cv_.notify_one();
    }
    std::vector<std::string> recent() const { std::lock_guard lock(mutex_); return {recent_.begin(),recent_.end()}; }
    std::filesystem::path path() const { return path_; }
    bool healthy() const { return healthy_.load(); }
    std::uint64_t dropped() const { return dropped_.load(); }
private:
    void run() noexcept {
        try {
            std::ofstream stream(path_,std::ios::binary);
            stream.exceptions(std::ios::badbit|std::ios::failbit);
            std::uint64_t bytes=0; unsigned part=0;
            for (;;) {
                std::deque<std::string> batch;
                { std::unique_lock lock(mutex_); cv_.wait(lock,[&]{return quit_||!queue_.empty();});
                  batch.swap(queue_); if(batch.empty()&&quit_) break; }
                for (const auto& line : batch) {
                    if(bytes>8*1024*1024) {
                        stream.close(); auto rotated=path_; rotated+=L"."+std::to_wstring((++part)%3);
                        stream.open(rotated,std::ios::binary|std::ios::trunc); bytes=0;
                    }
                    stream << line << '\n'; bytes+=line.size()+1;
                }
                stream.flush();
            }
        } catch (...) { healthy_=false; }
    }
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_,recent_;
    bool quit_{};
    std::atomic_bool healthy_{true};
    std::atomic_uint64_t dropped_{};
    std::thread worker_;
};
}
