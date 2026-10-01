#ifndef COURSEWORK_DBMS_GRPC_TELEMETRY_H
#define COURSEWORK_DBMS_GRPC_TELEMETRY_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace dbms
{
namespace grpc_api
{

class TelemetryCollector
{
public:
    explicit TelemetryCollector(const std::filesystem::path& log_path);
    ~TelemetryCollector();

    void record(bool ok, std::chrono::microseconds duration);

private:
    struct Bucket
    {
        int64_t  timestamp = 0;
        uint64_t requests  = 0;
        uint64_t errors    = 0;
        uint64_t total_us  = 0;
    };

    static constexpr int kWindowMax = 600; // 10 минут — максимальное окно

    void background_loop();
    void print_metrics(int64_t now);
    static std::string format_now(int64_t unix_sec);

    std::array<Bucket, kWindowMax> _buckets{};
    std::mutex   _mutex;
    std::ofstream _file;
    std::thread  _thread;
    std::atomic<bool> _stop{false};
};

}
}

#endif
