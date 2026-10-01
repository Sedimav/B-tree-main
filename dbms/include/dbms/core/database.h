#ifndef COURSEWORK_DBMS_DATABASE_H
#define COURSEWORK_DBMS_DATABASE_H

#include <string>
#include <vector>
#include "table.h"

namespace dbms
{

class Database
{
public:
    explicit Database(std::string name = {}) : _name(std::move(name)) {}

    const std::string& name() const { return _name; }
    const std::vector<Table>& tables() const { return _tables; }
    std::vector<Table>& tables() { return _tables; }

private:
    std::string _name;
    std::vector<Table> _tables;
};

}

#endif
