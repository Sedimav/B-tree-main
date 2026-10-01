#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "dbms/sql/lexer.h"
#include "dbms/sql/parser.h"
#include "dbms/sql/executor.h"

using dbms::Dbms;
using dbms::Executor;
using dbms::Lexer;
using dbms::Parser;

static std::filesystem::path prepare_data_root(const std::string& test_name)
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("coursework_dbms_tests_" + test_name);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    return root;
}

static bool run_sql(Executor& exec, const std::string& sql)
{
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    return exec.execute(parser.parse_statement());
}

TEST(executorTests, createDropUse)
{
    Dbms dbms(prepare_data_root("create_drop_use"));
    Executor exec(dbms);

    {
        Lexer lex("CREATE DATABASE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("USE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
        EXPECT_EQ(exec.current_db(), "test");
    }
    {
        Lexer lex("DROP DATABASE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
        EXPECT_TRUE(exec.current_db().empty());
    }
}

TEST(executorTests, duplicateDatabase)
{
    Dbms dbms(prepare_data_root("duplicate_database"));
    Executor exec(dbms);

    Lexer lex("CREATE DATABASE test;\n");
    Parser parser(lex.tokenize());
    EXPECT_TRUE(exec.execute(parser.parse_statement()));

    Lexer lex2("CREATE DATABASE test;\n");
    Parser parser2(lex2.tokenize());
    EXPECT_FALSE(exec.execute(parser2.parse_statement()));
}

TEST(executorTests, createDropTable)
{
    Dbms dbms(prepare_data_root("create_drop_table"));
    Executor exec(dbms);

    Lexer lex0("CREATE DATABASE test;\n");
    Parser parser0(lex0.tokenize());
    EXPECT_TRUE(exec.execute(parser0.parse_statement()));

    Lexer lex1("USE test;\n");
    Parser parser1(lex1.tokenize());
    EXPECT_TRUE(exec.execute(parser1.parse_statement()));

    Lexer lex2("CREATE TABLE users (id INT NOT NULL, name STRING);\n");
    Parser parser2(lex2.tokenize());
    EXPECT_TRUE(exec.execute(parser2.parse_statement()));

    Lexer lex3("DROP TABLE users;\n");
    Parser parser3(lex3.tokenize());
    EXPECT_TRUE(exec.execute(parser3.parse_statement()));
}

TEST(executorTests, createTableWithoutDb)
{
    Dbms dbms(prepare_data_root("create_table_without_db"));
    Executor exec(dbms);

    Lexer lex("CREATE TABLE users (id INT);\n");
    Parser parser(lex.tokenize());
    EXPECT_FALSE(exec.execute(parser.parse_statement()));
}

TEST(executorTests, invalidColumnType)
{
    Dbms dbms(prepare_data_root("invalid_column_type"));
    Executor exec(dbms);

    Lexer lex0("CREATE DATABASE test;\n");
    Parser parser0(lex0.tokenize());
    EXPECT_TRUE(exec.execute(parser0.parse_statement()));

    Lexer lex1("USE test;\n");
    Parser parser1(lex1.tokenize());
    EXPECT_TRUE(exec.execute(parser1.parse_statement()));

    Lexer lex2("CREATE TABLE users (id BADTYPE);\n");
    Parser parser2(lex2.tokenize());
    EXPECT_FALSE(exec.execute(parser2.parse_statement()));
}

TEST(executorTests, insertWithIndexedAndNotNull)
{
    const std::filesystem::path root = prepare_data_root("insert_indexed_not_null");
    Dbms dbms(root);
    Executor exec(dbms);

    {
        Lexer lex("CREATE DATABASE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("USE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("CREATE TABLE users (id INT INDEXED, name STRING, age INT NOT NULL);\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO users (id, name, age) VALUE (1, \"bob\", 25);\n");
        Parser parser(lex.tokenize());
        EXPECT_FALSE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO users (id, name, age) VALUE (2, \"charlie\", NULL);\n");
        Parser parser(lex.tokenize());
        EXPECT_FALSE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO users (name, age) VALUE (\"delta\", 28);\n");
        Parser parser(lex.tokenize());
        EXPECT_FALSE(exec.execute(parser.parse_statement()));
    }

    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users.tbl"));
    EXPECT_TRUE(std::filesystem::exists(root / "test" / "users__id.idx"));
}

TEST(executorTests, insertWithStringIndexed)
{
    const std::filesystem::path root = prepare_data_root("insert_string_indexed");
    Dbms dbms(root);
    Executor exec(dbms);

    {
        Lexer lex("CREATE DATABASE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("USE test;\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("CREATE TABLE tags (name STRING INDEXED);\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO tags (name) VALUE (\"alpha\");\n");
        Parser parser(lex.tokenize());
        EXPECT_TRUE(exec.execute(parser.parse_statement()));
    }
    {
        Lexer lex("INSERT INTO tags (name) VALUE (\"alpha\");\n");
        Parser parser(lex.tokenize());
        EXPECT_FALSE(exec.execute(parser.parse_statement()));
    }

    EXPECT_TRUE(std::filesystem::exists(root / "test" / "tags__name.idx"));
}

TEST(executorTests, selectStarWithWhereBuildsJson)
{
    const std::filesystem::path root = prepare_data_root("select_star_where_json");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING, age INT NOT NULL);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (2, \"bob\", 25);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (3, NULL, 30);\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT * FROM users WHERE age >= 25;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);

    EXPECT_EQ(result[0]["id"], 2);
    EXPECT_EQ(result[0]["name"], "bob");
    EXPECT_EQ(result[0]["age"], 25);

    EXPECT_EQ(result[1]["id"], 3);
    EXPECT_TRUE(result[1]["name"].is_null());
    EXPECT_EQ(result[1]["age"], 30);
}

TEST(executorTests, selectProjectionAliasBuildsJson)
{
    const std::filesystem::path root = prepare_data_root("select_projection_alias_json");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING, age INT);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (2, \"bob\", 25);\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id AS user_id, name FROM users WHERE id == 1;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result[0].contains("user_id"));
    ASSERT_TRUE(result[0].contains("name"));
    ASSERT_FALSE(result[0].contains("id"));

    if (result[0]["user_id"].is_number_integer()) {
        EXPECT_EQ(result[0]["user_id"], 1);
    } else {
        EXPECT_EQ(result[0]["user_id"], "1");
    }
    EXPECT_EQ(result[0]["name"], "alice");
}

TEST(executorTests, selectIndexedIntEqualityAndRange)
{
    const std::filesystem::path root = prepare_data_root("select_indexed_int_equality_range");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (2, \"bob\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (3, \"carol\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id == 2;\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["id"], 2);
        EXPECT_EQ(result[0]["name"], "bob");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id >= 2;\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 2u);
        std::vector<int> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row["id"].get<int>());
        }
        std::sort(ids.begin(), ids.end());
        EXPECT_EQ(ids[0], 2);
        EXPECT_EQ(ids[1], 3);
    }
}

TEST(executorTests, selectIndexedIntWithLiteralOnLeft)
{
    const std::filesystem::path root = prepare_data_root("select_indexed_literal_left");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (2, \"bob\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (3, \"carol\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE 2 <= id;\n"));
    EXPECT_TRUE(exec.last_operation_used_index());

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);
    std::vector<int> ids;
    ids.reserve(result.size());
    for (const auto& row : result) {
        ids.push_back(row["id"].get<int>());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], 2);
    EXPECT_EQ(ids[1], 3);
}

TEST(executorTests, indexUsageDiagnosticCoversStringAndFallbacks)
{
    const std::filesystem::path root = prepare_data_root("index_usage_diagnostic_string_fallbacks");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    EXPECT_FALSE(exec.last_operation_used_index());
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED, age INT);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20), (2, \"bob\", 30), (3, \"carol\", 40);\n"));

    const std::vector<std::string> int_indexed_conditions = {
        "id == 2",
        "id < 3",
        "id <= 2",
        "id > 1",
        "id >= 2"
    };
    for (const std::string& condition : int_indexed_conditions) {
        ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE " + condition + ";\n")) << condition;
        EXPECT_TRUE(exec.last_operation_used_index()) << condition;
    }

    const std::vector<std::string> string_indexed_conditions = {
        "name == \"bob\"",
        "name < \"carol\"",
        "name <= \"bob\"",
        "name > \"alice\"",
        "name >= \"bob\""
    };
    for (const std::string& condition : string_indexed_conditions) {
        ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE " + condition + ";\n")) << condition;
        EXPECT_TRUE(exec.last_operation_used_index()) << condition;
    }

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE name == \"bob\";\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["name"], "bob");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE name >= \"bob\";\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 2u);
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id != 2;\n"));
    EXPECT_FALSE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 2u);
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE age >= 30;\n"));
    EXPECT_FALSE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 2u);
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id == age;\n"));
    EXPECT_FALSE(exec.last_operation_used_index());
}

TEST(executorTests, betweenConditionCoversStringRangeAndTypeErrors)
{
    const std::filesystem::path root = prepare_data_root("between_string_range_type_errors");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING INDEXED);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\"), (3, \"carol\"), (4, \"dave\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE name BETWEEN \"bob\" AND \"dave\";\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 3u);
        EXPECT_EQ(result[0]["name"], "bob");
        EXPECT_EQ(result[1]["name"], "carol");
        EXPECT_EQ(result[2]["name"], "dave");
    }

    EXPECT_FALSE(run_sql(exec, "SELECT id FROM users WHERE id BETWEEN \"bad\" AND 4;\n"));
    EXPECT_NE(exec.last_error().find("Type error"), std::string::npos);

    EXPECT_FALSE(run_sql(exec, "SELECT id FROM users WHERE missing BETWEEN 1 AND 3;\n"));
    EXPECT_NE(exec.last_error().find("Semantic error"), std::string::npos);
}

TEST(executorTests, betweenUsesIndexForIndexedIntAndFallsBackOtherwise)
{
    const std::filesystem::path root = prepare_data_root("between_uses_index_and_fallback");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, age INT, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, age, name) VALUE (1, 10, \"a\"), (2, 20, \"b\"), (3, 30, \"c\"), (4, 40, \"d\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id BETWEEN 2 AND 4;\n"));
    EXPECT_TRUE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 3u);
        EXPECT_EQ(result[0]["id"], 2);
        EXPECT_EQ(result[1]["id"], 3);
        EXPECT_EQ(result[2]["id"], 4);
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE age BETWEEN 20 AND 40;\n"));
    EXPECT_FALSE(exec.last_operation_used_index());
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 3u);
    }
}

TEST(executorTests, likeConditionCoversEmptyResultAndTypeErrors)
{
    const std::filesystem::path root = prepare_data_root("like_empty_result_type_errors");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING, pattern STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, pattern) VALUE (1, \"alice\", \"a.*\"), (2, \"bob\", \"b.*\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE name LIKE \"z.*\";\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        EXPECT_TRUE(result.empty());
    }

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE name LIKE pattern;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 2u);
    }

    EXPECT_FALSE(run_sql(exec, "SELECT name FROM users WHERE name LIKE \"[\";\n"));
    EXPECT_NE(exec.last_error().find("invalid LIKE regex"), std::string::npos);

    EXPECT_FALSE(run_sql(exec, "SELECT id FROM users WHERE id LIKE \".*\";\n"));
    EXPECT_NE(exec.last_error().find("LIKE value"), std::string::npos);

    EXPECT_FALSE(run_sql(exec, "SELECT name FROM users WHERE name LIKE id;\n"));
    EXPECT_NE(exec.last_error().find("LIKE pattern"), std::string::npos);
}

TEST(executorTests, indexedStringRejectsValuesLongerThanKeyLimit)
{
    const std::filesystem::path root = prepare_data_root("indexed_string_too_long");
    Dbms dbms(root);
    Executor exec(dbms);
    const std::string long_value(241, 'x');

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE tags (id INT INDEXED, name STRING INDEXED);\n"));

    EXPECT_FALSE(run_sql(exec, "INSERT INTO tags (id, name) VALUE (1, \"" + long_value + "\");\n"));
    EXPECT_NE(exec.last_error().find("too long for the index key"), std::string::npos);

    ASSERT_TRUE(run_sql(exec, "INSERT INTO tags (id, name) VALUE (1, \"short\");\n"));
    EXPECT_FALSE(run_sql(exec, "UPDATE tags SET name = \"" + long_value + "\" WHERE id == 1;\n"));
    EXPECT_NE(exec.last_error().find("too long for the index key"), std::string::npos);

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM tags WHERE id == 1;\n"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "short");
}

TEST(executorTests, selectFailsForUnknownProjectionColumn)
{
    const std::filesystem::path root = prepare_data_root("select_unknown_projection_column");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));

    EXPECT_FALSE(run_sql(exec, "SELECT missing_column FROM users;\n"));
}

TEST(executorTests, updateWithWhereChangesRow)
{
    const std::filesystem::path root = prepare_data_root("update_where_changes_row");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING, age INT NOT NULL);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (2, \"bob\", 25);\n"));

    ASSERT_TRUE(run_sql(exec, "UPDATE users SET age = 26 WHERE id == 2;\n"));
    ASSERT_TRUE(run_sql(exec, "SELECT id, age FROM users WHERE id == 2;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 2);
    EXPECT_EQ(result[0]["age"], 26);
}

TEST(executorTests, updateIndexedKeyUpdatesIndexLookup)
{
    const std::filesystem::path root = prepare_data_root("update_indexed_key_updates_index_lookup");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (2, \"bob\");\n"));

    ASSERT_TRUE(run_sql(exec, "UPDATE users SET id = 3 WHERE id == 2;\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id == 3;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["id"], 3);
        EXPECT_EQ(result[0]["name"], "bob");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id == 2;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 0u);
    }
}

TEST(executorTests, updateIndexedKeyDuplicateFails)
{
    const std::filesystem::path root = prepare_data_root("update_indexed_key_duplicate_fails");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (2, \"bob\");\n"));

    EXPECT_FALSE(run_sql(exec, "UPDATE users SET id = 1 WHERE id == 2;\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id == 2;\n"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 2);
    EXPECT_EQ(result[0]["name"], "bob");
}

TEST(executorTests, updateNotNullToNullFails)
{
    const std::filesystem::path root = prepare_data_root("update_not_null_to_null_fails");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, age INT NOT NULL);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, age) VALUE (1, 20);\n"));

    EXPECT_FALSE(run_sql(exec, "UPDATE users SET age = NULL WHERE id == 1;\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, age FROM users WHERE id == 1;\n"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 1);
    EXPECT_EQ(result[0]["age"], 20);
}

TEST(executorTests, updateWithoutWhereUpdatesAllRows)
{
    const std::filesystem::path root = prepare_data_root("update_without_where_updates_all");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, age INT);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, age) VALUE (1, 10);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, age) VALUE (2, 20);\n"));

    ASSERT_TRUE(run_sql(exec, "UPDATE users SET age = 30;\n"));
    ASSERT_TRUE(run_sql(exec, "SELECT id, age FROM users WHERE id >= 1;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);

    std::vector<int> ages;
    ages.reserve(result.size());
    for (const auto& row : result) {
        ages.push_back(row["age"].get<int>());
    }
    std::sort(ages.begin(), ages.end());
    EXPECT_EQ(ages[0], 30);
    EXPECT_EQ(ages[1], 30);
}

TEST(executorTests, updateIndexedKeyStressSequenceKeepsIndexConsistent)
{
    const std::filesystem::path root = prepare_data_root("update_indexed_stress_sequence");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));

    for (int i = 1; i <= 30; ++i) {
        const std::string sql = "INSERT INTO users (id, name) VALUE (" + std::to_string(i) + ", \"u" + std::to_string(i) + "\");\n";
        ASSERT_TRUE(run_sql(exec, sql));
    }

    for (int i = 10; i <= 20; ++i) {
        const std::string sql = "UPDATE users SET id = " + std::to_string(i + 100) + " WHERE id == " + std::to_string(i) + ";\n";
        ASSERT_TRUE(run_sql(exec, sql));
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id >= 110;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 11u);
        std::vector<int> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row["id"].get<int>());
        }
        std::sort(ids.begin(), ids.end());
        for (int i = 0; i < 11; ++i) {
            EXPECT_EQ(ids[static_cast<std::size_t>(i)], 110 + i);
        }
    }

    for (int i = 10; i <= 20; ++i) {
        const std::string sql = "SELECT id FROM users WHERE id == " + std::to_string(i) + ";\n";
        ASSERT_TRUE(run_sql(exec, sql));
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 0u);
    }
}

TEST(executorTests, deleteWithWhereRemovesRow)
{
    const std::filesystem::path root = prepare_data_root("delete_with_where_removes_row");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\"), (3, \"carol\");\n"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users WHERE id == 2;\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id >= 1;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 2u);
        std::vector<int> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row["id"].get<int>());
        }
        std::sort(ids.begin(), ids.end());
        EXPECT_EQ(ids[0], 1);
        EXPECT_EQ(ids[1], 3);
    }

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id == 2;\n"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_TRUE(result.is_array());
        ASSERT_EQ(result.size(), 0u);
    }
}

TEST(executorTests, deleteIndexedKeyRemovesFromIndex)
{
    const std::filesystem::path root = prepare_data_root("delete_indexed_key_removes_from_index");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");\n"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users WHERE id == 1;\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"again\");\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT name FROM users WHERE id == 1;\n"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "again");
}

TEST(executorTests, deleteUnknownTableFails)
{
    const std::filesystem::path root = prepare_data_root("delete_unknown_table_fails");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));

    EXPECT_FALSE(run_sql(exec, "DELETE FROM missing WHERE id == 1;\n"));
}

TEST(executorTests, deleteWithoutWhereRemovesAllRows)
{
    const std::filesystem::path root = prepare_data_root("delete_without_where_removes_all_rows");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");\n"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users;\n"));
    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id >= 1;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 0u);
}

TEST(executorTests, deleteWhereNoMatchesDoesNothing)
{
    const std::filesystem::path root = prepare_data_root("delete_where_no_matches_does_nothing");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");\n"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users WHERE id == 999;\n"));

    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id >= 1;\n"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);
    std::vector<int> ids;
    ids.reserve(result.size());
    for (const auto& row : result) {
        ids.push_back(row["id"].get<int>());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2);
}

TEST(executorTests, deleteWithoutWhereClearsAllIndexedKeys)
{
    const std::filesystem::path root = prepare_data_root("delete_without_where_clears_all_indexed_keys");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;\n"));
    ASSERT_TRUE(run_sql(exec, "USE test;\n"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT INDEXED, name STRING);\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");\n"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users;\n"));

    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice2\");\n"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (2, \"bob2\");\n"));
    ASSERT_TRUE(run_sql(exec, "SELECT id, name FROM users WHERE id >= 1;\n"));

    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);
}

// ============ AND / OR / скобки ============

TEST(executorTests, selectWhereAndFiltersRows)
{
    const std::filesystem::path root = prepare_data_root("select_where_and_filters");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING, age INT);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20), (2, \"bob\", 25), (3, \"alice\", 30);"));

    // name == "alice" AND age >= 25  ->  только id=3
    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE name == \"alice\" AND age >= 25;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 3);
}

TEST(executorTests, selectWhereOrFiltersRows)
{
    const std::filesystem::path root = prepare_data_root("select_where_or_filters");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING, age INT);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20), (2, \"bob\", 25), (3, \"carol\", 30);"));

    // name == "alice" OR name == "carol"  ->  id=1 и id=3
    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE name == \"alice\" OR name == \"carol\";"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 2u);
    std::vector<int> ids;
    for (const auto& row : result) ids.push_back(row["id"].get<int>());
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 3);
}

// AND имеет более высокий приоритет:
// a == 1 OR b == 20 AND c == 30  =>  a==1 OR (b==20 AND c==30)
TEST(executorTests, selectWhereAndPrecedenceOverOr)
{
    const std::filesystem::path root = prepare_data_root("select_and_precedence_or");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE t (a INT, b INT, c INT);"));
    // строка 1: a==1 => попадает по левой части OR
    // строка 2: b==20 AND c==30 => попадает по правой части OR
    // строка 3: никаких совпадений
    ASSERT_TRUE(run_sql(exec, "INSERT INTO t (a, b, c) VALUE (1, 0, 0), (0, 20, 30), (0, 0, 0);"));

    ASSERT_TRUE(run_sql(exec, "SELECT a, b, c FROM t WHERE a == 1 OR b == 20 AND c == 30;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 2u);
}

// Скобки переопределяют приоритет:
// (a == 1 OR b == 20) AND c == 3
TEST(executorTests, selectWhereParenthesesOverridePrecedence)
{
    const std::filesystem::path root = prepare_data_root("select_parentheses_precedence");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE t (a INT, b INT, c INT);"));
    // строка 1: (a==1 OR false) AND c==3  => true AND true => match
    // строка 2: (a==1 OR b==20) AND c==99 => true AND false => no
    // строка 3: (false OR b==20) AND c==3 => true AND false => no (c!=3)
    ASSERT_TRUE(run_sql(exec, "INSERT INTO t (a, b, c) VALUE (1, 0, 3), (1, 20, 99), (0, 20, 0);"));

    ASSERT_TRUE(run_sql(exec, "SELECT a, b, c FROM t WHERE (a == 1 OR b == 20) AND c == 3;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["a"], 1);
    EXPECT_EQ(result[0]["b"], 0);
    EXPECT_EQ(result[0]["c"], 3);
}

TEST(executorTests, selectWhereAndBothFalseReturnsEmpty)
{
    const std::filesystem::path root = prepare_data_root("select_and_both_false_empty");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");"));

    // одна строка не может одновременно иметь id==1 и id==2
    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id == 1 AND id == 2;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 0u);
}

TEST(executorTests, selectWhereOrAllFalseReturnsEmpty)
{
    const std::filesystem::path root = prepare_data_root("select_or_all_false_empty");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\");"));

    ASSERT_TRUE(run_sql(exec, "SELECT id FROM users WHERE id == 99 OR id == 100;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 0u);
}

TEST(executorTests, updateWhereAndOnlyMatchingRows)
{
    const std::filesystem::path root = prepare_data_root("update_where_and_matching");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING, age INT);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name, age) VALUE (1, \"alice\", 20), (2, \"alice\", 25), (3, \"bob\", 20);"));

    // обновить только alice с age==20 (не alice-25, не bob)
    ASSERT_TRUE(run_sql(exec, "UPDATE users SET age = 99 WHERE name == \"alice\" AND age == 20;"));
    ASSERT_TRUE(run_sql(exec, "SELECT id, age FROM users WHERE age == 99;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 1);
    EXPECT_EQ(result[0]["age"], 99);
}

TEST(executorTests, deleteWhereOrRemovesMatchingRows)
{
    const std::filesystem::path root = prepare_data_root("delete_where_or_removes");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\"), (2, \"bob\"), (3, \"carol\");"));

    ASSERT_TRUE(run_sql(exec, "DELETE FROM users WHERE id == 1 OR id == 3;"));
    ASSERT_TRUE(run_sql(exec, "SELECT * FROM users;"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["id"], 2);
    EXPECT_EQ(result[0]["name"], "bob");
}

TEST(executorTests, whereAndWithUnknownColumnFails)
{
    const std::filesystem::path root = prepare_data_root("where_and_unknown_column");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");"));

    // missing_column не существует — семантическая ошибка
    EXPECT_FALSE(run_sql(exec, "SELECT id FROM users WHERE id == 1 AND missing == 2;"));
    EXPECT_NE(exec.last_error().find("Semantic error"), std::string::npos);
}

TEST(executorTests, whereOrWithUnknownColumnFails)
{
    const std::filesystem::path root = prepare_data_root("where_or_unknown_column");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE users (id INT, name STRING);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO users (id, name) VALUE (1, \"alice\");"));

    EXPECT_FALSE(run_sql(exec, "SELECT id FROM users WHERE id == 1 OR missing == 2;"));
    EXPECT_NE(exec.last_error().find("Semantic error"), std::string::npos);
}

TEST(executorTests, selectWhereComplexNestedCondition)
{
    const std::filesystem::path root = prepare_data_root("select_complex_nested");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE t (id INT, cat STRING, score INT);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO t (id, cat, score) VALUE "
                              "(1, \"a\", 10), (2, \"a\", 50), "
                              "(3, \"b\", 10), (4, \"b\", 50), "
                              "(5, \"c\", 99);"));

    // (cat == "a" AND score >= 50) OR (cat == "b" AND score < 20)
    // => id=2 (a,50) и id=3 (b,10)
    ASSERT_TRUE(run_sql(exec, "SELECT id FROM t WHERE "
                              "(cat == \"a\" AND score >= 50) OR "
                              "(cat == \"b\" AND score < 20);"));
    const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
    ASSERT_EQ(result.size(), 2u);
    std::vector<int> ids;
    for (const auto& row : result) ids.push_back(row["id"].get<int>());
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], 2);
    EXPECT_EQ(ids[1], 3);
}

TEST(executorTests, aggregateSumCountAvg)
{
    const std::filesystem::path root = prepare_data_root("aggregate_sum_count_avg");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE items (id INT INDEXED, name STRING, price INT NOT NULL);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO items (id, name, price) VALUE (1, \"a\", 10), (2, \"b\", 20), (3, NULL, 30);"));

    ASSERT_TRUE(run_sql(exec, "SELECT COUNT(id) FROM items;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["COUNT(id)"], "3");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT SUM(price) FROM items;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["SUM(price)"], "60");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT AVG(price) FROM items;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["AVG(price)"], "20.00");
    }

    // COUNT на nullable колонке считает только не-NULL значения
    ASSERT_TRUE(run_sql(exec, "SELECT COUNT(name) FROM items;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["COUNT(name)"], "2");
    }
}

TEST(executorTests, aggregateWithWhereFilter)
{
    const std::filesystem::path root = prepare_data_root("aggregate_with_where_filter");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE sales (id INT INDEXED, amount INT NOT NULL);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO sales (id, amount) VALUE (1, 100), (2, 200), (3, 300), (4, 400);"));

    ASSERT_TRUE(run_sql(exec, "SELECT COUNT(id) FROM sales WHERE amount >= 300;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["COUNT(id)"], "2");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT SUM(amount) FROM sales WHERE id <= 2;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["SUM(amount)"], "300");
    }
}

TEST(executorTests, aggregateOnEmptyResultSet)
{
    const std::filesystem::path root = prepare_data_root("aggregate_empty_result_set");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE items (id INT INDEXED, price INT NOT NULL);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO items (id, price) VALUE (1, 10), (2, 20);"));

    ASSERT_TRUE(run_sql(exec, "SELECT COUNT(id) FROM items WHERE price > 1000;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["COUNT(id)"], "0");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT SUM(price) FROM items WHERE price > 1000;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["SUM(price)"], "0");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT AVG(price) FROM items WHERE price > 1000;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_TRUE(result[0]["AVG(price)"].is_null());
    }
}

TEST(executorTests, aggregateAliasRenamesColumn)
{
    const std::filesystem::path root = prepare_data_root("aggregate_alias_renames_column");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE nums (id INT INDEXED, val INT NOT NULL);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO nums (id, val) VALUE (1, 5), (2, 15), (3, 30);"));

    ASSERT_TRUE(run_sql(exec, "SELECT SUM(val) AS total FROM nums;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_TRUE(result[0].contains("total"));
        EXPECT_FALSE(result[0].contains("SUM(val)"));
        EXPECT_EQ(result[0]["total"], "50");
    }

    ASSERT_TRUE(run_sql(exec, "SELECT AVG(val) AS mean FROM nums;"));
    {
        const nlohmann::json result = nlohmann::json::parse(exec.last_select_json());
        ASSERT_EQ(result.size(), 1u);
        EXPECT_TRUE(result[0].contains("mean"));
        EXPECT_EQ(result[0]["mean"], "16.67");
    }
}

TEST(executorTests, aggregateUnknownColumnFails)
{
    const std::filesystem::path root = prepare_data_root("aggregate_unknown_column_fails");
    Dbms dbms(root);
    Executor exec(dbms);

    ASSERT_TRUE(run_sql(exec, "CREATE DATABASE test;"));
    ASSERT_TRUE(run_sql(exec, "USE test;"));
    ASSERT_TRUE(run_sql(exec, "CREATE TABLE t (id INT INDEXED);"));
    ASSERT_TRUE(run_sql(exec, "INSERT INTO t (id) VALUE (1);"));

    EXPECT_FALSE(run_sql(exec, "SELECT SUM(nonexistent) FROM t;"));
    EXPECT_FALSE(run_sql(exec, "SELECT COUNT(bad_col) FROM t;"));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
