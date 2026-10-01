#ifndef COURSEWORK_DBMS_GRPC_ACCESS_LOGGER_H
#define COURSEWORK_DBMS_GRPC_ACCESS_LOGGER_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>


namespace dbms
{
namespace grpc_api
{

class AccessLogger
{
public:
    explicit AccessLogger(const std::filesystem::path& log_path);

    void log(const std::string& client_id,
             const std::string& handler_id,
             const std::string& body,
             std::chrono::system_clock::time_point start,
             std::chrono::system_clock::time_point end,
             bool ok,
             const std::string& status);

private:
    static std::string format_time(std::chrono::system_clock::time_point tp);

    std::ofstream _file;
    std::mutex _mutex;
};

}
}

#endif
