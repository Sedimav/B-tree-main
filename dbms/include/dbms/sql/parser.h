#ifndef COURSEWORK_DBMS_SQL_PARSER_H
#define COURSEWORK_DBMS_SQL_PARSER_H

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "statements.h"
#include "token.h"

namespace dbms
{

class Parser
{
public:
    explicit Parser(std::vector<Token> tokens) : _tokens(std::move(tokens)), _pos(0) {}

    Statement parse_statement();

private:
    std::vector<Token> _tokens;
    std::size_t _pos;

    const Token& peek() const;
    const Token& consume();
    bool match_keyword(Keyword kw);
    bool match_symbol(char c);
    void expect_keyword(Keyword kw);
    void expect_symbol(char c);
    std::string parse_name();
    std::string parse_table_name();

    Statement parse_create();
    Statement parse_drop();
    Statement parse_use();
    Statement parse_insert();
    Statement parse_update();
    Statement parse_delete();
    SelectStmt parse_select();

    CreateTableStmt parse_create_table();
    DropTableStmt parse_drop_table();
    CreateDatabaseStmt parse_create_database();
    DropDatabaseStmt parse_drop_database();
    ColumnDef parse_column_def();
    std::vector<InsertValue> parse_insert_row();
    InsertValue parse_insert_value();
    std::string parse_type_name();
    SelectProjection parse_select_projection();
    SelectItem parse_select_item();
    Operand parse_operand();
    ComparisonOp parse_comparison_op();
    WhereCondition parse_select_where();
    WhereCondition parse_where_or();
    WhereCondition parse_where_and();
    WhereCondition parse_where_atom();
};

}

#endif
