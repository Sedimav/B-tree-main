#include <gtest/gtest.h>
#include "dbms/sql/lexer.h"

using dbms::Keyword;
using dbms::Lexer;
using dbms::TokenType;

TEST(lexerTests, createTable)
{
    Lexer lexer("CREATE TABLE users (id INT, name STRING);");
    auto tokens = lexer.tokenize();
    ASSERT_GT(tokens.size(), 0u);
    EXPECT_EQ(tokens[0].type, TokenType::keyword);
    EXPECT_EQ(tokens[0].keyword, Keyword::create_kw);
    EXPECT_EQ(tokens[1].keyword, Keyword::table_kw);
    EXPECT_EQ(tokens[2].type, TokenType::identifier);
    EXPECT_EQ(tokens[2].text, "users");
    EXPECT_EQ(tokens[3].symbol, '(');
    EXPECT_EQ(tokens[4].type, TokenType::identifier);
    EXPECT_EQ(tokens[4].text, "id");
    EXPECT_EQ(tokens[5].keyword, Keyword::int_kw);
    EXPECT_EQ(tokens[6].symbol, ',');
    EXPECT_EQ(tokens[7].type, TokenType::identifier);
    EXPECT_EQ(tokens[7].text, "name");
    EXPECT_EQ(tokens[8].keyword, Keyword::string_kw);
    EXPECT_EQ(tokens[9].symbol, ')');
    EXPECT_EQ(tokens[10].symbol, ';');
    EXPECT_EQ(tokens.back().type, TokenType::eof_token);
}

TEST(lexerTests, stringLiteral)
{
    Lexer lexer("USE \"mydb\";");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].keyword, Keyword::use_kw);
    EXPECT_EQ(tokens[1].type, TokenType::string_literal);
    EXPECT_EQ(tokens[1].text, "mydb");
    EXPECT_EQ(tokens[2].symbol, ';');
}

TEST(lexerTests, singleQuotedStringRejected)
{
    Lexer lexer("USE 'mydb';");
    EXPECT_THROW((void)lexer.tokenize(), std::runtime_error);
}

TEST(lexerTests, mixedCaseKeywordRejected)
{
    Lexer lexer("CrEaTe DATABASE test;");
    EXPECT_THROW((void)lexer.tokenize(), std::runtime_error);
}

TEST(lexerTests, mixedCaseIdentifierAllowed)
{
    Lexer lexer("CREATE TABLE userName (id INT);");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[2].type, TokenType::identifier);
    EXPECT_EQ(tokens[2].text, "userName");
}

TEST(lexerTests, insert)
{
    Lexer lexer("INSERT INTO users (id) VALUE (-10);");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 11u);
    EXPECT_EQ(tokens[0].type, TokenType::keyword);
    EXPECT_EQ(tokens[0].keyword, Keyword::insert_kw);
    EXPECT_EQ(tokens[1].keyword, Keyword::into_kw);
    EXPECT_EQ(tokens[2].type, TokenType::identifier);
    EXPECT_EQ(tokens[2].text, "users");
    EXPECT_EQ(tokens[3].symbol, '(');
    EXPECT_EQ(tokens[4].type, TokenType::identifier);
    EXPECT_EQ(tokens[4].text, "id");
    EXPECT_EQ(tokens[5].symbol, ')');
    EXPECT_EQ(tokens[6].keyword, Keyword::value_kw);
    EXPECT_EQ(tokens[7].symbol, '(');
    EXPECT_EQ(tokens[8].symbol, '-');
    EXPECT_EQ(tokens[9].type, TokenType::number);
    EXPECT_EQ(tokens[9].text, "10");
}

TEST(lexerTests, selectKeywords)
{
    Lexer lexer("SELECT name AS n FROM users WHERE age BETWEEN 18 AND 30 AND name LIKE \"a.*\";");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 17u);
    EXPECT_EQ(tokens[0].keyword, Keyword::select_kw);
    EXPECT_EQ(tokens[2].keyword, Keyword::as_kw);
    EXPECT_EQ(tokens[4].keyword, Keyword::from_kw);
    EXPECT_EQ(tokens[6].keyword, Keyword::where_kw);
    EXPECT_EQ(tokens[8].keyword, Keyword::between_kw);
    EXPECT_EQ(tokens[10].keyword, Keyword::and_kw);
    EXPECT_EQ(tokens[12].keyword, Keyword::and_kw);
    EXPECT_EQ(tokens[14].keyword, Keyword::like_kw);
    EXPECT_EQ(tokens[15].type, TokenType::string_literal);
    EXPECT_EQ(tokens[16].symbol, ';');
}

TEST(lexerTests, deleteKeywords)
{
    Lexer lexer("DELETE FROM users WHERE id == 1;");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 9u);
    EXPECT_EQ(tokens[0].type, TokenType::keyword);
    EXPECT_EQ(tokens[0].keyword, Keyword::delete_kw);
    EXPECT_EQ(tokens[1].keyword, Keyword::from_kw);
    EXPECT_EQ(tokens[2].type, TokenType::identifier);
    EXPECT_EQ(tokens[2].text, "users");
    EXPECT_EQ(tokens[3].keyword, Keyword::where_kw);
    EXPECT_EQ(tokens[4].type, TokenType::identifier);
    EXPECT_EQ(tokens[4].text, "id");
    EXPECT_EQ(tokens[5].type, TokenType::symbol);
    EXPECT_EQ(tokens[5].text, "==");
    EXPECT_EQ(tokens[6].type, TokenType::number);
    EXPECT_EQ(tokens[6].text, "1");
    EXPECT_EQ(tokens[7].symbol, ';');
}

TEST(lexerTests, comparisonOperators)
{
    Lexer lexer("SELECT * FROM users WHERE a == 1 AND b != 2 AND c <= 3 AND d >= 4 AND e < 5 AND f > 6;");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 30u);
    EXPECT_EQ(tokens[1].type, TokenType::symbol);
    EXPECT_EQ(tokens[1].text, "*");
    EXPECT_EQ(tokens[6].type, TokenType::symbol);
    EXPECT_EQ(tokens[6].text, "==");
    EXPECT_EQ(tokens[10].type, TokenType::symbol);
    EXPECT_EQ(tokens[10].text, "!=");
    EXPECT_EQ(tokens[14].type, TokenType::symbol);
    EXPECT_EQ(tokens[14].text, "<=");
    EXPECT_EQ(tokens[18].type, TokenType::symbol);
    EXPECT_EQ(tokens[18].text, ">=");
    EXPECT_EQ(tokens[22].type, TokenType::symbol);
    EXPECT_EQ(tokens[22].text, "<");
    EXPECT_EQ(tokens[26].type, TokenType::symbol);
    EXPECT_EQ(tokens[26].text, ">");
}
