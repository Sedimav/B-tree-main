#include "dbms/grpc/sql_service_impl.h"

#include <sstream>
#include <thread>

namespace dbms
{
namespace grpc_api
{

static std::string current_thread_id()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

grpc::Status SqlServiceImpl::OpenSession(grpc::ServerContext* context,
                                         const dbms::rpc::OpenSessionRequest* request,
                                         dbms::rpc::OpenSessionResponse* response)
{
    (void)context;
    (void)request;

    const auto start = std::chrono::system_clock::now();
    const std::string handler_id = current_thread_id();

    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        session_id = std::to_string(_next_session_id++);
        _sessions.emplace(session_id, std::make_unique<SqlApi>(_engine));
    }

    const auto end = std::chrono::system_clock::now();
    _logger.log(session_id, handler_id, "OpenSession", start, end, true, "ok");

    response->set_ok(true);
    response->set_session_id(session_id);
    response->clear_error();
    return grpc::Status::OK;
}

grpc::Status SqlServiceImpl::Execute(grpc::ServerContext* context,
                                     const dbms::rpc::ExecuteRequest* request,
                                     dbms::rpc::ExecuteResponse* response)
{
    (void)context;

    const auto start = std::chrono::system_clock::now();
    const std::string handler_id = current_thread_id();
    const std::string client_id = request->session_id();

    SqlResponse result;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(client_id);
        if (it == _sessions.end()) {
            const auto end = std::chrono::system_clock::now();
            _logger.log(client_id, handler_id, request->sql(), start, end, false, "Unknown session");
            response->set_ok(false);
            response->set_is_select(false);
            response->clear_json();
            response->set_error("Unknown session");
            return grpc::Status::OK;
        }
        result = it->second->execute_sql(request->sql());
    }

    const auto end = std::chrono::system_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const std::string status = result.ok ? "ok" : result.error;
    _logger.log(client_id, handler_id, request->sql(), start, end, result.ok, status);
    _telemetry.record(result.ok, duration);

    response->set_ok(result.ok);
    response->set_is_select(result.is_select);
    response->set_json(result.json);
    response->set_error(result.error);
    return grpc::Status::OK;
}

grpc::Status SqlServiceImpl::CloseSession(grpc::ServerContext* context,
                                          const dbms::rpc::CloseSessionRequest* request,
                                          dbms::rpc::CloseSessionResponse* response)
{
    (void)context;

    const auto start = std::chrono::system_clock::now();
    const std::string handler_id = current_thread_id();
    const std::string client_id = request->session_id();

    bool ok = false;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto erased = _sessions.erase(client_id);
        if (erased == 0u) {
            status = "Unknown session";
        } else {
            ok = true;
            status = "ok";
        }
    }

    const auto end = std::chrono::system_clock::now();
    _logger.log(client_id, handler_id, "CloseSession", start, end, ok, status);

    if (!ok) {
        response->set_ok(false);
        response->set_error(status);
        return grpc::Status::OK;
    }

    response->set_ok(true);
    response->clear_error();
    return grpc::Status::OK;
}

}
}
