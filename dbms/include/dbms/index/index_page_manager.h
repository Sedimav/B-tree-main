#ifndef COURSEWORK_DBMS_INDEX_PAGE_MANAGER_H
#define COURSEWORK_DBMS_INDEX_PAGE_MANAGER_H

#include <array>
#include <cstring>
#include <type_traits>
#include "../storage/page.h"
#include "../storage/table_storage.h"

namespace dbms
{

struct IndexHeader
{
    int root_id;
    int order;
    int next_page_id;
};

class IndexPageManager
{
public:
    explicit IndexPageManager(std::string path = {}) : _storage(std::move(path)) {}

    const std::string& path() const { return _storage.path(); }

    bool read_header(IndexHeader& header) const
    {
        Page page;
        if (!_storage.read_page(0, page)) {
            return false;
        }
        std::memcpy(&header, page.data().data(), sizeof(IndexHeader));
        return true;
    }

    bool write_header(const IndexHeader& header)
    {
        Page page;
        std::memcpy(page.data().data(), &header, sizeof(IndexHeader));
        return _storage.write_page(0, page);
    }

    int allocate_page()
    {
        IndexHeader header{};
        if (!read_header(header)) {
            header.root_id = -1;
            header.order = 0;
            header.next_page_id = 1;
        }
        int id = header.next_page_id;
        header.next_page_id += 1;
        if (!write_header(header)) {
            return -1;
        }
        return id;
    }

    template<typename Node>
    bool write_node(int page_id, const Node& node)
    {
        static_assert(std::is_trivially_copyable_v<Node>);
        Page page;
        std::memcpy(page.data().data(), &node, sizeof(Node));
        return _storage.write_page(page_id, page);
    }

    template<typename Node>
    bool read_node(int page_id, Node& node) const
    {
        static_assert(std::is_trivially_copyable_v<Node>);
        Page page;
        if (!_storage.read_page(page_id, page)) {
            return false;
        }
        std::memcpy(&node, page.data().data(), sizeof(Node));
        return true;
    }

private:
    TableStorage _storage;
};

}

#endif
