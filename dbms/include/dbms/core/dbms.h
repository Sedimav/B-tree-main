#ifndef COURSEWORK_DBMS_DBMS_H
#define COURSEWORK_DBMS_DBMS_H

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include "catalog.h"

namespace dbms
{

class Dbms
{
public:
    explicit Dbms(std::filesystem::path data_root = default_data_root()) : _catalog(), _data_root(std::move(data_root))
    {
        std::error_code ec;
        std::filesystem::create_directories(_data_root, ec);
        load_catalog_from_disk();
    }

    Catalog& catalog() { return _catalog; }
    const Catalog& catalog() const { return _catalog; }
    const std::filesystem::path& data_root() const { return _data_root; }

    static std::filesystem::path default_data_root()
    {
        const char* env = std::getenv("DBMS_DATA_ROOT");
        if (env != nullptr && env[0] != '\0') {
            return std::filesystem::path(env);
        }
        return std::filesystem::temp_directory_path() / "coursework_dbms_data";
    }

private:
    void load_catalog_from_disk()
    {
        std::error_code ec;
        if (!std::filesystem::exists(_data_root, ec)) {
            return;
        }

        for (const auto& db_entry : std::filesystem::directory_iterator(_data_root, ec)) {
            if (ec || !db_entry.is_directory()) {
                continue;
            }

            Database db(db_entry.path().filename().string());
            for (const auto& table_entry : std::filesystem::directory_iterator(db_entry.path(), ec)) {
                if (ec || !table_entry.is_regular_file() || table_entry.path().extension() != ".tbl") {
                    continue;
                }

                Table table = Table::open(table_entry.path().string(), table_entry.path().stem().string());
                if (!table.columns().empty()) {
                    db.tables().push_back(std::move(table));
                }
            }
            _catalog.databases().push_back(std::move(db));
        }
    }

    Catalog _catalog;
    std::filesystem::path _data_root;
};

}

#endif
