#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dbms/core/dbms.h"
#include "dbms/sql/cli.h"
#include "dbms/sql/sql_api.h"

namespace
{

struct CliRunResult
{
    int exit_code = 0;
    std::string stdout_text;
};

std::filesystem::path prepare_data_root(const std::string& test_name)
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("coursework_dbms_cli_tests_" + test_name);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    return root;
}

std::size_t count_occurrences(const std::string& text, const std::string& needle)
{
    if (needle.empty()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos = 0;
    while (true) {
        pos = text.find(needle, pos);
        if (pos == std::string::npos) {
            break;
        }
        ++count;
        pos += needle.size();
    }
    return count;
}

std::optional<nlohmann::json> try_parse_last_json_array(const std::string& stdout_text)
{
    const std::size_t begin = stdout_text.rfind('[');
    if (begin == std::string::npos) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(stdout_text.substr(begin));
    } catch (...) {
        return std::nullopt;
    }
}

CliRunResult run_cli_capture(dbms::SqlApi& api, std::vector<std::string> args)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }

    std::ostringstream captured;
    std::streambuf* const old_buf = std::cout.rdbuf(captured.rdbuf());
    const int exit_code = dbms::run_cli(api, static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old_buf);

    return CliRunResult{exit_code, captured.str()};
}

CliRunResult run_script(const std::string& test_name, const std::string& script)
{
    const std::filesystem::path root = prepare_data_root(test_name);
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    const std::filesystem::path script_path = root / "script.sql";
    std::ofstream out(script_path);
    EXPECT_TRUE(out.is_open());
    out << script;
    out.close();

    return run_cli_capture(api, {"prog", script_path.string()});
}

TEST(cliScriptTests, missingScriptFileReturnsNonZero)
{
    const std::filesystem::path root = prepare_data_root("missing_script_file");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    const std::filesystem::path missing_script = root / "missing.sql";
    const CliRunResult result = run_cli_capture(api, {"prog", missing_script.string()});

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stdout_text.find("ERROR: cannot open script file:"), std::string::npos);
}

TEST(cliScriptTests, invalidArgcPrintsUsageAndReturnsNonZero)
{
    const std::filesystem::path root = prepare_data_root("invalid_argc");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    const CliRunResult result = run_cli_capture(api, {"prog", "first.sql", "second.sql"});

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stdout_text.find("Usage: ./prog [script.txt]"), std::string::npos);
}

TEST(cliScriptTests, multilineScriptExecutesSuccessfully)
{
    const std::string script =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (\n"
        "  id INT INDEXED,\n"
        "  name STRING\n"
        ");\n"
        "INSERT INTO users (\n"
        "  id,\n"
        "  name\n"
        ") VALUE (\n"
        "  1,\n"
        "  \"alice\"\n"
        ");\n"
        "SELECT id, name\n"
        "FROM users\n"
        "WHERE id == 1;\n";

    const CliRunResult result = run_script("multiline_script_success", script);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(count_occurrences(result.stdout_text, "OK\n"), 4u);

    const auto parsed = try_parse_last_json_array(result.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 1u);
    EXPECT_EQ((*parsed)[0]["id"], 1);
    EXPECT_EQ((*parsed)[0]["name"], "alice");
}

TEST(cliScriptTests, continuesAfterSqlErrorAndReturnsNonZero)
{
    const std::string script =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (id INT INDEXED, name STRING);\n"
        "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"
        "BAD SQL;\n"
        "SELECT id, name FROM users WHERE id == 1;\n";

    const CliRunResult result = run_script("continues_after_error", script);

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stdout_text.find("ERROR:"), std::string::npos);

    const auto parsed = try_parse_last_json_array(result.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 1u);
    EXPECT_EQ((*parsed)[0]["id"], 1);
    EXPECT_EQ((*parsed)[0]["name"], "alice");
}

TEST(cliScriptTests, semicolonInsideStringIsNotStatementDelimiter)
{
    const std::string script =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (id INT INDEXED, name STRING);\n"
        "INSERT INTO users (id, name) VALUE (1, \"a;bc\");\n"
        "SELECT id, name FROM users WHERE id == 1;\n";

    const CliRunResult result = run_script("semicolon_inside_string", script);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(count_occurrences(result.stdout_text, "OK\n"), 4u);

    const auto parsed = try_parse_last_json_array(result.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 1u);
    EXPECT_EQ((*parsed)[0]["id"], 1);
    EXPECT_EQ((*parsed)[0]["name"], "a;bc");
}

TEST(cliScriptTests, tailStatementWithoutFinalSemicolonFails)
{
    const std::string script =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (id INT INDEXED, name STRING);\n"
        "INSERT INTO users (id, name) VALUE (1, \"tail\");\n"
        "SELECT id, name FROM users WHERE id == 1\n";

    const CliRunResult result = run_script("tail_without_semicolon", script);

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_NE(result.stdout_text.find("ERROR:"), std::string::npos);
    EXPECT_EQ(count_occurrences(result.stdout_text, "OK\n"), 4u);
}

TEST(cliScriptTests, stressScriptWithManyStatementsStaysStable)
{
    std::ostringstream script;
    script << "CREATE DATABASE test;\n";
    script << "USE test;\n";
    script << "CREATE TABLE users (id INT INDEXED, name STRING);\n";
    for (int i = 1; i <= 250; ++i) {
        script << "INSERT INTO users (id, name) VALUE (" << i << ", \"u" << i << "\");\n";
    }
    script << "SELECT id FROM users WHERE id >= 240;\n";

    const CliRunResult result = run_script("stress_many_statements", script.str());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(count_occurrences(result.stdout_text, "OK\n"), 253u);

    const auto parsed = try_parse_last_json_array(result.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 11u);

    std::vector<int> ids;
    ids.reserve(parsed->size());
    for (const auto& row : *parsed) {
        ids.push_back(row["id"].get<int>());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids.front(), 240);
    EXPECT_EQ(ids.back(), 250);
}

} // namespace
