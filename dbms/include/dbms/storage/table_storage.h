#ifndef COURSEWORK_DBMS_TABLE_STORAGE_H
#define COURSEWORK_DBMS_TABLE_STORAGE_H

#include <string>
#include <fstream>
#include "page.h"

namespace dbms
{

class TableStorage
{
public:
    explicit TableStorage(std::string path = {}) : _path(std::move(path)) {}

    const std::string& path() const { return _path; }

    bool write_page(int page_id, const Page& page)
    {
        std::fstream file(_path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            file.open(_path, std::ios::out | std::ios::binary);
            file.close();
            file.open(_path, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (!file.is_open()) {
            return false;
        }
        std::streamoff offset = static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(kPageSize);
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(page.data().data()), static_cast<std::streamsize>(kPageSize));
        file.flush();
        return file.good();
    }

    bool read_page(int page_id, Page& page) const
    {
        std::ifstream file(_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        std::streamoff offset = static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(kPageSize);
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(page.data().data()), static_cast<std::streamsize>(kPageSize));
        return file.good();
    }

private:
    std::string _path;
};

}

#endif
