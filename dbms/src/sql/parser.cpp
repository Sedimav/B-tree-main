#include "dbms/sql/parser.h"

namespace dbms
{

Statement Parser::parse_statement()
{
    if (match_keyword(Keyword::create_kw)) {
        return parse_create();
    }
    if (match_keyword(Keyword::drop_kw)) {
        return parse_drop();
    }
    if (match_keyword(Keyword::use_kw)) {
        return parse_use();
    }
    if (match_keyword(Keyword::insert_kw)) {
        return parse_insert();
    }
    if (match_keyword(Keyword::select_kw)) {
        return parse_select();
    }
    if (match_keyword(Keyword::update_kw)) {
        return parse_update();
    }
    if (match_keyword(Keyword::delete_kw)) {
        return parse_delete();
    }
    throw std::runtime_error("Unexpected token");
}

const Token& Parser::peek() const
{
    if (_pos >= _tokens.size()) {
        return _tokens.back();
    }
    return _tokens[_pos];
}

const Token& Parser::consume()
{
    const Token& t = peek();
    if (_pos < _tokens.size()) {
        _pos += 1;
    }
    return t;
}

bool Parser::match_keyword(Keyword kw)
{
    if (peek().type == TokenType::keyword && peek().keyword == kw) {
        consume();
        return true;
    }
    return false;
}

bool Parser::match_symbol(char c)
{
    if (peek().type == TokenType::symbol && peek().symbol == c) {
        consume();
        return true;
    }
    return false;
}

void Parser::expect_keyword(Keyword kw)
{
    if (!match_keyword(kw)) {
        throw std::runtime_error("Expected keyword");
    }
}

void Parser::expect_symbol(char c)
{
    if (!match_symbol(c)) {
        throw std::runtime_error(std::string("Expected '") + c + "'");
    }
}

std::string Parser::parse_table_name()
{
    std::string name = parse_name();
    if (match_symbol('.')) {
        name += ".";
        name += parse_name();
    }
    return name;
}


std::string Parser::parse_name()
{
    const Token& t = peek();
    if (t.type == TokenType::identifier) {
        consume();
        return t.text;
    }
    throw std::runtime_error("Expected name");
}

Statement Parser::parse_create()
{
    if (match_keyword(Keyword::table_kw)) {
        return parse_create_table();
    }
    if (match_keyword(Keyword::database_kw)) {
        return parse_create_database();
    }
    throw std::runtime_error("Expected TABLE or DATABASE");
}

Statement Parser::parse_drop()
{
    if (match_keyword(Keyword::table_kw)) {
        return parse_drop_table();
    }
    if (match_keyword(Keyword::database_kw)) {
        return parse_drop_database();
    }
    throw std::runtime_error("Expected TABLE or DATABASE");
}

Statement Parser::parse_use()
{
    UseDatabaseStmt stmt{};
    stmt.name = parse_name();
    expect_symbol(';');
    return stmt;
}

Statement Parser::parse_insert()
{
    expect_keyword(Keyword::into_kw);
    InsertStmt stmt{};
    stmt.table_name = parse_table_name();
    expect_symbol('(');
    stmt.columns.push_back(parse_name());
    while (match_symbol(',')) {
        stmt.columns.push_back(parse_name());
    }
    expect_symbol(')');
    if (!match_keyword(Keyword::value_kw)) {
        expect_keyword(Keyword::values_kw);
    }
    stmt.rows.push_back(parse_insert_row());
    while (match_symbol(',')) {
        stmt.rows.push_back(parse_insert_row());
    }
    expect_symbol(';');
    return stmt;
}

CreateDatabaseStmt Parser::parse_create_database()
{
    CreateDatabaseStmt stmt{};
    stmt.name = parse_name();
    expect_symbol(';');
    return stmt;
}

DropDatabaseStmt Parser::parse_drop_database()
{
    DropDatabaseStmt stmt{};
    stmt.name = parse_name();
    expect_symbol(';');
    return stmt;
}

CreateTableStmt Parser::parse_create_table()
{
    CreateTableStmt stmt{};
    stmt.name = parse_name();
    expect_symbol('(');
    stmt.columns.push_back(parse_column_def());
    while (match_symbol(',')) {
        stmt.columns.push_back(parse_column_def());
    }
    expect_symbol(')');
    expect_symbol(';');
    return stmt;
}

DropTableStmt Parser::parse_drop_table()
{
    DropTableStmt stmt{};
    stmt.name = parse_name();
    expect_symbol(';');
    return stmt;
}

std::vector<InsertValue> Parser::parse_insert_row()
{
    std::vector<InsertValue> row;
    expect_symbol('(');
    row.push_back(parse_insert_value());
    while (match_symbol(',')) {
        row.push_back(parse_insert_value());
    }
    expect_symbol(')');
    return row;
}

InsertValue Parser::parse_insert_value()
{
    if (match_keyword(Keyword::null_kw)) {
        return InsertValue{true, {}};
    }
    if (match_symbol('-')) {
        const Token& n = peek();
        if (n.type != TokenType::number) {
            throw std::runtime_error("Expected number after minus");
        }
        consume();
        return InsertValue{false, "-" + n.text};
    }
    const Token& t = peek();
    if (t.type == TokenType::number || t.type == TokenType::string_literal || t.type == TokenType::identifier) {
        consume();
        return InsertValue{false, t.text};
    }
    throw std::runtime_error("Expected value");
}

ColumnDef Parser::parse_column_def()
{
    ColumnDef col{};
    col.name = parse_name();
    col.type = parse_type_name();
    col.not_null = false;
    col.indexed = false;
    bool advanced = true;
    while (advanced) {
        advanced = false;
        if (match_keyword(Keyword::not_kw)) {
            expect_keyword(Keyword::null_kw);
            col.not_null = true;
            advanced = true;
        }
        if (match_keyword(Keyword::indexed_kw)) {
            col.indexed = true;
            advanced = true;
        }
        if (match_keyword(Keyword::default_kw)) {
            InsertValue val = parse_insert_value();
            if (val.is_null) {
                col.default_value = std::nullopt;
            } else {
                col.default_value = val.text;
            }
            advanced = true;
        }
    }
    return col;
}

std::string Parser::parse_type_name()
{
    if (match_keyword(Keyword::int_kw)) {
        return "INT";
    }
    if (match_keyword(Keyword::string_kw)) {
        return "STRING";
    }
    if (match_keyword(Keyword::bool_kw)) {
        return "BOOL";
    }
    const Token& t = peek();
    if (t.type == TokenType::identifier) {
        consume();
        std::string upper = t.text;
        for (char& ch : upper) {
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<char>(ch - 'a' + 'A');
            }
        }
        return upper;
    }
    throw std::runtime_error("Expected type");
}

SelectStmt Parser::parse_select()
{
    SelectStmt stmt{};
    stmt.projection = parse_select_projection();
    expect_keyword(Keyword::from_kw);
    stmt.table_name = parse_table_name();
    if (match_keyword(Keyword::where_kw)) {
        stmt.where = parse_select_where();
    } else {
        stmt.where = std::nullopt;
    }
    expect_symbol(';');
    return stmt;
}

SelectProjection Parser::parse_select_projection()
{
    SelectProjection projection{};
    if (match_symbol('*')) {
        projection.is_star = true;
        return projection;
    }

    projection.is_star = false;
    projection.items.push_back(parse_select_item());
    while (match_symbol(',')) {
        projection.items.push_back(parse_select_item());
    }
    return projection;
}

SelectItem Parser::parse_select_item()
{
    SelectItem item{};

    AggregateFunc agg{};
    bool is_agg = false;
    if (match_keyword(Keyword::sum_kw)) {
        agg = AggregateFunc::sum; is_agg = true;
    } else if (match_keyword(Keyword::count_kw)) {
        agg = AggregateFunc::count; is_agg = true;
    } else if (match_keyword(Keyword::avg_kw)) {
        agg = AggregateFunc::avg; is_agg = true;
    }

    if (is_agg) {
        expect_symbol('(');
        item.column_name = parse_name();
        expect_symbol(')');
        item.aggregate = agg;
    } else {
        item.column_name = parse_name();
    }

    if (match_keyword(Keyword::as_kw)) {
        item.alias = parse_name();
    }
    return item;
}

Operand Parser::parse_operand()
{
    if (match_keyword(Keyword::null_kw)) {
        return Literal{true, {}};
    }
    if (match_symbol('-')) {
        const Token& n = peek();
        if (n.type != TokenType::number) {
            throw std::runtime_error("Expected number after minus");
        }
        consume();
        return Literal{false, "-" + n.text};
    }
    const Token& t = peek();
    if (t.type == TokenType::identifier) {
        consume();
        return ColumnRef{t.text};
    }
    if (t.type == TokenType::number || t.type == TokenType::string_literal) {
        consume();
        return Literal{false, t.text};
    }
    throw std::runtime_error("Expected operand");
}

ComparisonOp Parser::parse_comparison_op()
{
    const Token& t = peek();
    if (t.type != TokenType::symbol) {
        throw std::runtime_error("Expected comparison operator");
    }
    const std::string op = t.text;
    consume();

    if (op == "==") {
        return ComparisonOp::eq;
    }
    if (op == "!=") {
        return ComparisonOp::ne;
    }
    if (op == "<") {
        return ComparisonOp::lt;
    }
    if (op == ">") {
        return ComparisonOp::gt;
    }
    if (op == "<=") {
        return ComparisonOp::le;
    }
    if (op == ">=") {
        return ComparisonOp::ge;
    }
    throw std::runtime_error("Unsupported comparison operator");
}

WhereCondition Parser::parse_select_where()
{
    return parse_where_or();
}

WhereCondition Parser::parse_where_or()
{
    WhereCondition left = parse_where_and();
    while (match_keyword(Keyword::or_kw)) {
        WhereCondition right = parse_where_and();
        left = std::make_unique<WhereOr>(WhereOr{std::move(left), std::move(right)});
    }
    return left;
}

WhereCondition Parser::parse_where_and()
{
    WhereCondition left = parse_where_atom();
    while (match_keyword(Keyword::and_kw)) {
        WhereCondition right = parse_where_atom();
        left = std::make_unique<WhereAnd>(WhereAnd{std::move(left), std::move(right)});
    }
    return left;
}

WhereCondition Parser::parse_where_atom()
{
    if (match_symbol('(')) {
        WhereCondition cond = parse_where_or();
        expect_symbol(')');
        return cond;
    }

    Operand lhs = parse_operand();

    if (match_keyword(Keyword::between_kw)) {
        WhereBetween where{};
        where.value = std::move(lhs);
        where.low = parse_operand();
        expect_keyword(Keyword::and_kw);
        where.high = parse_operand();
        return where;
    }

    if (match_keyword(Keyword::like_kw)) {
        WhereLike where{};
        where.value = std::move(lhs);
        where.pattern = parse_operand();
        return where;
    }

    WhereComparison where{};
    where.lhs = std::move(lhs);
    where.op = parse_comparison_op();
    where.rhs = parse_operand();
    return where;
}

Statement Parser::parse_update()
{
    UpdateStmt stmt{};
    stmt.table_name = parse_table_name();
    expect_keyword(Keyword::set_kw);

    UpdateAssignment first{};
    first.column_name = parse_name();
    expect_symbol('=');
    first.value = parse_insert_value();
    stmt.assignments.push_back(std::move(first));

    while (match_symbol(',')) {
        UpdateAssignment next{};
        next.column_name = parse_name();
        expect_symbol('=');
        next.value = parse_insert_value();
        stmt.assignments.push_back(std::move(next));
    }

    if (match_keyword(Keyword::where_kw)) {
        stmt.where = parse_select_where();
    } else {
        stmt.where = std::nullopt;
    }

    expect_symbol(';');
    return stmt;
}

Statement Parser::parse_delete()
{
    DeleteStmt stmt{};
    expect_keyword(Keyword::from_kw);
    stmt.table_name = parse_table_name();
    if (match_keyword(Keyword::where_kw)) {
        stmt.where = parse_select_where();
    } else {
        stmt.where = std::nullopt;
    }
    
    expect_symbol(';');
    return stmt;
}

}
