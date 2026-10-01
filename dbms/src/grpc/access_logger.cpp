#include "dbms/grpc/access_logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace dbms
{
namespace grpc_api
{

AccessLogger::AccessLogger(const std::filesystem::path& log_path)
    : _file(log_path, std::ios::app)
{
}

void AccessLogger::log(const std::string& client_id,
                       const std::string& handler_id,
                       const std::string& body,
                       std::chrono::system_clock::time_point start,
                       std::chrono::system_clock::time_point end,
                       bool ok,
                       const std::string& status)
{
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::lock_guard<std::mutex> lock(_mutex);
    _file << '[' << format_time(start) << ']'
          << " client=" << client_id
          << " handler=" << handler_id
          << " duration=" << duration_us << "us"
          << " ok=" << (ok ? "true" : "false")
          << " status=" << status
          << " body=" << body
          << '\n';
    _file.flush();
}

std::string AccessLogger::format_time(std::chrono::system_clock::time_point tp)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tp.time_since_epoch()) % 1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    std::ostringstream oss;
    oss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

}
}
