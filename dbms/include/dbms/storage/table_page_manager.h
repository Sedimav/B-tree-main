#ifndef COURSEWORK_DBMS_TABLE_PAGE_MANAGER_H
#define COURSEWORK_DBMS_TABLE_PAGE_MANAGER_H

#include <filesystem>
#include <vector>
#include <cstring>
#include <string>
#include "page.h"
#include "table_storage.h"
#include "../core/schema.h"

namespace dbms
{

struct TableHeader
{
    int columns_count;
    int schema_bytes;
    int next_page_id;
};

class TablePageManager
{
public:
    explicit TablePageManager(std::string path = {}) : _storage(std::move(path))
    {
        ensure_header();
    }

    const std::string& path() const { return _storage.path(); }

    int allocate_page()
    {
        TableHeader header{};
        if (!read_header(header)) {
            header.next_page_id = 1;
        }
        int id = header.next_page_id;
        header.next_page_id += 1;
        write_header(header);
        Page blank;
        _storage.write_page(id, blank);
        return id;
    }

    bool write_schema(const std::vector<Column>& columns)
    {
        std::vector<unsigned char> schema = serialize_columns(columns);
        if (schema.size() + sizeof(TableHeader) > kPageSize) {
            return false;
        }
        TableHeader header{};
        TableHeader current{};
        if (read_header(current)) {
            header.next_page_id = current.next_page_id;
        } else {
            header.next_page_id = 1;
        }
        header.columns_count = static_cast<int>(columns.size());
        header.schema_bytes = static_cast<int>(schema.size());
        Page page;
        std::memcpy(page.data().data(), &header, sizeof(TableHeader));
        if (!schema.empty()) {
            std::memcpy(page.data().data() + sizeof(TableHeader), schema.data(), schema.size());
        }
        return _storage.write_page(0, page);
    }

    bool read_schema(std::vector<Column>& columns) const
    {
        TableHeader header{};
        if (!read_header(header)) {
            return false;
        }
        if (header.schema_bytes <= 0 || header.columns_count <= 0) {
            columns.clear();
            return true;
        }
        if (static_cast<std::size_t>(header.schema_bytes) + sizeof(TableHeader) > kPageSize) {
            return false;
        }
        Page page;
        if (!_storage.read_page(0, page)) {
            return false;
        }
        const unsigned char* data = page.data().data() + sizeof(TableHeader);
        columns = deserialize_columns(data, header.schema_bytes);
        return static_cast<int>(columns.size()) == header.columns_count;
    }

    bool read_page(int page_id, Page& page) const
    {
        return _storage.read_page(page_id, page);
    }

    bool write_page(int page_id, const Page& page)
    {
        return _storage.write_page(page_id, page);
    }

private:
    void ensure_header()
    {
        if (_storage.path().empty()) {
            return;
        }
        std::error_code ec;
        std::uintmax_t size = 0;
        if (std::filesystem::exists(_storage.path(), ec)) {
            size = std::filesystem::file_size(_storage.path(), ec);
        }
        if (size >= kPageSize) {
            return;
        }
        TableHeader header{};
        header.columns_count = 0;
        header.schema_bytes = 0;
        header.next_page_id = 1;
        write_header(header);
    }

    bool read_header(TableHeader& header) const
    {
        Page page;
        if (!_storage.read_page(0, page)) {
            return false;
        }
        std::memcpy(&header, page.data().data(), sizeof(TableHeader));
        return true;
    }

    void write_header(const TableHeader& header)
    {
        Page page;
        (void)_storage.read_page(0, page);
        std::memcpy(page.data().data(), &header, sizeof(TableHeader));
        _storage.write_page(0, page);
    }

    static void write_int(std::vector<unsigned char>& out, int value)
    {
        unsigned char bytes[sizeof(int)];
        std::memcpy(bytes, &value, sizeof(int));
        out.insert(out.end(), bytes, bytes + sizeof(int));
    }

    static int read_int(const unsigned char* data)
    {
        int value = 0;
        std::memcpy(&value, data, sizeof(int));
        return value;
    }

    static std::vector<unsigned char> serialize_columns(const std::vector<Column>& columns)
    {
        std::vector<unsigned char> out;
        for (const auto& col : columns) {
            write_int(out, static_cast<int>(col.name.size()));
            out.insert(out.end(), col.name.begin(), col.name.end());
            write_int(out, static_cast<int>(col.type.size()));
            out.insert(out.end(), col.type.begin(), col.type.end());
            write_int(out, col.not_null ? 1 : 0);
            write_int(out, col.indexed ? 1 : 0);
            if (col.default_value.has_value()) {
                write_int(out, 1);
                write_int(out, static_cast<int>(col.default_value->size()));
                out.insert(out.end(), col.default_value->begin(), col.default_value->end());
            } else {
                write_int(out, 0);
            }
        }
        return out;
    }

    static std::vector<Column> deserialize_columns(const unsigned char* data, int size_bytes)
    {
        std::vector<Column> columns;
        const unsigned char* cursor = data;
        const unsigned char* end = data + size_bytes;
        while (cursor < end) {
            if (end - cursor < static_cast<int>(sizeof(int))) {
                break;
            }
            int name_len = read_int(cursor);
            cursor += sizeof(int);
            if (name_len < 0 || cursor + name_len > end) {
                break;
            }
            std::string name(reinterpret_cast<const char*>(cursor), static_cast<std::size_t>(name_len));
            cursor += name_len;
            if (cursor + sizeof(int) > end) {
                break;
            }
            int type_len = read_int(cursor);
            cursor += sizeof(int);
            if (type_len < 0 || cursor + type_len > end) {
                break;
            }
            std::string type(reinterpret_cast<const char*>(cursor), static_cast<std::size_t>(type_len));
            cursor += type_len;
            if (cursor + sizeof(int) * 2 > end) {
                break;
            }
            int not_null = read_int(cursor);
            cursor += sizeof(int);
            int indexed = read_int(cursor);
            cursor += sizeof(int);
            Column col{};
            col.name = std::move(name);
            col.type = std::move(type);
            col.not_null = (not_null != 0);
            col.indexed = (indexed != 0);
            if (cursor + sizeof(int) <= end) {
                int has_default = read_int(cursor);
                cursor += sizeof(int);
                if (has_default && cursor + sizeof(int) <= end) {
                    int def_len = read_int(cursor);
                    cursor += sizeof(int);
                    if (def_len >= 0 && cursor + def_len <= end) {
                        col.default_value = std::string(reinterpret_cast<const char*>(cursor), static_cast<std::size_t>(def_len));
                        cursor += def_len;
                    }
                }
            }
            columns.push_back(std::move(col));
        }
        return columns;
    }

    TableStorage _storage;
};

}

#endif
