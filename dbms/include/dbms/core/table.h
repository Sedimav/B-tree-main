#ifndef COURSEWORK_DBMS_TABLE_H
#define COURSEWORK_DBMS_TABLE_H

#include <string>
#include <vector>
#include "schema.h"
#include "../storage/record.h"
#include "../storage/table_page_manager.h"

namespace dbms
{

class Table
{
public:
    explicit Table(std::string name = {}, std::vector<Column> columns = {}) : _name(std::move(name)), _columns(std::move(columns)) {}

    static Table open(const std::string& path, std::string name = {})
    {
        Table table(std::move(name));
        table.load_schema(path);
        return table;
    }

    static Table create(const std::string& path, std::string name, std::vector<Column> columns)
    {
        Table table(std::move(name), std::move(columns));
        table.save_schema(path);
        return table;
    }

    const std::string& name() const { return _name; }
    const std::vector<Column>& columns() const { return _columns; }

    bool load_schema(const std::string& path)
    {
        TablePageManager manager(path);
        return manager.read_schema(_columns);
    }

    bool save_schema(const std::string& path) const
    {
        TablePageManager manager(path);
        return manager.write_schema(_columns);
    }

private:
    std::string _name;
    std::vector<Column> _columns;
};

}

#endif
