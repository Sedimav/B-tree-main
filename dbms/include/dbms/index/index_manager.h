#ifndef COURSEWORK_DBMS_INDEX_MANAGER_H
#define COURSEWORK_DBMS_INDEX_MANAGER_H

#include <string>
#include <vector>
#include "b_tree_disk_index.h"

namespace dbms
{

template<typename tkey>
class IndexManager
{
public:
    explicit IndexManager(std::string path = {}) : _path(std::move(path)), _index(_path) {}

    const std::string& path() const { return _path; }

    bool insert(const tkey& key, const Rid& rid)
    {
        return _index.insert(key, rid);
    }

    bool erase(const tkey& key)
    {
        return _index.erase(key);
    }

    bool find(const tkey& key, Rid& out_rid) const
    {
        return _index.find(key, out_rid);
    }

    std::vector<std::pair<tkey, Rid>> range(const tkey& low, const tkey& high) const
    {
        return _index.range(low, high);
    }

private:
    std::string _path;
    BTreeDiskIndex<tkey> _index;
};

}

#endif
