#ifndef COURSEWORK_DBMS_CATALOG_H
#define COURSEWORK_DBMS_CATALOG_H

#include <string>
#include <vector>
#include "database.h"

namespace dbms
{

class Catalog
{
public:
    Catalog() = default;

    const std::vector<Database>& databases() const { return _databases; }
    std::vector<Database>& databases() { return _databases; }

private:
    std::vector<Database> _databases;
};

}

#endif
