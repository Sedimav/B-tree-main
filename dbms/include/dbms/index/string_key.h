#ifndef COURSEWORK_DBMS_STRING_KEY_H
#define COURSEWORK_DBMS_STRING_KEY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace dbms
{

template<std::size_t tsize = 240>
struct FixedStringKey
{
    std::array<char, tsize> bytes{};
    std::uint16_t length = 0;

    static bool from_string(std::string_view value, FixedStringKey& out)
    {
        if (value.size() > tsize) {
            return false;
        }
        out.bytes.fill('\0');
        for (std::size_t i = 0; i < value.size(); ++i) {
            out.bytes[i] = value[i];
        }
        out.length = static_cast<std::uint16_t>(value.size());
        return true;
    }

    friend bool operator<(const FixedStringKey& lhs, const FixedStringKey& rhs)
    {
        const std::size_t min_len = lhs.length < rhs.length ? lhs.length : rhs.length;
        for (std::size_t i = 0; i < min_len; ++i) {
            if (lhs.bytes[i] < rhs.bytes[i]) {
                return true;
            }
            if (lhs.bytes[i] > rhs.bytes[i]) {
                return false;
            }
        }
        return lhs.length < rhs.length;
    }

    friend bool operator>(const FixedStringKey& lhs, const FixedStringKey& rhs)
    {
        return rhs < lhs;
    }

    friend bool operator==(const FixedStringKey& lhs, const FixedStringKey& rhs)
    {
        if (lhs.length != rhs.length) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.length; ++i) {
            if (lhs.bytes[i] != rhs.bytes[i]) {
                return false;
            }
        }
        return true;
    }
};

static_assert(std::is_trivially_copyable_v<FixedStringKey<>>);

}

#endif
