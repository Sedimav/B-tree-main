#ifndef COURSEWORK_DBMS_SQL_TOKEN_H
#define COURSEWORK_DBMS_SQL_TOKEN_H

#include <string>

namespace dbms
{

enum class TokenType
{
    eof_token,
    identifier,
    number,
    string_literal,
    keyword,
    symbol
};

enum class Keyword
{
    create_kw,
    drop_kw,
    use_kw,
    insert_kw,
    into_kw,
    select_kw,
    from_kw,
    where_kw,
    update_kw,
    set_kw,
    delete_kw,                                                  
    as_kw,
    between_kw,
    and_kw,
    like_kw,
    value_kw,
    values_kw,
    table_kw,
    database_kw,
    int_kw,
    string_kw,
    bool_kw,
    not_kw,
    null_kw,
    indexed_kw,
    default_kw,
    or_kw,
    sum_kw,
    count_kw,
    avg_kw
};

struct Token
{
    TokenType type;
    std::string text;
    Keyword keyword;
    char symbol;
};

}

#endif
