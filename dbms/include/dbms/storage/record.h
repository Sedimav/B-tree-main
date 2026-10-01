#ifndef COURSEWORK_DBMS_RECORD_H
#define COURSEWORK_DBMS_RECORD_H

#include <cstddef>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace dbms
{

struct Rid
{
    int page_id;
    int slot_id;
    static constexpr std::size_t kSize = sizeof(int) * 2;
    bool operator==(const Rid& other) const noexcept { return page_id == other.page_id && slot_id == other.slot_id; }
    bool operator!=(const Rid& other) const noexcept { return !(*this == other); }
    std::array<unsigned char, kSize> to_bytes() const noexcept
    {
        std::array<unsigned char, kSize> out{};
        std::memcpy(out.data(), &page_id, sizeof(page_id));
        std::memcpy(out.data() + sizeof(page_id), &slot_id, sizeof(slot_id));
        return out;
    }
    static Rid from_bytes(const unsigned char* data) noexcept
    {
        Rid r{};
        std::memcpy(&r.page_id, data, sizeof(r.page_id));
        std::memcpy(&r.slot_id, data + sizeof(r.page_id), sizeof(r.slot_id));
        return r;
    }
};

class Record
{
public:
    using value_type = std::optional<std::string>;
    explicit Record(std::vector<value_type> values = {}) : _values(std::move(values)) {}
    explicit Record(std::vector<std::string> values)
    {
        _values.reserve(values.size());
        for (auto& value : values) {
            _values.emplace_back(std::move(value));
        }
    }

    const std::vector<value_type>& values() const { return _values; }

private:
    std::vector<value_type> _values;
};

}

#endif
