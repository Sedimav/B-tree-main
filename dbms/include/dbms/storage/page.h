#ifndef COURSEWORK_DBMS_PAGE_H
#define COURSEWORK_DBMS_PAGE_H

#include <array>
#include <cstring>
#include <vector>
#include "storage_format.h"

namespace dbms
{

class Page
{
public:
    Page()
    {
        _data.fill(0);
        PageHeader header{};
        header.slots_count = 0;
        header.free_start = static_cast<int>(sizeof(PageHeader));
        header.free_end = static_cast<int>(kPageSize);
        write_header(header);
    }

    const std::array<unsigned char, kPageSize>& data() const { return _data; }
    std::array<unsigned char, kPageSize>& data() { return _data; }

    PageHeader read_header() const
    {
        PageHeader h{};
        std::memcpy(&h, _data.data(), sizeof(PageHeader));
        return h;
    }

    void write_header(const PageHeader& h)
    {
        std::memcpy(_data.data(), &h, sizeof(PageHeader));
    }

    Slot read_slot(int index) const
    {
        Slot s{};
        std::size_t offset = sizeof(PageHeader) + static_cast<std::size_t>(index) * sizeof(Slot);
        std::memcpy(&s, _data.data() + offset, sizeof(Slot));
        return s;
    }

    void write_slot(int index, const Slot& s)
    {
        std::size_t offset = sizeof(PageHeader) + static_cast<std::size_t>(index) * sizeof(Slot);
        std::memcpy(_data.data() + offset, &s, sizeof(Slot));
    }

    int free_space() const
    {
        PageHeader h = read_header();
        int slot_dir_end = static_cast<int>(sizeof(PageHeader) + h.slots_count * sizeof(Slot));
        return h.free_end - slot_dir_end;
    }

    int append_record(const unsigned char* record_bytes, int record_size)
    {
        PageHeader h = read_header();
        int new_slot_end = static_cast<int>(sizeof(PageHeader) + (h.slots_count + 1) * sizeof(Slot));
        if (h.free_end - new_slot_end < record_size) {
            return -1;
        }
        h.free_end = h.free_end - record_size;
        std::memcpy(_data.data() + h.free_end, record_bytes, static_cast<std::size_t>(record_size));
        Slot s{};
        s.offset = h.free_end;
        s.size = record_size;
        write_slot(h.slots_count, s);
        h.slots_count += 1;
        write_header(h);
        return h.slots_count - 1;
    }

    std::vector<unsigned char> read_record(int slot_id) const
    {
        PageHeader h = read_header();
        if (slot_id < 0 || slot_id >= h.slots_count) {
            return {};
        }
        Slot s = read_slot(slot_id);
        if (s.size <= 0) {
            return {};
        }
        std::vector<unsigned char> out(static_cast<std::size_t>(s.size));
        std::memcpy(out.data(), _data.data() + s.offset, static_cast<std::size_t>(s.size));
        return out;
    }

    bool remove_record(int slot_id)
    {
        PageHeader h = read_header();
        if (slot_id < 0 || slot_id >= h.slots_count) {
            return false;
        }
        Slot s = read_slot(slot_id);
        if (s.size <= 0) {
            return false;
        }
        s.size = 0;
        s.offset = 0;
        write_slot(slot_id, s);
        return true;
    }

private:
    std::array<unsigned char, kPageSize> _data{};
};

}

#endif
