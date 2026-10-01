#include "dbms/sql/cli.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

struct SqlSplitResult
{
    std::vector<std::string> complete_statements;
    std::string tail;
};

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
                result.complete_statements.push_back(statement);
            }
            current.clear();
        }
    }
    result.tail = current;
    return result;
}

bool is_exit_command(const std::string& statement)
{
    std::string normalized = trim_copy(statement);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    return normalized == "exit;" || normalized == "quit;";
}

bool execute_sql_and_print(dbms::SqlApi& api, const std::string& sql)
{
    const dbms::SqlResponse response = api.execute_sql(sql);
    if (!response.ok) {
        std::cout << "ERROR: " << response.error << '\n';
        return false;
    }
    if (response.is_select) {
        std::cout << response.json << '\n';
    } else {
        std::cout << "OK\n";
    }
    return true;
}

bool run_script_file(dbms::SqlApi& api, const std::string& script_path)
{
    std::ifstream input(script_path);
    if (!input.is_open()) {
        std::cout << "ERROR: cannot open script file: " << script_path << '\n';
        return false;
    }

    const std::string script((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>()); //способ прочитать весь файл в одну строку
    const SqlSplitResult split = split_sql_statements(script);
    std::vector<std::string> statements = split.complete_statements;
    const std::string tail = trim_copy(split.tail);
    if (!tail.empty()) {
        statements.push_back(tail);
    }

    bool all_ok = true;
    for (const std::string& statement : statements) {
        if (!execute_sql_and_print(api, statement)) {
            all_ok = false;
        }
    }
    return all_ok;
}

void run_interactive(dbms::SqlApi& api)
{
    std::cout << "Good Luck\n";

    std::string line;
    std::string pending_sql;
    while (true) {
        std::cout << (pending_sql.empty() ? "> " : "... ");
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty() && pending_sql.empty()) {
            continue;
        }
        pending_sql.append(line);
        pending_sql.push_back('\n');

        const SqlSplitResult split = split_sql_statements(pending_sql);
        for (const std::string& statement : split.complete_statements) {
            if (is_exit_command(statement)) {
                return;
            }
            (void)execute_sql_and_print(api, statement);
        }

        pending_sql = split.tail;
        if (trim_copy(pending_sql).empty()) {
            pending_sql.clear();
        }
    }
}

} // namespace

namespace dbms
{

int run_cli(SqlApi& api, int argc, char** argv)
{
    if (argc == 1) {
        run_interactive(api);
        return 0;
    }
    if (argc == 2) {
        return run_script_file(api, argv[1]) ? 0 : 1;
    }

    std::cout << "Usage: ./prog [script.txt]\n";
    return 1;
}

}
