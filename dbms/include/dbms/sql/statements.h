#ifndef COURSEWORK_DBMS_SQL_AST_H
#define COURSEWORK_DBMS_SQL_AST_H

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dbms
{

struct CreateDatabaseStmt
{
    std::string name;
};

struct DropDatabaseStmt
{
    std::string name;
};

struct UseDatabaseStmt
{
    std::string name;
};

struct ColumnDef
{
    std::string name;
    std::string type;
    bool not_null;
    bool indexed;
    std::optional<std::string> default_value;
};

struct CreateTableStmt
{
    std::string name;
    std::vector<ColumnDef> columns;
};

struct DropTableStmt
{
    std::string name;
};

struct InsertValue
{
    bool is_null;
    std::string text;
};

struct InsertStmt
{
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<InsertValue>> rows;
};

// ---------- SELECT AST ----------

enum class AggregateFunc { sum, count, avg };

struct SelectItem
{
    std::string column_name;
    std::optional<std::string> alias;
    std::optional<AggregateFunc> aggregate; // nullopt = обычная колонка
};

struct SelectProjection
{
    bool is_star = false;              // true для SELECT *
    std::vector<SelectItem> items;     // пусто, если is_star == true
};

struct ColumnRef
{
    std::string name;                  // имя колонки
};

struct Literal
{
    bool is_null = false;              // true для NULL
    std::string text;                  // "10", "alice", "true"
};

using Operand = std::variant<ColumnRef, Literal>;

enum class ComparisonOp
{
    eq, // ==
    ne, // !=
    lt, // <
    gt, // >
    le, // <=
    ge  // >=
};

struct WhereComparison
{
    Operand lhs;
    ComparisonOp op;
    Operand rhs;
};

// пока можно не парсить, но держать в AST заранее
struct WhereBetween
{
    Operand value;
    Operand low;
    Operand high;
};

struct WhereLike
{
    Operand value;
    Operand pattern;
};

struct WhereAnd;
struct WhereOr;

using WhereCondition = std::variant<
    WhereComparison,
    WhereBetween,
    WhereLike,
    std::unique_ptr<WhereAnd>,
    std::unique_ptr<WhereOr>
>;

struct WhereAnd
{
    WhereCondition left;
    WhereCondition right;
};

struct WhereOr
{
    WhereCondition left;
    WhereCondition right;
};

struct SelectStmt
{
    SelectProjection projection;
    std::string table_name;
    std::optional<WhereCondition> where; // нет WHERE => nullopt
};

struct UpdateAssignment
{
    std::string column_name;
    InsertValue value;
};

struct UpdateStmt
{
    std::string table_name;
    std::vector<UpdateAssignment> assignments;
    std::optional<WhereCondition> where;
};

struct DeleteStmt
{
    std::string table_name;
    std::optional<WhereCondition> where;
};

using Statement = std::variant<
    CreateDatabaseStmt,
    DropDatabaseStmt,
    UseDatabaseStmt,
    CreateTableStmt,
    DropTableStmt,
    InsertStmt,
    SelectStmt,
    UpdateStmt,
    DeleteStmt>;

} // namespace dbms

#endif
