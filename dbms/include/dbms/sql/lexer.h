#ifndef COURSEWORK_DBMS_SQL_LEXER_H
#define COURSEWORK_DBMS_SQL_LEXER_H

#include <string>
#include <string_view>
#include <vector>
#include "token.h"

namespace dbms
{

class Lexer
{
public:
    explicit Lexer(std::string_view input) : _input(input), _pos(0) {}

    std::vector<Token> tokenize();

private:
    std::string_view _input;
    std::size_t _pos;

    void skip_whitespace();
    bool is_ident_start(char c) const;
    bool is_ident_char(char c) const;

    Token read_identifier_or_keyword();
    Token read_number();
    Token read_string();
    Token read_symbol();

    static bool is_symbol(char c);
    static bool is_whitespace(char c);
    static char to_lower(char c);
    static bool match_keyword(const std::string& text, Keyword& out_kw);
};

}

#endif
