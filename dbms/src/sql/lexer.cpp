#include "dbms/sql/lexer.h"
#include <stdexcept>
namespace dbms
{

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    while (true) {
        skip_whitespace();
        if (_pos >= _input.size()) {
            Token t{};
            t.type = TokenType::eof_token;
            t.text = "";
            t.keyword = Keyword::create_kw;
            t.symbol = '\0';
            tokens.push_back(t);
            return tokens;
        }
        char c = _input[_pos];
        if (is_ident_start(c)) {
            Token t = read_identifier_or_keyword();
            if (t.type == TokenType::identifier) {
                bool has_lower = false;
                bool has_upper = false;
                std::string lower = t.text;
                for (char& ch : lower) {
                    if (ch >= 'a' && ch <= 'z') has_lower = true;
                    if (ch >= 'A' && ch <= 'Z') has_upper = true;
                    ch = to_lower(ch);
                }
                if (lower == "not_null") {
                    if (has_lower && has_upper) {
                        throw std::runtime_error("Mixed-case keywords are not allowed");
                    }
                    Token a{};
                    a.type = TokenType::keyword;
                    a.text = t.text;
                    a.keyword = Keyword::not_kw;
                    a.symbol = '\0';
                    Token b{};
                    b.type = TokenType::keyword;
                    b.text = t.text;
                    b.keyword = Keyword::null_kw;
                    b.symbol = '\0';
                    tokens.push_back(a);
                    tokens.push_back(b);
                    continue;
                }
            }
            tokens.push_back(t);
            continue;
        }
        if (c >= '0' && c <= '9') {
            tokens.push_back(read_number());
            continue;
        }
        if (c == '\'') {
            throw std::runtime_error("Single-quoted strings are not allowed, use double quotes");
        }
        if (c == '"') {
            tokens.push_back(read_string());
            continue;
        }
        if (is_symbol(c)) {
            tokens.push_back(read_symbol());
            continue;
        }
        _pos += 1;
    }
}

void Lexer::skip_whitespace()
{
    while (_pos < _input.size() && is_whitespace(_input[_pos])) {
        _pos += 1;
    }
}

bool Lexer::is_ident_start(char c) const
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool Lexer::is_ident_char(char c) const
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

Token Lexer::read_identifier_or_keyword()
{
    std::size_t start = _pos;
    _pos += 1;
    while (_pos < _input.size() && is_ident_char(_input[_pos])) {
        _pos += 1;
    }
    std::string text(_input.substr(start, _pos - start));
    std::string lower = text;
    bool has_lower = false;
    bool has_upper = false;
    for (char& ch : lower) {
        has_lower = has_lower || (ch >= 'a' && ch <= 'z');
        has_upper = has_upper || (ch >= 'A' && ch <= 'Z');
        ch = to_lower(ch);
    }
    Keyword kw{};
    if (match_keyword(lower, kw)) {
        if (has_lower && has_upper) {
            throw std::runtime_error("Mixed-case keywords are not allowed");
        }
        Token t{};
        t.type = TokenType::keyword;
        t.text = text;
        t.keyword = kw;
        t.symbol = '\0';
        return t;
    }
    Token t{};
    t.type = TokenType::identifier;
    t.text = text;
    t.keyword = Keyword::create_kw;
    t.symbol = '\0';
    return t;
}

Token Lexer::read_number()
{
    std::size_t start = _pos;
    while (_pos < _input.size() && _input[_pos] >= '0' && _input[_pos] <= '9') {
        _pos += 1;
    }
    Token t{};
    t.type = TokenType::number;
    t.text = std::string(_input.substr(start, _pos - start));
    t.keyword = Keyword::create_kw;
    t.symbol = '\0';
    return t;
}

Token Lexer::read_string()
{
    if (_input[_pos] != '"') {
        throw std::runtime_error("Only double-quoted string literals are supported");
    }
    _pos += 1;
    std::size_t start = _pos;
    while (_pos < _input.size() && _input[_pos] != '"') {
        _pos += 1;
    }
    if (_pos >= _input.size()) {
        throw std::runtime_error("Unterminated string literal");
    }
    std::string text(_input.substr(start, _pos - start));
    _pos += 1; // closing "
    Token t{};
    t.type = TokenType::string_literal;
    t.text = text;
    t.keyword = Keyword::create_kw;
    t.symbol = '\0';
    return t;
}


Token Lexer::read_symbol()
{
    char c = _input[_pos];
    char next = '\0';
    if (_pos + 1 < _input.size()) {
        next = _input[_pos + 1];
    }
    Token t{};
    t.type = TokenType::symbol;
    t.keyword = Keyword::create_kw;
    t.symbol = c;
    if ((c == '=' && next == '=') ||
        (c == '!' && next == '=') ||
        (c == '<' && next == '=') ||
        (c == '>' && next == '=')) {
        t.text = std::string{c, next};
        _pos += 2;
        return t;
    }
    t.text = std::string(1, c);
    _pos += 1;
    return t;
}

bool Lexer::is_symbol(char c)
{
    return c == '(' || c == ')' || c == ',' || c == ';' || c == '-' ||
           c == '*' || c == '=' || c == '!' || c == '<' || c == '>' || c == '.';
}

bool Lexer::is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char Lexer::to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool Lexer::match_keyword(const std::string& text, Keyword& out_kw)
{
    if (text == "create") {
        out_kw = Keyword::create_kw;
        return true;
    }
    if (text == "drop") {
        out_kw = Keyword::drop_kw;
        return true;
    }
    if (text == "use") {
        out_kw = Keyword::use_kw;
        return true;
    }
    if (text == "insert") {
        out_kw = Keyword::insert_kw;
        return true;
    }
    if (text == "into") {
        out_kw = Keyword::into_kw;
        return true;
    }
    if (text == "select") {
        out_kw = Keyword::select_kw;
        return true;
    }
    if (text == "update") {
        out_kw = Keyword::update_kw;
        return true;
    }
    if (text == "set") {
        out_kw = Keyword::set_kw;
        return true;
    }
    if (text == "delete") {
        out_kw = Keyword::delete_kw;
        return true;
    }
    if (text == "from") {
        out_kw = Keyword::from_kw;
        return true;
    }
    if (text == "where") {
        out_kw = Keyword::where_kw;
        return true;
    }
    if (text == "as") {
        out_kw = Keyword::as_kw;
        return true;
    }
    if (text == "between") {
        out_kw = Keyword::between_kw;
        return true;
    }
    if (text == "and") {
        out_kw = Keyword::and_kw;
        return true;
    }
    if (text == "like") {
        out_kw = Keyword::like_kw;
        return true;
    }
    if (text == "value") {
        out_kw = Keyword::value_kw;
        return true;
    }
    if (text == "values") {
        out_kw = Keyword::values_kw;
        return true;
    }
    if (text == "table") {
        out_kw = Keyword::table_kw;
        return true;
    }
    if (text == "database") {
        out_kw = Keyword::database_kw;
        return true;
    }
    if (text == "int") {
        out_kw = Keyword::int_kw;
        return true;
    }
    if (text == "string") {
        out_kw = Keyword::string_kw;
        return true;
    }
    if (text == "bool") {
        out_kw = Keyword::bool_kw;
        return true;
    }
    if (text == "not") {
        out_kw = Keyword::not_kw;
        return true;
    }
    if (text == "null") {
        out_kw = Keyword::null_kw;
        return true;
    }
    if (text == "indexed") {
        out_kw = Keyword::indexed_kw;
        return true;
    }
    if (text == "default") {
        out_kw = Keyword::default_kw;
        return true;
    }
    if (text == "or") {
        out_kw = Keyword::or_kw;
        return true;
    }
    if (text == "sum") {
        out_kw = Keyword::sum_kw;
        return true;
    }
    if (text == "count") {
        out_kw = Keyword::count_kw;
        return true;
    }
    if (text == "avg") {
        out_kw = Keyword::avg_kw;
        return true;
    }
    return false;
}

}
