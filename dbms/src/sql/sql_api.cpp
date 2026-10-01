#include "dbms/sql/sql_api.h"
#include "dbms/sql/parser.h"
#include "dbms/sql/lexer.h"
namespace dbms
{

SqlResponse SqlApi::execute_sql(const std::string& sql)
{
    std::lock_guard<std::mutex> lock(_mutex);
    SqlResponse resp{};

    if (sql.find_first_not_of(" \t\r\n") == std::string::npos) {
        resp.error = "Empty SQL";
        return resp;
    }

    try {
        std::vector<Token> tokens;
        try {
            Lexer lexer(sql);
            tokens = lexer.tokenize();
        } catch (const std::exception& e) {
            resp.error = std::string("Lexer error: ") + e.what();
            return resp;
        }

        Statement stmt;
        try {
            Parser parser(tokens);
            stmt = parser.parse_statement();
        } catch (const std::exception& e) {
            resp.error = std::string("Parser error: ") + e.what();
            return resp;
        }

        const bool is_select = std::holds_alternative<SelectStmt>(stmt);

        if (!_exec.execute(stmt)) {
            resp.error = _exec.last_error().empty()
                ? "Runtime error: statement was rejected without details"
                : _exec.last_error();
            return resp;
        }

        resp.ok = true;
        resp.is_select = is_select;
        if (is_select) {
            resp.json = _exec.last_select_json();
        }
        return resp;
    } catch (const std::exception& e) {
        resp.error = std::string("Runtime error: ") + e.what();
        return resp;
    }
}

}
