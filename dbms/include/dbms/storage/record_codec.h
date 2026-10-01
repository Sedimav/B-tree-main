#ifndef COURSEWORK_DBMS_RECORD_CODEC_H
#define COURSEWORK_DBMS_RECORD_CODEC_H

#include <vector>
#include <string>
#include <cstring>
#include "record.h"
#include "storage_format.h"

namespace dbms
{

inline void write_int(std::vector<unsigned char>& out, int value)
{
    unsigned char bytes[sizeof(int)];
    std::memcpy(bytes, &value, sizeof(int));
    out.insert(out.end(), bytes, bytes + sizeof(int));
}

inline int read_int(const unsigned char* data)
{
    int value = 0;
    std::memcpy(&value, data, sizeof(int));
    return value;
}

inline std::vector<unsigned char> serialize_record(const Record& record, std::size_t columns)
{
    std::vector<unsigned char> out;
    int null_bytes = null_bitmap_bytes(columns);
    RecordHeader header{};
    header.null_bytes = null_bytes;
    header.size = 0;

    out.resize(sizeof(RecordHeader) + static_cast<std::size_t>(null_bytes), 0);

    const auto& values = record.values();
    for (std::size_t i = 0; i < columns; ++i) {
        bool is_null = (i >= values.size()) || (!values[i].has_value());
        if (is_null) {
            std::size_t byte_index = i / 8;
            std::size_t bit_index = i % 8;
            out[sizeof(RecordHeader) + byte_index] |= static_cast<unsigned char>(1u << bit_index);
            continue;
        }
        const std::string& v = values[i].value();
        int len = static_cast<int>(v.size());
        write_int(out, len);
        out.insert(out.end(), v.begin(), v.end());
    }

    header.size = static_cast<int>(out.size());
    std::memcpy(out.data(), &header, sizeof(RecordHeader));
    return out;
}

inline Record deserialize_record(const unsigned char* data, std::size_t columns)
{
    RecordHeader header{};
    std::memcpy(&header, data, sizeof(RecordHeader));
    const unsigned char* null_map = data + sizeof(RecordHeader);
    const unsigned char* cursor = null_map + header.null_bytes;

    std::vector<Record::value_type> values;
    values.reserve(columns);

    for (std::size_t i = 0; i < columns; ++i) {
        std::size_t byte_index = i / 8;
        std::size_t bit_index = i % 8;
        bool is_null = (null_map[byte_index] & static_cast<unsigned char>(1u << bit_index)) != 0;
        if (is_null) {
            values.emplace_back(std::nullopt);
            continue;
        }
        int len = read_int(cursor);
        cursor += sizeof(int);
        std::string v(reinterpret_cast<const char*>(cursor), static_cast<std::size_t>(len));
        cursor += len;
        values.emplace_back(std::move(v));
    }

    return Record(std::move(values));
}

}

#endif
