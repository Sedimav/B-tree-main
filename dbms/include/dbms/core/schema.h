#ifndef COURSEWORK_DBMS_SCHEMA_H
#define COURSEWORK_DBMS_SCHEMA_H

#include <optional>
#include <string>

namespace dbms
{

struct Column
{
    std::string name;
    std::string type;
    bool not_null;
    bool indexed;
    std::optional<std::string> default_value;
};

}

#endif
