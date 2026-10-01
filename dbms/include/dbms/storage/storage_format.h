#ifndef COURSEWORK_DBMS_STORAGE_FORMAT_H
#define COURSEWORK_DBMS_STORAGE_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace dbms
{

static constexpr std::size_t kPageSize = 4096;

struct PageHeader
{
    int slots_count;
    int free_start;
    int free_end;
};

struct Slot
{
    int offset;
    int size;
};

struct RecordHeader
{
    int size;
    int null_bytes;
};

inline int null_bitmap_bytes(std::size_t columns)
{
    return static_cast<int>((columns + 7) / 8);
}

}

#endif
