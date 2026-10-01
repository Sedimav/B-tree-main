#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "dbms/core/dbms.h"
#include "dbms/index/index_manager.h"
#include "dbms/index/string_key.h"
#include "dbms/sql/cli.h"
#include "dbms/sql/sql_api.h"
#include "dbms/storage/record_codec.h"
#include "dbms/storage/table_page_manager.h"

namespace
{

struct CliRunResult
{
    int exit_code = 0;
    std::string stdout_text;
};

std::filesystem::path prepare_data_root(const std::string& test_name)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("coursework_dbms_tz_point0_" + test_name);
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
    const std::size_t end = stdout_text.rfind(']');
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(stdout_text.substr(begin, end - begin + 1));
    } catch (...) {
        return std::nullopt;
    }
}

CliRunResult run_cli_capture(dbms::SqlApi& api,
                             const std::vector<std::string>& args,
                             const std::string& stdin_text = {})
{
    std::vector<std::string> args_copy = args;
    std::vector<char*> argv;
    argv.reserve(args_copy.size());
    for (std::string& arg : args_copy) {
        argv.push_back(arg.data());
    }

    std::ostringstream captured;
    std::istringstream input(stdin_text);

    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_in = std::cin.rdbuf(input.rdbuf());
    const int exit_code = dbms::run_cli(api, static_cast<int>(argv.size()), argv.data());
    std::cin.rdbuf(old_in);
    std::cout.rdbuf(old_out);

    return CliRunResult{exit_code, captured.str()};
}

CliRunResult run_script(const std::string& test_name, const std::string& script)
{
    const std::filesystem::path root = prepare_data_root(test_name);
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    const std::filesystem::path script_path = root / "script.sql";
    std::ofstream out(script_path);
    if (out.is_open()) {
        out << script;
    }
    out.close();

    return run_cli_capture(api, {"prog", script_path.string()});
}

void expect_sql_ok(dbms::SqlApi& api, const std::string& sql)
{
    const dbms::SqlResponse response = api.execute_sql(sql);
    ASSERT_TRUE(response.ok) << "SQL failed: `" << sql << "`, error: " << response.error;
}

dbms::SqlResponse run_sql(dbms::SqlApi& api, const std::string& sql)
{
    return api.execute_sql(sql);
}

void force_table_header_next_page(const std::filesystem::path& table_file, int next_page_id)
{
    dbms::TablePageManager manager(table_file.string());
    dbms::Page page;
    ASSERT_TRUE(manager.read_page(0, page));
    dbms::TableHeader header{};
    std::memcpy(&header, page.data().data(), sizeof(dbms::TableHeader));
    header.next_page_id = next_page_id;
    std::memcpy(page.data().data(), &header, sizeof(dbms::TableHeader));
    ASSERT_TRUE(manager.write_page(0, page));
}

} // namespace

TEST(tzPoint0Pass, SupportsIntAndStringDataTypes)
{
    dbms::Dbms engine(prepare_data_root("supports_int_string"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
    ASSERT_TRUE(select.ok);
    ASSERT_TRUE(select.is_select);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0]["id"].is_number_integer());
    EXPECT_TRUE(result[0]["name"].is_string());
}

TEST(tzPoint0Pass, SupportsDatabaseHierarchyAndContextSwitch)
{
    dbms::Dbms engine(prepare_data_root("db_hierarchy_context"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE db1;");
    expect_sql_ok(api, "CREATE DATABASE db2;");

    expect_sql_ok(api, "USE db1;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"from_db1\");");

    expect_sql_ok(api, "USE db2;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"from_db2\");");

    {
        expect_sql_ok(api, "USE db1;");
        const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE id == 1;");
        ASSERT_TRUE(select.ok);
        const nlohmann::json result = nlohmann::json::parse(select.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["name"], "from_db1");
    }

    {
        expect_sql_ok(api, "USE db2;");
        const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE id == 1;");
        ASSERT_TRUE(select.ok);
        const nlohmann::json result = nlohmann::json::parse(select.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["name"], "from_db2");
    }
}

TEST(tzPoint0Pass, BatchModeWorksWithScriptFile)
{
    const std::string script =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (id INT INDEXED, name STRING);\n"
        "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"
        "SELECT id, name FROM users WHERE id == 1;\n";

    const CliRunResult run = run_script("batch_mode_script", script);
    EXPECT_EQ(run.exit_code, 0);
    EXPECT_EQ(count_occurrences(run.stdout_text, "OK\n"), 4u);
    const auto parsed = try_parse_last_json_array(run.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 1u);
}

TEST(tzPoint0Pass, InteractiveModeWorksForSingleLineCommands)
{
    dbms::Dbms engine(prepare_data_root("interactive_single_line"));
    dbms::SqlApi api(engine);
    const std::string input =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (id INT INDEXED, name STRING);\n"
        "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"
        "SELECT id, name FROM users WHERE id == 1;\n"
        "exit;\n";

    const CliRunResult run = run_cli_capture(api, {"prog"}, input);
    EXPECT_EQ(run.exit_code, 0);
    EXPECT_EQ(count_occurrences(run.stdout_text, "OK\n"), 4u);
    const auto parsed = try_parse_last_json_array(run.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_array());
    ASSERT_EQ(parsed->size(), 1u);
}

TEST(tzPoint0SpecGaps, InteractiveModeShouldSupportMultilineStatementsBySemicolon)
{
    dbms::Dbms engine(prepare_data_root("interactive_multiline"));
    dbms::SqlApi api(engine);
    const std::string input =
        "CREATE DATABASE test;\n"
        "USE test;\n"
        "CREATE TABLE users (\n"
        "  id INT INDEXED,\n"
        "  name STRING\n"
        ");\n"
        "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"
        "SELECT id, name FROM users WHERE id == 1;\n"
        "exit;\n";

    const CliRunResult run = run_cli_capture(api, {"prog"}, input);
    EXPECT_EQ(run.exit_code, 0);
    EXPECT_EQ(run.stdout_text.find("ERROR:"), std::string::npos);
    const auto parsed = try_parse_last_json_array(run.stdout_text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1u);
}

TEST(tzPoint0Pass, WritesTableAndIndexFilesToFilesystem)
{
    const std::filesystem::path root = prepare_data_root("writes_files");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users.tbl"));
    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users__id.idx"));
}

TEST(tzPoint0SpecGaps, DataShouldSurviveRestartWithSameDataRoot)
{
    const std::filesystem::path root = prepare_data_root("restart_persistence");
    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);
        expect_sql_ok(api, "CREATE DATABASE test;");
        expect_sql_ok(api, "USE test;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
        expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");
    }

    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);
        const dbms::SqlResponse use = run_sql(api, "USE test;");
        ASSERT_TRUE(use.ok) << use.error;
        const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
        ASSERT_TRUE(select.ok) << select.error;
        const nlohmann::json result = nlohmann::json::parse(select.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["id"], 1);
        EXPECT_EQ(result[0]["name"], "alice");
    }
}

TEST(tzPoint0Pass, NotNullAndDefaultNullableRulesWork)
{
    dbms::Dbms engine(prepare_data_root("not_null_nullable"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT NOT NULL, name STRING);");

    {
        const dbms::SqlResponse ok_insert = run_sql(api, "INSERT INTO users (id) VALUE (1);");
        EXPECT_TRUE(ok_insert.ok) << ok_insert.error;
    }
    {
        const dbms::SqlResponse bad_insert = run_sql(api, "INSERT INTO users (name) VALUE (\"x\");");
        EXPECT_FALSE(bad_insert.ok);
        EXPECT_FALSE(bad_insert.error.empty());
    }
    {
        const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
        ASSERT_TRUE(select.ok);
        const nlohmann::json result = nlohmann::json::parse(select.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_TRUE(result[0]["name"].is_null());
    }
}

TEST(tzPoint0Pass, IndexedConstraintIsUniqueAndNotNull)
{
    dbms::Dbms engine(prepare_data_root("indexed_unique_not_null"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    {
        const dbms::SqlResponse duplicate = run_sql(api, "INSERT INTO users (id, name) VALUE (1, \"dup\");");
        EXPECT_FALSE(duplicate.ok);
    }
    {
        const dbms::SqlResponse null_id = run_sql(api, "INSERT INTO users (id, name) VALUE (NULL, \"bad\");");
        EXPECT_FALSE(null_id.ok);
    }
}

TEST(tzPoint0Pass, InsertBatchRollsBackWhenLaterRowIsTooLarge)
{
    const std::filesystem::path root = prepare_data_root("insert_batch_large_row_rollback");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");

    const std::string huge_name(5000, 'x');
    const dbms::SqlResponse insert = run_sql(
        api,
        "INSERT INTO users (id, name) VALUE (1, \"ok\"), (2, \"" + huge_name + "\");");
    ASSERT_FALSE(insert.ok);
    EXPECT_NE(insert.error.find("record is too large"), std::string::npos);

    const dbms::SqlResponse select_all = run_sql(api, "SELECT * FROM users;");
    ASSERT_TRUE(select_all.ok);
    const nlohmann::json result = nlohmann::json::parse(select_all.json);
    EXPECT_TRUE(result.empty());

    dbms::IndexManager<int> index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_FALSE(index.find(1, rid));
    EXPECT_FALSE(index.find(2, rid));
}

TEST(tzPoint0Pass, InsertRollsBackTablePageWhenIndexCannotBeWritten)
{
    const std::filesystem::path root = prepare_data_root("insert_index_write_failure_rollback");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");

    const std::filesystem::path idx_path = root / "test" / "users__id.idx";
    std::error_code ec;
    std::filesystem::remove(idx_path, ec);
    std::filesystem::create_directories(idx_path, ec);

    const dbms::SqlResponse insert = run_sql(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");
    ASSERT_FALSE(insert.ok);
    EXPECT_NE(insert.error.find("cannot update one or more indexes"), std::string::npos);

    std::filesystem::remove_all(idx_path, ec);
    dbms::IndexManager<int> repaired_index(idx_path.string());

    const dbms::SqlResponse select_all = run_sql(api, "SELECT * FROM users;");
    ASSERT_TRUE(select_all.ok);
    const nlohmann::json result = nlohmann::json::parse(select_all.json);
    EXPECT_TRUE(result.empty());
}

TEST(tzPoint0Pass, InsertRollsBackPreviouslyWrittenIndexWhenLaterIndexFails)
{
    const std::filesystem::path root = prepare_data_root("insert_second_index_failure_rolls_back_first");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);");

    const std::filesystem::path name_idx_path = root / "test" / "users__name.idx";
    std::error_code ec;
    std::filesystem::remove(name_idx_path, ec);
    std::filesystem::create_directories(name_idx_path, ec);

    const dbms::SqlResponse insert = run_sql(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");
    ASSERT_FALSE(insert.ok);
    EXPECT_NE(insert.error.find("cannot update one or more indexes"), std::string::npos);

    dbms::IndexManager<int> id_index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_FALSE(id_index.find(1, rid));

    const dbms::SqlResponse select_all = run_sql(api, "SELECT * FROM users;");
    ASSERT_TRUE(select_all.ok);
    const nlohmann::json result = nlohmann::json::parse(select_all.json);
    EXPECT_TRUE(result.empty());
}

TEST(tzPoint0Pass, UpdateIndexedDuplicateKeepsTableAndIndexUnchanged)
{
    const std::filesystem::path root = prepare_data_root("update_duplicate_keeps_state");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");");

    const dbms::SqlResponse update = run_sql(api, "UPDATE users SET id = 1 WHERE id == 2;");
    ASSERT_FALSE(update.ok);
    EXPECT_NE(update.error.find("duplicate INDEXED key"), std::string::npos);

    const dbms::SqlResponse old_key = run_sql(api, "SELECT id, name FROM users WHERE id == 2;");
    ASSERT_TRUE(old_key.ok);
    const nlohmann::json old_key_result = nlohmann::json::parse(old_key.json);
    ASSERT_EQ(old_key_result.size(), 1u);
    EXPECT_EQ(old_key_result[0]["name"], "bob");

    const dbms::SqlResponse duplicate_key = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
    ASSERT_TRUE(duplicate_key.ok);
    const nlohmann::json duplicate_key_result = nlohmann::json::parse(duplicate_key.json);
    ASSERT_EQ(duplicate_key_result.size(), 1u);
    EXPECT_EQ(duplicate_key_result[0]["name"], "alice");
}

TEST(tzPoint0Pass, SuccessfulUpdateKeepsIndexConsistentAfterRestart)
{
    const std::filesystem::path root = prepare_data_root("update_restart_index_consistency");
    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);

        expect_sql_ok(api, "CREATE DATABASE test;");
        expect_sql_ok(api, "USE test;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
        expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");");
        expect_sql_ok(api, "UPDATE users SET id = 3 WHERE id == 2;");
    }

    dbms::Dbms restarted(root);
    dbms::SqlApi api(restarted);

    expect_sql_ok(api, "USE test;");
    const dbms::SqlResponse old_key = run_sql(api, "SELECT id, name FROM users WHERE id == 2;");
    ASSERT_TRUE(old_key.ok);
    EXPECT_TRUE(nlohmann::json::parse(old_key.json).empty());

    const dbms::SqlResponse new_key = run_sql(api, "SELECT id, name FROM users WHERE id == 3;");
    ASSERT_TRUE(new_key.ok);
    const nlohmann::json new_key_result = nlohmann::json::parse(new_key.json);
    ASSERT_EQ(new_key_result.size(), 1u);
    EXPECT_EQ(new_key_result[0]["name"], "bob");
}

TEST(tzPoint0Pass, UpdateRollsBackEarlierIndexWhenLaterIndexFails)
{
    const std::filesystem::path root = prepare_data_root("update_later_index_failure_rolls_back_first");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED, age INT);");
    expect_sql_ok(api, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);");

    const std::filesystem::path name_idx_path = root / "test" / "users__name.idx";
    std::error_code ec;
    std::filesystem::remove(name_idx_path, ec);
    std::filesystem::create_directories(name_idx_path, ec);

    const dbms::SqlResponse update = run_sql(api, "UPDATE users SET id = 10, name = \"ann\" WHERE age == 20;");
    ASSERT_FALSE(update.ok);
    EXPECT_NE(update.error.find("cannot update one or more indexes"), std::string::npos);

    dbms::IndexManager<int> id_index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_TRUE(id_index.find(1, rid));
    EXPECT_FALSE(id_index.find(10, rid));

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE age == 20;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 1);
    EXPECT_EQ(result[0]["name"], "alice");
}

TEST(tzPoint0Pass, DeleteMultipleRowsStaysConsistentAfterRestart)
{
    const std::filesystem::path root = prepare_data_root("delete_restart_consistency");
    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);

        expect_sql_ok(api, "CREATE DATABASE test;");
        expect_sql_ok(api, "USE test;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED, age INT);");
        expect_sql_ok(api,
                      "INSERT INTO users (id, name, age) VALUE "
                      "(1, \"alice\", 20), (2, \"bob\", 30), (3, \"carol\", 30), (4, \"dave\", 40);");
        expect_sql_ok(api, "DELETE FROM users WHERE age == 30;");
    }

    dbms::Dbms restarted(root);
    dbms::SqlApi api(restarted);

    expect_sql_ok(api, "USE test;");
    const dbms::SqlResponse all_rows = run_sql(api, "SELECT id, name FROM users;");
    ASSERT_TRUE(all_rows.ok);
    const nlohmann::json all_result = nlohmann::json::parse(all_rows.json);
    ASSERT_EQ(all_result.size(), 2u);

    const dbms::SqlResponse deleted_id = run_sql(api, "SELECT id FROM users WHERE id == 2;");
    ASSERT_TRUE(deleted_id.ok);
    EXPECT_TRUE(nlohmann::json::parse(deleted_id.json).empty());

    const dbms::SqlResponse deleted_name = run_sql(api, "SELECT name FROM users WHERE name == \"carol\";");
    ASSERT_TRUE(deleted_name.ok);
    EXPECT_TRUE(nlohmann::json::parse(deleted_name.json).empty());

    const dbms::SqlResponse kept_id = run_sql(api, "SELECT name FROM users WHERE id == 4;");
    ASSERT_TRUE(kept_id.ok);
    const nlohmann::json kept_result = nlohmann::json::parse(kept_id.json);
    ASSERT_EQ(kept_result.size(), 1u);
    EXPECT_EQ(kept_result[0]["name"], "dave");
}

TEST(tzPoint0Pass, DeleteWhereIndexedColumnClearsKeyAndAllowsReinsert)
{
    const std::filesystem::path root = prepare_data_root("delete_indexed_key_reinsert");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");
    expect_sql_ok(api, "DELETE FROM users WHERE id == 1;");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"again\");");

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "again");
}

TEST(tzPoint0Pass, DeleteRollsBackEarlierIndexWhenLaterIndexFails)
{
    const std::filesystem::path root = prepare_data_root("delete_later_index_failure_rolls_back_first");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED, age INT);");
    expect_sql_ok(api, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);");

    const std::filesystem::path name_idx_path = root / "test" / "users__name.idx";
    std::error_code ec;
    std::filesystem::remove(name_idx_path, ec);
    std::filesystem::create_directories(name_idx_path, ec);

    const dbms::SqlResponse del = run_sql(api, "DELETE FROM users WHERE age == 20;");
    ASSERT_FALSE(del.ok);
    EXPECT_NE(del.error.find("cannot update one or more indexes"), std::string::npos);

    dbms::IndexManager<int> id_index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_TRUE(id_index.find(1, rid));

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alice");
}

TEST(tzPoint0Pass, LongInsertUpdateDeleteSequenceStaysConsistentAfterRestart)
{
    const std::filesystem::path root = prepare_data_root("long_mutation_restart_consistency");
    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);

        expect_sql_ok(api, "CREATE DATABASE test;");
        expect_sql_ok(api, "USE test;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED, age INT);");
        expect_sql_ok(api,
                      "INSERT INTO users (id, name, age) VALUE "
                      "(1, \"u1\", 21), (2, \"u2\", 22), (3, \"u3\", 23), (4, \"u4\", 24), "
                      "(5, \"u5\", 25), (6, \"u6\", 26), (7, \"u7\", 27), (8, \"u8\", 28), "
                      "(9, \"u9\", 29), (10, \"u10\", 30), (11, \"u11\", 31), (12, \"u12\", 32);");
        expect_sql_ok(api, "UPDATE users SET age = 99 WHERE id >= 10;");
        expect_sql_ok(api, "DELETE FROM users WHERE age == 99;");
        expect_sql_ok(api, "UPDATE users SET id = 101, name = \"alice_new\" WHERE id == 1;");
        expect_sql_ok(api, "UPDATE users SET id = 106, name = \"frank_new\" WHERE id == 6;");
        expect_sql_ok(api, "DELETE FROM users WHERE id == 2;");
        expect_sql_ok(api, "DELETE FROM users WHERE name == \"u7\";");
    }

    dbms::Dbms restarted(root);
    dbms::SqlApi api(restarted);
    expect_sql_ok(api, "USE test;");

    const dbms::SqlResponse all_rows = run_sql(api, "SELECT id, name, age FROM users;");
    ASSERT_TRUE(all_rows.ok);
    const nlohmann::json all_result = nlohmann::json::parse(all_rows.json);
    ASSERT_EQ(all_result.size(), 7u);

    std::vector<int> ids;
    ids.reserve(all_result.size());
    for (const auto& row : all_result) {
        ids.push_back(row["id"].get<int>());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<int>{3, 4, 5, 8, 9, 101, 106}));

    const dbms::SqlResponse updated_id = run_sql(api, "SELECT name FROM users WHERE id == 101;");
    ASSERT_TRUE(updated_id.ok);
    const nlohmann::json updated_id_result = nlohmann::json::parse(updated_id.json);
    ASSERT_EQ(updated_id_result.size(), 1u);
    EXPECT_EQ(updated_id_result[0]["name"], "alice_new");

    const dbms::SqlResponse updated_name = run_sql(api, "SELECT id FROM users WHERE name == \"frank_new\";");
    ASSERT_TRUE(updated_name.ok);
    const nlohmann::json updated_name_result = nlohmann::json::parse(updated_name.json);
    ASSERT_EQ(updated_name_result.size(), 1u);
    EXPECT_EQ(updated_name_result[0]["id"], 106);

    const dbms::SqlResponse old_id = run_sql(api, "SELECT id FROM users WHERE id == 1;");
    ASSERT_TRUE(old_id.ok);
    EXPECT_TRUE(nlohmann::json::parse(old_id.json).empty());

    const dbms::SqlResponse old_name = run_sql(api, "SELECT name FROM users WHERE name == \"u6\";");
    ASSERT_TRUE(old_name.ok);
    EXPECT_TRUE(nlohmann::json::parse(old_name.json).empty());

    const dbms::SqlResponse deleted_id = run_sql(api, "SELECT id FROM users WHERE id == 2;");
    ASSERT_TRUE(deleted_id.ok);
    EXPECT_TRUE(nlohmann::json::parse(deleted_id.json).empty());

    const dbms::SqlResponse deleted_name = run_sql(api, "SELECT name FROM users WHERE name == \"u7\";");
    ASSERT_TRUE(deleted_name.ok);
    EXPECT_TRUE(nlohmann::json::parse(deleted_name.json).empty());

    dbms::FixedStringKey<240> key{};
    ASSERT_TRUE(dbms::FixedStringKey<240>::from_string("alice_new", key));
    dbms::IndexManager<dbms::FixedStringKey<240>> name_index((root / "test" / "users__name.idx").string());
    dbms::Rid rid{};
    ASSERT_TRUE(name_index.find(key, rid));

    dbms::TablePageManager manager((root / "test" / "users.tbl").string());
    dbms::Page page;
    ASSERT_TRUE(manager.read_page(rid.page_id, page));
    const std::vector<unsigned char> raw = page.read_record(rid.slot_id);
    ASSERT_FALSE(raw.empty());
    const dbms::Record record = dbms::deserialize_record(raw.data(), 3);
    ASSERT_EQ(record.values().size(), 3u);
    ASSERT_TRUE(record.values()[0].has_value());
    ASSERT_TRUE(record.values()[1].has_value());
    EXPECT_EQ(record.values()[0].value(), "101");
    EXPECT_EQ(record.values()[1].value(), "alice_new");
}

TEST(tzPoint0Pass, SameTableNamesInDifferentDatabasesKeepSeparateIndexesAfterRestart)
{
    const std::filesystem::path root = prepare_data_root("same_table_names_separate_indexes");
    {
        dbms::Dbms engine(root);
        dbms::SqlApi api(engine);

        expect_sql_ok(api, "CREATE DATABASE db1;");
        expect_sql_ok(api, "CREATE DATABASE db2;");

        expect_sql_ok(api, "USE db1;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);");
        expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"db1_alice\"), (2, \"db1_bob\");");

        expect_sql_ok(api, "USE db2;");
        expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);");
        expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"db2_alice\"), (2, \"db2_bob\");");

        expect_sql_ok(api, "UPDATE db1.users SET id = 10, name = \"db1_alice_new\" WHERE id == 1;");
        expect_sql_ok(api, "DELETE FROM db2.users WHERE id == 2;");
    }

    dbms::Dbms restarted(root);
    dbms::SqlApi api(restarted);

    {
        const dbms::SqlResponse db1_row = run_sql(api, "SELECT id, name FROM db1.users WHERE id == 10;");
        ASSERT_TRUE(db1_row.ok);
        const nlohmann::json result = nlohmann::json::parse(db1_row.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["name"], "db1_alice_new");
    }
    {
        const dbms::SqlResponse db1_old = run_sql(api, "SELECT id FROM db1.users WHERE id == 1;");
        ASSERT_TRUE(db1_old.ok);
        EXPECT_TRUE(nlohmann::json::parse(db1_old.json).empty());
    }
    {
        const dbms::SqlResponse db2_row = run_sql(api, "SELECT id, name FROM db2.users WHERE id == 1;");
        ASSERT_TRUE(db2_row.ok);
        const nlohmann::json result = nlohmann::json::parse(db2_row.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["name"], "db2_alice");
    }
    {
        const dbms::SqlResponse db2_deleted = run_sql(api, "SELECT id FROM db2.users WHERE id == 2;");
        ASSERT_TRUE(db2_deleted.ok);
        EXPECT_TRUE(nlohmann::json::parse(db2_deleted.json).empty());
    }

    dbms::IndexManager<int> db1_id_index((root / "db1" / "users__id.idx").string());
    dbms::IndexManager<int> db2_id_index((root / "db2" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_TRUE(db1_id_index.find(10, rid));
    EXPECT_FALSE(db1_id_index.find(1, rid));
    EXPECT_TRUE(db2_id_index.find(1, rid));
    EXPECT_FALSE(db2_id_index.find(10, rid));
}

TEST(tzPoint0Pass, MissingIndexFileReturnsInformativeRuntimeError)
{
    const std::filesystem::path root = prepare_data_root("missing_index_file_runtime_error");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    std::error_code ec;
    std::filesystem::remove(root / "test" / "users__id.idx", ec);

    const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE id == 1;");
    ASSERT_FALSE(select.ok);
    EXPECT_NE(select.error.find("cannot read index"), std::string::npos);
}

TEST(tzPoint0Pass, TruncatedTablePageReturnsInformativeRuntimeError)
{
    const std::filesystem::path root = prepare_data_root("truncated_table_page_runtime_error");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    std::error_code ec;
    std::filesystem::resize_file(root / "test" / "users.tbl", dbms::kPageSize, ec);
    ASSERT_FALSE(ec);

    const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE name == \"alice\";");
    ASSERT_FALSE(select.ok);
    EXPECT_NE(select.error.find("cannot read table page"), std::string::npos);
}

TEST(tzPoint0Pass, CorruptedIndexRidReturnsInformativeRuntimeError)
{
    const std::filesystem::path root = prepare_data_root("corrupted_index_rid_runtime_error");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    dbms::IndexManager<int> id_index((root / "test" / "users__id.idx").string());
    ASSERT_TRUE(id_index.erase(1));
    ASSERT_TRUE(id_index.insert(1, dbms::Rid{999, 999}));

    const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE id == 1;");
    ASSERT_FALSE(select.ok);
    EXPECT_NE(select.error.find("points to unreadable table row"), std::string::npos);
}

TEST(tzPoint0Pass, DeleteWithIndexedConditionFailsCleanlyWhenSlotWasAlreadyDeleted)
{
    const std::filesystem::path root = prepare_data_root("delete_already_deleted_slot_runtime_error");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    dbms::TablePageManager manager((root / "test" / "users.tbl").string());
    dbms::Page page;
    ASSERT_TRUE(manager.read_page(1, page));
    ASSERT_TRUE(page.remove_record(0));
    ASSERT_TRUE(manager.write_page(1, page));

    const dbms::SqlResponse del = run_sql(api, "DELETE FROM users WHERE id == 1;");
    ASSERT_FALSE(del.ok);
    EXPECT_NE(del.error.find("points to unreadable table row"), std::string::npos);

    dbms::IndexManager<int> id_index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    EXPECT_TRUE(id_index.find(1, rid));
}

TEST(tzPoint0SpecGaps, IndexedColumnShouldCreateIndexAtTableCreationTime)
{
    const std::filesystem::path root = prepare_data_root("indexed_create_time");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");

    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users__id.idx"));
}

TEST(tzPoint0Pass, CreateTableRollsBackWhenOneIndexCannotBeCreated)
{
    const std::filesystem::path root = prepare_data_root("create_table_index_failure_rollback");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");

    const std::filesystem::path failing_idx_path = root / "test" / "users__name.idx";
    std::error_code ec;
    std::filesystem::create_directories(failing_idx_path, ec);
    ASSERT_FALSE(ec);

    const dbms::SqlResponse create = run_sql(
        api,
        "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);");
    ASSERT_FALSE(create.ok);
    EXPECT_NE(create.error.find("cannot create index"), std::string::npos);

    EXPECT_FALSE(std::filesystem::exists(root / "test" / "users.tbl"));
    EXPECT_FALSE(std::filesystem::exists(root / "test" / "users__id.idx"));
    EXPECT_TRUE(std::filesystem::is_directory(failing_idx_path));

    const dbms::SqlResponse select = run_sql(api, "SELECT * FROM users;");
    ASSERT_FALSE(select.ok);
    EXPECT_NE(select.error.find("does not exist"), std::string::npos);
}

TEST(tzPoint0Pass, DropTableFailsBeforeRemovingTableWhenIndexPathIsCorrupted)
{
    const std::filesystem::path root = prepare_data_root("drop_table_corrupt_index_preflight");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    const std::filesystem::path idx_path = root / "test" / "users__id.idx";
    std::error_code ec;
    std::filesystem::remove(idx_path, ec);
    std::filesystem::create_directories(idx_path, ec);
    ASSERT_FALSE(ec);
    std::ofstream marker(idx_path / "not_an_index");
    marker << "x";
    marker.close();

    const dbms::SqlResponse drop = run_sql(api, "DROP TABLE users;");
    ASSERT_FALSE(drop.ok);
    EXPECT_NE(drop.error.find("is not a regular file"), std::string::npos);

    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users.tbl"));
    const dbms::SqlResponse select = run_sql(api, "SELECT name FROM users WHERE name == \"alice\";");
    ASSERT_TRUE(select.ok) << select.error;
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "alice");
}

TEST(tzPoint0Pass, ColumnTypeValidationWorks)
{
    dbms::Dbms engine(prepare_data_root("type_validation"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT, name STRING);");

    const dbms::SqlResponse bad_insert = run_sql(api, "INSERT INTO users (id, name) VALUE (\"abc\", \"alice\");");
    EXPECT_FALSE(bad_insert.ok);
    EXPECT_FALSE(bad_insert.error.empty());
}

TEST(tzPoint0Pass, UsesIndexForIndexedCondition)
{
    const std::filesystem::path root = prepare_data_root("index_usage");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    force_table_header_next_page(root / "test" / "users.tbl", 1);

    {
        const dbms::SqlResponse indexed = run_sql(api, "SELECT id FROM users WHERE id == 1;");
        ASSERT_TRUE(indexed.ok) << indexed.error;
        const nlohmann::json result = nlohmann::json::parse(indexed.json);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["id"], 1);
    }
    {
        const dbms::SqlResponse non_indexed = run_sql(api, "SELECT name FROM users WHERE name == \"alice\";");
        ASSERT_TRUE(non_indexed.ok) << non_indexed.error;
        const nlohmann::json result = nlohmann::json::parse(non_indexed.json);
        EXPECT_EQ(result.size(), 0u);
    }
}

TEST(tzPoint0Pass, IndexStoresRidReferencingTableRecord)
{
    const std::filesystem::path root = prepare_data_root("index_rid_reference");
    dbms::Dbms engine(root);
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (7, \"alice\");");

    dbms::IndexManager<int> index((root / "test" / "users__id.idx").string());
    dbms::Rid rid{};
    ASSERT_TRUE(index.find(7, rid));

    dbms::TablePageManager manager((root / "test" / "users.tbl").string());
    dbms::Page page;
    ASSERT_TRUE(manager.read_page(rid.page_id, page));
    const std::vector<unsigned char> raw = page.read_record(rid.slot_id);
    ASSERT_FALSE(raw.empty());

    const dbms::Record record = dbms::deserialize_record(raw.data(), 2);
    const auto& values = record.values();
    ASSERT_EQ(values.size(), 2u);
    ASSERT_TRUE(values[0].has_value());
    ASSERT_TRUE(values[1].has_value());
    EXPECT_EQ(values[0].value(), "7");
    EXPECT_EQ(values[1].value(), "alice");
}

TEST(tzPoint0Pass, SyntaxAndSemanticErrorsAreInformative)
{
    dbms::Dbms engine(prepare_data_root("informative_errors"));
    dbms::SqlApi api(engine);

    const dbms::SqlResponse syntax_error = run_sql(api, "SELECT FROM;");
    EXPECT_FALSE(syntax_error.ok);
    EXPECT_FALSE(syntax_error.error.empty());
    EXPECT_NE(syntax_error.error.find("Parser error"), std::string::npos);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING, age INT NOT NULL);");
    expect_sql_ok(api, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);");

    const dbms::SqlResponse semantic_error = run_sql(api, "SELECT * FROM missing;");
    EXPECT_FALSE(semantic_error.ok);
    EXPECT_FALSE(semantic_error.error.empty());
    EXPECT_NE(semantic_error.error.find("Semantic error"), std::string::npos);
    EXPECT_NE(semantic_error.error.find("missing"), std::string::npos);

    const dbms::SqlResponse type_error = run_sql(api, "INSERT INTO users (id, name, age) VALUE (\"bad\", \"bob\", 25);");
    EXPECT_FALSE(type_error.ok);
    EXPECT_NE(type_error.error.find("Type error"), std::string::npos);
    EXPECT_NE(type_error.error.find("id"), std::string::npos);

    const dbms::SqlResponse duplicate_error = run_sql(api, "INSERT INTO users (id, name, age) VALUE (1, \"dup\", 30);");
    EXPECT_FALSE(duplicate_error.ok);
    EXPECT_NE(duplicate_error.error.find("Constraint error"), std::string::npos);
    EXPECT_NE(duplicate_error.error.find("duplicate INDEXED key"), std::string::npos);

    const dbms::SqlResponse where_error = run_sql(api, "SELECT id FROM users WHERE missing == 1;");
    EXPECT_FALSE(where_error.ok);
    EXPECT_NE(where_error.error.find("Semantic error"), std::string::npos);
    EXPECT_NE(where_error.error.find("missing"), std::string::npos);

    const dbms::SqlResponse regex_error = run_sql(api, "SELECT name FROM users WHERE name LIKE \"[\";");
    EXPECT_FALSE(regex_error.ok);
    EXPECT_NE(regex_error.error.find("Runtime error"), std::string::npos);
    EXPECT_NE(regex_error.error.find("invalid LIKE regex"), std::string::npos);

    const dbms::SqlResponse not_null_error = run_sql(api, "UPDATE users SET age = NULL WHERE id == 1;");
    EXPECT_FALSE(not_null_error.ok);
    EXPECT_NE(not_null_error.error.find("Constraint error"), std::string::npos);
    EXPECT_NE(not_null_error.error.find("age"), std::string::npos);

    const std::vector<dbms::SqlResponse> checked_errors = {
        syntax_error,
        semantic_error,
        type_error,
        duplicate_error,
        where_error,
        regex_error,
        not_null_error
    };
    for (const dbms::SqlResponse& response : checked_errors) {
        EXPECT_EQ(response.error.find("Execution failed"), std::string::npos);
    }
}

TEST(tzPoint0Pass, SelectResultIsJsonArray)
{
    dbms::Dbms engine(prepare_data_root("json_array_result"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name FROM users WHERE id == 1;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json parsed = nlohmann::json::parse(select.json);
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    ASSERT_TRUE(parsed[0].is_object());
}

TEST(tzPoint0Pass, InvalidQueriesDoNotCrashProcess)
{
    dbms::Dbms engine(prepare_data_root("no_crash_invalid_queries"));
    dbms::SqlApi api(engine);

    const std::vector<std::string> queries = {
        "BAD SQL;",
        "SELECT",
        "INSERT INTO",
        "CREATE TABLE t (id BADTYPE);",
        "DELETE FROM missing WHERE id == 1;"
    };

    for (const std::string& query : queries) {
        dbms::SqlResponse response{};
        EXPECT_NO_THROW(response = run_sql(api, query));
        EXPECT_FALSE(response.ok);
        EXPECT_FALSE(response.error.empty());
    }
}

TEST(tzPoint0Pass, KeywordsAreCaseInsensitive)
{
    dbms::Dbms engine(prepare_data_root("keywords_case_insensitive"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "create database test;");
    expect_sql_ok(api, "use test;");
    expect_sql_ok(api, "create table users (id int indexed, name string);");
    expect_sql_ok(api, "insert into users (id, name) value (1, \"alice\");");

    const dbms::SqlResponse select = run_sql(api, "select id, name from users where id == 1;");
    EXPECT_TRUE(select.ok) << select.error;
}

TEST(tzPoint0SpecGaps, MixedCaseKeywordInOneWordShouldBeRejected)
{
    dbms::Dbms engine(prepare_data_root("mixed_case_keyword"));
    dbms::SqlApi api(engine);

    const dbms::SqlResponse response = run_sql(api, "CrEaTe DATABASE test;");
    EXPECT_FALSE(response.ok);
}

TEST(tzPoint0SpecGaps, SingleQuotedStringsShouldBeRejected)
{
    dbms::Dbms engine(prepare_data_root("single_quotes"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");

    const dbms::SqlResponse response = run_sql(api, "INSERT INTO users (id, name) VALUE (1, 'alice');");
    EXPECT_FALSE(response.ok);
}

TEST(tzPoint0Pass, IdentifierRulesWorkForAllowedAndForbiddenNames)
{
    dbms::Dbms engine(prepare_data_root("identifier_rules"));
    dbms::SqlApi api(engine);

    {
        const dbms::SqlResponse bad = run_sql(api, "CREATE DATABASE 1bad;");
        EXPECT_FALSE(bad.ok);
    }
    {
        const dbms::SqlResponse bad = run_sql(api, "CREATE DATABASE \"bad name\";");
        EXPECT_FALSE(bad.ok);
    }

    expect_sql_ok(api, "CREATE DATABASE db_1;");
    expect_sql_ok(api, "USE db_1;");
    expect_sql_ok(api, "CREATE TABLE users_2 (id INT INDEXED, name STRING);");
}

TEST(tzPoint0SpecGaps, QualifiedDatabaseDotTableAccessShouldWork)
{
    dbms::Dbms engine(prepare_data_root("qualified_table_access"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\");");

    const dbms::SqlResponse response = run_sql(api, "SELECT id, name FROM test.users WHERE id == 1;");
    ASSERT_TRUE(response.ok) << response.error;
    const nlohmann::json result = nlohmann::json::parse(response.json);
    ASSERT_EQ(result.size(), 1u);
}

TEST(tzPoint0Pass, InsertOmittedNullableColumnsBecomeNull)
{
    dbms::Dbms engine(prepare_data_root("insert_default_null"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING, age INT);");
    expect_sql_ok(api, "INSERT INTO users (id) VALUE (1);");

    const dbms::SqlResponse select = run_sql(api, "SELECT id, name, age FROM users WHERE id == 1;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0]["name"].is_null());
    EXPECT_TRUE(result[0]["age"].is_null());
}

TEST(tzPoint0Pass, UpdateDeleteAndSelectAliasWork)
{
    dbms::Dbms engine(prepare_data_root("update_delete_alias"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING, age INT);");
    expect_sql_ok(api, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20), (2, \"bob\", 25);");
    expect_sql_ok(api, "UPDATE users SET age = 26 WHERE id == 2;");
    expect_sql_ok(api, "DELETE FROM users WHERE id == 1;");

    const dbms::SqlResponse select = run_sql(api, "SELECT id AS user_id, age FROM users WHERE id == 2;");
    ASSERT_TRUE(select.ok);
    const nlohmann::json result = nlohmann::json::parse(select.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(result[0].contains("user_id"));
    EXPECT_EQ(result[0]["user_id"], 2);
    EXPECT_EQ(result[0]["age"], 26);
}

TEST(tzPoint0Pass, ComparisonOperatorsWork)
{
    dbms::Dbms engine(prepare_data_root("comparison_ops"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"a\"), (2, \"b\"), (3, \"c\");");

    const std::vector<std::pair<std::string, std::size_t>> checks = {
        {"SELECT id FROM users WHERE id == 2;", 1u},
        {"SELECT id FROM users WHERE id != 2;", 2u},
        {"SELECT id FROM users WHERE id < 2;", 1u},
        {"SELECT id FROM users WHERE id > 2;", 1u},
        {"SELECT id FROM users WHERE id <= 2;", 2u},
        {"SELECT id FROM users WHERE id >= 2;", 2u},
    };

    for (const auto& check : checks) {
        const dbms::SqlResponse response = run_sql(api, check.first);
        ASSERT_TRUE(response.ok) << check.first << " error: " << response.error;
        const nlohmann::json result = nlohmann::json::parse(response.json);
        EXPECT_EQ(result.size(), check.second) << check.first;
    }
}

TEST(tzPoint0Pass, StringComparisonIsLexicographic)
{
    dbms::Dbms engine(prepare_data_root("string_lexicographic"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\"), (3, \"carol\");");

    const dbms::SqlResponse response = run_sql(api, "SELECT name FROM users WHERE name > \"bob\";");
    ASSERT_TRUE(response.ok);
    const nlohmann::json result = nlohmann::json::parse(response.json);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "carol");
}

TEST(tzPoint0Pass, ConditionSupportsLiteralOnEitherSideAndColumnVsColumn)
{
    dbms::Dbms engine(prepare_data_root("condition_operands"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"a\"), (2, \"b\"), (3, \"c\");");

    {
        const dbms::SqlResponse response = run_sql(api, "SELECT id FROM users WHERE 2 <= id;");
        ASSERT_TRUE(response.ok);
        const nlohmann::json result = nlohmann::json::parse(response.json);
        EXPECT_EQ(result.size(), 2u);
    }
    {
        const dbms::SqlResponse response = run_sql(api, "SELECT id FROM users WHERE id == id;");
        ASSERT_TRUE(response.ok);
        const nlohmann::json result = nlohmann::json::parse(response.json);
        EXPECT_EQ(result.size(), 3u);
    }
}

TEST(tzPoint0SpecGaps, BetweenConditionShouldWorkAsClosedInterval)
{
    dbms::Dbms engine(prepare_data_root("between_condition"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"a\"), (2, \"b\"), (3, \"c\"), (4, \"d\");");

    const dbms::SqlResponse response = run_sql(api, "SELECT id FROM users WHERE id BETWEEN 2 AND 4;");
    ASSERT_TRUE(response.ok) << response.error;
    const nlohmann::json result = nlohmann::json::parse(response.json);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0]["id"], 2);
    EXPECT_EQ(result[1]["id"], 3);
    EXPECT_EQ(result[2]["id"], 4);
}

TEST(tzPoint0SpecGaps, LikeConditionShouldSupportRegexPattern)
{
    dbms::Dbms engine(prepare_data_root("like_condition"));
    dbms::SqlApi api(engine);

    expect_sql_ok(api, "CREATE DATABASE test;");
    expect_sql_ok(api, "USE test;");
    expect_sql_ok(api, "CREATE TABLE users (id INT INDEXED, name STRING);");
    expect_sql_ok(api, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\"), (3, \"anna\");");

    const dbms::SqlResponse response = run_sql(api, "SELECT name FROM users WHERE name LIKE \"a.*\";");
    ASSERT_TRUE(response.ok) << response.error;
    const nlohmann::json result = nlohmann::json::parse(response.json);
    ASSERT_EQ(result.size(), 2u);
}

TEST(tzPoint0SpecGaps, CommandsShouldRequireSemicolonTerminator)
{
    dbms::Dbms engine(prepare_data_root("semicolon_required"));
    dbms::SqlApi api(engine);

    const dbms::SqlResponse response = run_sql(api, "CREATE DATABASE test");
    EXPECT_FALSE(response.ok);
}
