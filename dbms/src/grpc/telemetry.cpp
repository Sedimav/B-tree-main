#include "dbms/grpc/telemetry.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace dbms
{
namespace grpc_api
{

TelemetryCollector::TelemetryCollector(const std::filesystem::path& log_path)
    : _file(log_path, std::ios::app)
{
    _thread = std::thread(&TelemetryCollector::background_loop, this);
}

TelemetryCollector::~TelemetryCollector()
{
    _stop.store(true);
    _thread.join();
}

void TelemetryCollector::record(bool ok, std::chrono::microseconds duration)
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const int idx = static_cast<int>(now % kWindowMax);

    std::lock_guard<std::mutex> lock(_mutex);
    if (_buckets[idx].timestamp != now) {
        _buckets[idx] = Bucket{};
        _buckets[idx].timestamp = now;
    }
    _buckets[idx].requests++;
    if (!ok) {
        _buckets[idx].errors++;
    }
    _buckets[idx].total_us += static_cast<uint64_t>(duration.count());
}

void TelemetryCollector::background_loop()
{
    while (!_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (_stop.load()) {
            break;
        }
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        print_metrics(now);
    }
}

void TelemetryCollector::print_metrics(int64_t now)
{
    uint64_t rps_cur       = 0;
    uint64_t rps_10m_sum   = 0;
    uint64_t rps_10m_max   = 0;
    uint64_t rps_10m_count = 0;
    uint64_t time_10s_sum  = 0;
    uint64_t time_10s_reqs = 0;
    uint64_t err_1m_errors = 0;
    uint64_t err_1m_reqs   = 0;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (int i = 0; i < kWindowMax; ++i) {
            const Bucket& b = _buckets[i];
            if (b.timestamp <= 0) {
                continue;
            }
            const int64_t age = now - b.timestamp;
            if (age < 0 || age > kWindowMax) {
                continue;
            }

            // текущий RPS — предыдущая секунда
            if (age == 1) {
                rps_cur = b.requests;
            }

            // avg/max RPS за 10 минут
            if (age <= 600) {
                rps_10m_sum += b.requests;
                rps_10m_count++;
                if (b.requests > rps_10m_max) {
                    rps_10m_max = b.requests;
                }
            }

            // среднее время обработки за 10 секунд
            if (age <= 10) {
                time_10s_sum  += b.total_us;
                time_10s_reqs += b.requests;
            }

            // error rate за 1 минуту
            if (age <= 60) {
                err_1m_errors += b.errors;
                err_1m_reqs   += b.requests;
            }
        }
    }

    const double rps_10m_avg = rps_10m_count > 0
        ? static_cast<double>(rps_10m_sum) / static_cast<double>(rps_10m_count)
        : 0.0;

    const double avg_time_us = time_10s_reqs > 0
        ? static_cast<double>(time_10s_sum) / static_cast<double>(time_10s_reqs)
        : 0.0;

    const double err_rate = err_1m_reqs > 0
        ? 100.0 * static_cast<double>(err_1m_errors) / static_cast<double>(err_1m_reqs)
        : 0.0;

    std::ostringstream oss;
    oss << '[' << format_now(now) << ']'
        << " RPS(cur)=" << rps_cur
        << std::fixed << std::setprecision(2)
        << " RPS(10m_avg)=" << rps_10m_avg
        << " RPS(10m_max)=" << rps_10m_max
        << " avg_time(10s)=" << static_cast<uint64_t>(avg_time_us) << "us"
        << " err_rate(1m)=" << err_rate << "%";

    const std::string line = oss.str();
    std::cout << line << '\n';

    if (_file.is_open()) {
        _file << line << '\n';
        _file.flush();
    }
}

std::string TelemetryCollector::format_now(int64_t unix_sec)
{
    const std::time_t t = static_cast<std::time_t>(unix_sec);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

}
}
