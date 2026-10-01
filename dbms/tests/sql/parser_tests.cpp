#include <gtest/gtest.h>
#include "dbms/sql/lexer.h"
#include "dbms/sql/parser.h"

using dbms::ColumnDef;
using dbms::ColumnRef;
using dbms::ComparisonOp;
using dbms::CreateTableStmt;
using dbms::DeleteStmt;
using dbms::DropTableStmt;
using dbms::InsertStmt;
using dbms::Lexer;
using dbms::Literal;
using dbms::Parser;
using dbms::SelectStmt;
using dbms::Statement;
using dbms::UpdateStmt;
using dbms::WhereComparison;
using dbms::WhereBetween;
using dbms::WhereLike;
using dbms::WhereAnd;
using dbms::WhereOr;

TEST(parserTests, createTable)
{
    Lexer lexer("CREATE TABLE users (id INT NOT NULL, name STRING INDEXED);");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* create = std::get_if<CreateTableStmt>(&stmt);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->name, "users");
    ASSERT_EQ(create->columns.size(), 2u);
    EXPECT_EQ(create->columns[0].name, "id");
    EXPECT_EQ(create->columns[0].type, "INT");
    EXPECT_TRUE(create->columns[0].not_null);
    EXPECT_FALSE(create->columns[0].indexed);
    EXPECT_EQ(create->columns[1].name, "name");
    EXPECT_EQ(create->columns[1].type, "STRING");
    EXPECT_FALSE(create->columns[1].not_null);
    EXPECT_TRUE(create->columns[1].indexed);
}

TEST(parserTests, dropTable)
{
    Lexer lexer("DROP TABLE users;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* drop = std::get_if<DropTableStmt>(&stmt);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->name, "users");
}

TEST(parserTests, stringLiteralCannotBeEntityName)
{
    {
        Lexer lexer("CREATE DATABASE \"bad name\";");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
    {
        Lexer lexer("CREATE TABLE \"users\" (id INT);");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
    {
        Lexer lexer("CREATE TABLE users (\"id\" INT);");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
}

TEST(parserTests, insert)
{
    Lexer lexer("INSERT INTO users (id, name, active) VALUE (10, \"alice\", true), (11, NULL, false);");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* insert = std::get_if<InsertStmt>(&stmt);
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table_name, "users");
    ASSERT_EQ(insert->columns.size(), 3u);
    EXPECT_EQ(insert->columns[0], "id");
    EXPECT_EQ(insert->columns[1], "name");
    EXPECT_EQ(insert->columns[2], "active");
    ASSERT_EQ(insert->rows.size(), 2u);
    ASSERT_EQ(insert->rows[0].size(), 3u);
    EXPECT_FALSE(insert->rows[0][0].is_null);
    EXPECT_EQ(insert->rows[0][0].text, "10");
    EXPECT_FALSE(insert->rows[0][1].is_null);
    EXPECT_EQ(insert->rows[0][1].text, "alice");
    EXPECT_FALSE(insert->rows[0][2].is_null);
    EXPECT_EQ(insert->rows[0][2].text, "true");
    ASSERT_EQ(insert->rows[1].size(), 3u);
    EXPECT_TRUE(insert->rows[1][1].is_null);
}

TEST(parserTests, selectStar)
{
    Lexer lexer("SELECT * FROM users;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    EXPECT_TRUE(select->projection.is_star);
    EXPECT_TRUE(select->projection.items.empty());
    EXPECT_EQ(select->table_name, "users");
    EXPECT_FALSE(select->where.has_value());
}

TEST(parserTests, selectWithAliasAndWhereComparison)
{
    Lexer lexer("SELECT id AS user_id, age FROM users WHERE age >= 18;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);

    EXPECT_FALSE(select->projection.is_star);
    ASSERT_EQ(select->projection.items.size(), 2u);
    EXPECT_EQ(select->projection.items[0].column_name, "id");
    ASSERT_TRUE(select->projection.items[0].alias.has_value());
    EXPECT_EQ(select->projection.items[0].alias.value(), "user_id");
    EXPECT_EQ(select->projection.items[1].column_name, "age");
    EXPECT_FALSE(select->projection.items[1].alias.has_value());
    EXPECT_EQ(select->table_name, "users");

    ASSERT_TRUE(select->where.has_value());
    auto* where = std::get_if<WhereComparison>(&select->where.value());
    ASSERT_NE(where, nullptr);
    EXPECT_EQ(where->op, ComparisonOp::ge);

    auto* lhs = std::get_if<ColumnRef>(&where->lhs);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->name, "age");

    auto* rhs = std::get_if<Literal>(&where->rhs);
    ASSERT_NE(rhs, nullptr);
    EXPECT_FALSE(rhs->is_null);
    EXPECT_EQ(rhs->text, "18");
}

TEST(parserTests, selectWhereBetween)
{
    Lexer lexer("SELECT id FROM users WHERE id BETWEEN 2 AND 4;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);

    ASSERT_TRUE(select->where.has_value());
    const auto* where = std::get_if<WhereBetween>(&select->where.value());
    ASSERT_NE(where, nullptr);

    const auto* value = std::get_if<ColumnRef>(&where->value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->name, "id");

    const auto* low = std::get_if<Literal>(&where->low);
    ASSERT_NE(low, nullptr);
    EXPECT_EQ(low->text, "2");

    const auto* high = std::get_if<Literal>(&where->high);
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(high->text, "4");
}

TEST(parserTests, selectWhereLike)
{
    Lexer lexer("SELECT name FROM users WHERE name LIKE \"a.*\";");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);

    ASSERT_TRUE(select->where.has_value());
    const auto* where = std::get_if<WhereLike>(&select->where.value());
    ASSERT_NE(where, nullptr);

    const auto* value = std::get_if<ColumnRef>(&where->value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->name, "name");

    const auto* pattern = std::get_if<Literal>(&where->pattern);
    ASSERT_NE(pattern, nullptr);
    EXPECT_EQ(pattern->text, "a.*");
}

TEST(parserTests, malformedNamesAreRejected)
{
    {
        Lexer lexer("CREATE DATABASE 1bad;");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
    {
        Lexer lexer("SELECT * FROM db..users;");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
    {
        Lexer lexer("CREATE TABLE users (. INT);");
        Parser parser(lexer.tokenize());
        EXPECT_THROW(parser.parse_statement(), std::runtime_error);
    }
}

TEST(parserTests, updateWithSetAndWhereComparison)
{
    Lexer lexer("UPDATE users SET age = 21, name = \"bob\" WHERE id == 1;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* update = std::get_if<UpdateStmt>(&stmt);
    ASSERT_NE(update, nullptr);

    EXPECT_EQ(update->table_name, "users");
    ASSERT_EQ(update->assignments.size(), 2u);
    EXPECT_EQ(update->assignments[0].column_name, "age");
    EXPECT_FALSE(update->assignments[0].value.is_null);
    EXPECT_EQ(update->assignments[0].value.text, "21");
    EXPECT_EQ(update->assignments[1].column_name, "name");
    EXPECT_FALSE(update->assignments[1].value.is_null);
    EXPECT_EQ(update->assignments[1].value.text, "bob");

    ASSERT_TRUE(update->where.has_value());
    const auto* where = std::get_if<WhereComparison>(&update->where.value());
    ASSERT_NE(where, nullptr);
    EXPECT_EQ(where->op, ComparisonOp::eq);

    const auto* lhs = std::get_if<ColumnRef>(&where->lhs);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->name, "id");

    const auto* rhs = std::get_if<Literal>(&where->rhs);
    ASSERT_NE(rhs, nullptr);
    EXPECT_FALSE(rhs->is_null);
    EXPECT_EQ(rhs->text, "1");
}

TEST(parserTests, deleteWithWhereComparison)
{
    Lexer lexer("DELETE FROM users WHERE id == 10;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* del = std::get_if<DeleteStmt>(&stmt);
    ASSERT_NE(del, nullptr);

    EXPECT_EQ(del->table_name, "users");
    ASSERT_TRUE(del->where.has_value());
    const auto* where = std::get_if<WhereComparison>(&del->where.value());
    ASSERT_NE(where, nullptr);
    EXPECT_EQ(where->op, ComparisonOp::eq);

    const auto* lhs = std::get_if<ColumnRef>(&where->lhs);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->name, "id");

    const auto* rhs = std::get_if<Literal>(&where->rhs);
    ASSERT_NE(rhs, nullptr);
    EXPECT_FALSE(rhs->is_null);
    EXPECT_EQ(rhs->text, "10");
}

TEST(parserTests, deleteWithoutWhere)
{
    Lexer lexer("DELETE FROM users;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* del = std::get_if<DeleteStmt>(&stmt);
    ASSERT_NE(del, nullptr);

    EXPECT_EQ(del->table_name, "users");
    EXPECT_FALSE(del->where.has_value());
}

// ============ AND / OR / скобки ============

TEST(parserTests, whereAndProducesWhereAndNode)
{
    Lexer lexer("SELECT * FROM t WHERE a == 1 AND b == 2;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());

    const auto* and_node = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&select->where.value());
    ASSERT_NE(and_node, nullptr);
    ASSERT_NE(*and_node, nullptr);

    // оба операнда — простые сравнения
    const auto* left = std::get_if<WhereComparison>(&(*and_node)->left);
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op, ComparisonOp::eq);
    const auto* lhs_col = std::get_if<ColumnRef>(&left->lhs);
    ASSERT_NE(lhs_col, nullptr);
    EXPECT_EQ(lhs_col->name, "a");

    const auto* right = std::get_if<WhereComparison>(&(*and_node)->right);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->op, ComparisonOp::eq);
    const auto* rhs_col = std::get_if<ColumnRef>(&right->lhs);
    ASSERT_NE(rhs_col, nullptr);
    EXPECT_EQ(rhs_col->name, "b");
}

TEST(parserTests, whereOrProducesWhereOrNode)
{
    Lexer lexer("SELECT * FROM t WHERE a == 1 OR b == 2;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());

    const auto* or_node = std::get_if<std::unique_ptr<dbms::WhereOr>>(&select->where.value());
    ASSERT_NE(or_node, nullptr);
    ASSERT_NE(*or_node, nullptr);

    const auto* left = std::get_if<WhereComparison>(&(*or_node)->left);
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op, ComparisonOp::eq);

    const auto* right = std::get_if<WhereComparison>(&(*or_node)->right);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->op, ComparisonOp::eq);
}

// AND имеет более высокий приоритет, чем OR:
// a == 1 OR b == 2 AND c == 3  =>  OR( a==1, AND(b==2, c==3) )
TEST(parserTests, andPrecedenceHigherThanOr)
{
    Lexer lexer("SELECT * FROM t WHERE a == 1 OR b == 2 AND c == 3;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());

    // корень — OR
    const auto* or_node = std::get_if<std::unique_ptr<dbms::WhereOr>>(&select->where.value());
    ASSERT_NE(or_node, nullptr);
    ASSERT_NE(*or_node, nullptr);

    // левый операнд OR — простое сравнение (a == 1)
    const auto* left_cmp = std::get_if<WhereComparison>(&(*or_node)->left);
    ASSERT_NE(left_cmp, nullptr);
    const auto* left_col = std::get_if<ColumnRef>(&left_cmp->lhs);
    ASSERT_NE(left_col, nullptr);
    EXPECT_EQ(left_col->name, "a");

    // правый операнд OR — AND(b==2, c==3)
    const auto* right_and = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&(*or_node)->right);
    ASSERT_NE(right_and, nullptr);
    ASSERT_NE(*right_and, nullptr);

    const auto* and_left = std::get_if<WhereComparison>(&(*right_and)->left);
    ASSERT_NE(and_left, nullptr);
    const auto* and_left_col = std::get_if<ColumnRef>(&and_left->lhs);
    ASSERT_NE(and_left_col, nullptr);
    EXPECT_EQ(and_left_col->name, "b");

    const auto* and_right = std::get_if<WhereComparison>(&(*right_and)->right);
    ASSERT_NE(and_right, nullptr);
    const auto* and_right_col = std::get_if<ColumnRef>(&and_right->lhs);
    ASSERT_NE(and_right_col, nullptr);
    EXPECT_EQ(and_right_col->name, "c");
}

// Скобки переопределяют приоритет:
// (a == 1 OR b == 2) AND c == 3  =>  AND( OR(a==1, b==2), c==3 )
TEST(parserTests, parenthesesOverridePrecedence)
{
    Lexer lexer("SELECT * FROM t WHERE (a == 1 OR b == 2) AND c == 3;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());

    // корень — AND
    const auto* and_node = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&select->where.value());
    ASSERT_NE(and_node, nullptr);
    ASSERT_NE(*and_node, nullptr);

    // левый операнд AND — OR(a==1, b==2)
    const auto* left_or = std::get_if<std::unique_ptr<dbms::WhereOr>>(&(*and_node)->left);
    ASSERT_NE(left_or, nullptr);
    ASSERT_NE(*left_or, nullptr);

    // правый операнд AND — простое сравнение (c == 3)
    const auto* right_cmp = std::get_if<WhereComparison>(&(*and_node)->right);
    ASSERT_NE(right_cmp, nullptr);
    const auto* right_col = std::get_if<ColumnRef>(&right_cmp->lhs);
    ASSERT_NE(right_col, nullptr);
    EXPECT_EQ(right_col->name, "c");
}

// Цепочка AND левоассоциативна: a AND b AND c => AND(AND(a,b), c)
TEST(parserTests, chainedAndIsLeftAssociative)
{
    Lexer lexer("SELECT * FROM t WHERE a == 1 AND b == 2 AND c == 3;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* select = std::get_if<SelectStmt>(&stmt);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());

    // корень — AND
    const auto* outer_and = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&select->where.value());
    ASSERT_NE(outer_and, nullptr);
    ASSERT_NE(*outer_and, nullptr);

    // правый — c==3
    const auto* right_cmp = std::get_if<WhereComparison>(&(*outer_and)->right);
    ASSERT_NE(right_cmp, nullptr);
    const auto* right_col = std::get_if<ColumnRef>(&right_cmp->lhs);
    ASSERT_NE(right_col, nullptr);
    EXPECT_EQ(right_col->name, "c");

    // левый — AND(a==1, b==2)
    const auto* inner_and = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&(*outer_and)->left);
    ASSERT_NE(inner_and, nullptr);
    ASSERT_NE(*inner_and, nullptr);

    const auto* inner_left = std::get_if<WhereComparison>(&(*inner_and)->left);
    ASSERT_NE(inner_left, nullptr);
    const auto* inner_left_col = std::get_if<ColumnRef>(&inner_left->lhs);
    ASSERT_NE(inner_left_col, nullptr);
    EXPECT_EQ(inner_left_col->name, "a");
}

// AND/OR работает в DELETE и UPDATE тоже
TEST(parserTests, deleteWhereOr)
{
    Lexer lexer("DELETE FROM users WHERE id == 1 OR id == 2;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* del = std::get_if<DeleteStmt>(&stmt);
    ASSERT_NE(del, nullptr);
    ASSERT_TRUE(del->where.has_value());

    const auto* or_node = std::get_if<std::unique_ptr<dbms::WhereOr>>(&del->where.value());
    ASSERT_NE(or_node, nullptr);
}

TEST(parserTests, updateWhereAnd)
{
    Lexer lexer("UPDATE users SET age = 30 WHERE name == \"alice\" AND age < 25;");
    Parser parser(lexer.tokenize());
    Statement stmt = parser.parse_statement();
    auto* upd = std::get_if<UpdateStmt>(&stmt);
    ASSERT_NE(upd, nullptr);
    ASSERT_TRUE(upd->where.has_value());

    const auto* and_node = std::get_if<std::unique_ptr<dbms::WhereAnd>>(&upd->where.value());
    ASSERT_NE(and_node, nullptr);
}
