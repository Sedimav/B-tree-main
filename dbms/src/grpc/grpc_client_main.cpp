#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "sql_service.grpc.pb.h"

namespace
{

bool is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string trim_copy(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && is_space(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool is_escaped_quote(const std::string& text, std::size_t quote_index)
{
    std::size_t slash_count = 0;
    for (std::size_t i = quote_index; i > 0; --i) {
        if (text[i - 1] != '\\') {
            break;
        }
        ++slash_count;
    }
    return (slash_count % 2u) != 0u;
}

struct SqlSplitResult
{
    std::vector<std::string> complete;
    std::string tail;
};

SqlSplitResult split_sql_statements(const std::string& script)
{
    SqlSplitResult result;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (std::size_t i = 0; i < script.size(); ++i) {
        const char ch = script[i];
        current.push_back(ch);

        if (ch == '\'' && !in_double_quote && !is_escaped_quote(script, i)) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (ch == '"' && !in_single_quote && !is_escaped_quote(script, i)) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if (ch == ';' && !in_single_quote && !in_double_quote) {
            const std::string statement = trim_copy(current);
            if (!statement.empty()) {
                result.complete.push_back(statement);
            }
            current.clear();
        }
    }
    result.tail = current;
    return result;
}

bool is_exit_command(const std::string& s)
{
    std::string t = trim_copy(s);
    if (!t.empty() && t.back() == ';') {
        t.pop_back();
        t = trim_copy(t);
    }
    return t == "exit" || t == "quit";
}

class GrpcSqlClient
{
public:
    explicit GrpcSqlClient(std::shared_ptr<grpc::Channel> channel)
        : _stub(dbms::rpc::SqlService::NewStub(std::move(channel)))
    {
    }

    bool open_session()
    {
        grpc::ClientContext context;
        dbms::rpc::OpenSessionRequest request;
        dbms::rpc::OpenSessionResponse response;
        const grpc::Status status = _stub->OpenSession(&context, request, &response);
        if (!status.ok()) {
            std::cout << "ERROR: transport failed: " << status.error_message() << '\n';
            return false;
        }
        if (!response.ok()) {
            std::cout << "ERROR: " << response.error() << '\n';
            return false;
        }
        _session_id = response.session_id();
        return true;
    }

    bool execute_and_print(const std::string& sql) const
    {
        grpc::ClientContext context;
        dbms::rpc::ExecuteRequest request;
        request.set_session_id(_session_id);
        request.set_sql(sql);

        dbms::rpc::ExecuteResponse response;
        const grpc::Status status = _stub->Execute(&context, request, &response);
        if (!status.ok()) {
            std::cout << "ERROR: transport failed: " << status.error_message() << '\n';
            return false;
        }
        if (!response.ok()) {
            std::cout << "ERROR: " << response.error() << '\n';
            return false;
        }
        if (response.is_select()) {
            std::cout << response.json() << '\n';
        } else {
            std::cout << "OK\n";
        }
        return true;
    }

    void close_session() const
    {
        if (_session_id.empty()) {
            return;
        }
        grpc::ClientContext context;
        dbms::rpc::CloseSessionRequest request;
        request.set_session_id(_session_id);
        dbms::rpc::CloseSessionResponse response;
        (void)_stub->CloseSession(&context, request, &response);
    }

private:
    std::unique_ptr<dbms::rpc::SqlService::Stub> _stub;
    std::string _session_id;
};

bool run_script(GrpcSqlClient& client, const std::string& script_path)
{
    std::ifstream input(script_path);
    if (!input.is_open()) {
        std::cout << "ERROR: cannot open script file: " << script_path << '\n';
        return false;
    }

    const std::string script((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    const SqlSplitResult split = split_sql_statements(script);
    std::vector<std::string> statements = split.complete;
    const std::string tail = trim_copy(split.tail);
    if (!tail.empty()) {
        statements.push_back(tail);
    }

    bool all_ok = true;
    for (const std::string& statement : statements) {
        if (!client.execute_and_print(statement)) {
            all_ok = false;
        }
    }
    return all_ok;
}

void run_interactive(GrpcSqlClient& client)
{
    std::cout << "DBMS gRPC SQL client. Commands end with ';'.\n";

    std::string line;
    std::string pending;
    while (true) {
        std::cout << (pending.empty() ? "> " : "... ");
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty() && pending.empty()) {
            continue;
        }
        if (pending.empty() && is_exit_command(line)) {
            break;
        }

        pending += line;
        pending += '\n';

        const SqlSplitResult split = split_sql_statements(pending);
        for (const std::string& stmt : split.complete) {
            if (is_exit_command(stmt)) {
                return;
            }
            (void)client.execute_and_print(stmt);
        }
        pending = split.tail;
        if (trim_copy(pending).empty()) {
            pending.clear();
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string address = "127.0.0.1:50051";
    std::string script_path;

    if (argc >= 2) {
        address = argv[1];
    }
    if (argc >= 3) {
        script_path = argv[2];
    }
    if (argc > 3) {
        std::cout << "Usage: ./dbms_grpc_client [address] [script.sql]\n";
        return 1;
    }

    GrpcSqlClient client(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
    if (!client.open_session()) {
        return 1;
    }

    int exit_code = 0;
    if (script_path.empty()) {
        run_interactive(client);
    } else {
        exit_code = run_script(client, script_path) ? 0 : 1;
    }

    client.close_session();
    return exit_code;
}
