#include "dbms/sql/executor.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <sstream>
#include <filesystem>
#include <limits>
#include <regex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "dbms/index/index_manager.h"
#include "dbms/index/string_key.h"
#include "dbms/storage/record_codec.h"
#include "dbms/storage/table_page_manager.h"


namespace dbms
{





namespace
{

constexpr std::size_t kStringIndexKeySize = 240;
using StringIndexKey = FixedStringKey<kStringIndexKeySize>;

template<typename T>
bool compare_with_op(const T& lhs, const T& rhs, ComparisonOp op)
{
    switch (op) {
        case ComparisonOp::eq: return lhs == rhs;
        case ComparisonOp::ne: return !(lhs == rhs);
        case ComparisonOp::lt: return lhs < rhs;
        case ComparisonOp::gt: return rhs < lhs;
        case ComparisonOp::le: return !(rhs < lhs);
        case ComparisonOp::ge: return !(lhs < rhs);
    }
    return false;
}

bool parse_int_local(const std::string& text, int& out)
{
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    int value = 0;
    auto r = std::from_chars(begin, end, value);
    if (r.ec != std::errc{} || r.ptr != end) return false;
    out = value;
    return true;
}

std::string to_lower_local(std::string s)
{
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

bool parse_bool_local(const std::string& text, bool& out)
{
    const std::string lower = to_lower_local(text);
    if (lower == "true" || lower == "1") { out = true; return true; }
    if (lower == "false" || lower == "0") { out = false; return true; }
    return false;
}

struct ResolvedOperand
{
    std::optional<std::string> value;
    std::string type;
    bool has_type = false;
};

struct IndexWherePlan
{
    std::size_t column_idx = 0;
    ComparisonOp op = ComparisonOp::eq;
    std::string literal;
};

struct IndexBetweenPlan
{
    std::size_t column_idx = 0;
    std::string low_literal;
    std::string high_literal;
};

ComparisonOp reverse_comparison_op(ComparisonOp op)
{
    switch (op) {
        case ComparisonOp::eq: return ComparisonOp::eq;
        case ComparisonOp::ne: return ComparisonOp::ne;
        case ComparisonOp::lt: return ComparisonOp::gt;
        case ComparisonOp::gt: return ComparisonOp::lt;
        case ComparisonOp::le: return ComparisonOp::ge;
        case ComparisonOp::ge: return ComparisonOp::le;
    }
    return ComparisonOp::eq;
}

bool extract_column_literal_pattern(const WhereComparison& where,
                                    std::string& out_column,
                                    ComparisonOp& out_op,
                                    std::string& out_literal)
{
    const auto* lhs_col = std::get_if<ColumnRef>(&where.lhs);
    const auto* rhs_col = std::get_if<ColumnRef>(&where.rhs);
    const auto* lhs_lit = std::get_if<Literal>(&where.lhs);
    const auto* rhs_lit = std::get_if<Literal>(&where.rhs);

    if (lhs_col != nullptr && rhs_lit != nullptr && !rhs_lit->is_null) {
        out_column = lhs_col->name;
        out_op = where.op;
        out_literal = rhs_lit->text;
        return true;
    }

    if (rhs_col != nullptr && lhs_lit != nullptr && !lhs_lit->is_null) {
        out_column = rhs_col->name;
        out_op = reverse_comparison_op(where.op);
        out_literal = lhs_lit->text;
        return true;
    }

    return false;
}

bool try_build_index_where_plan(const WhereComparison& where,
                                const std::vector<Column>& columns,
                                const std::unordered_map<std::string, std::size_t>& col_pos,
                                IndexWherePlan& out_plan)
{
    std::string column_name;
    ComparisonOp op = ComparisonOp::eq;
    std::string literal;
    if (!extract_column_literal_pattern(where, column_name, op, literal)) {
        return false;
    }

    const auto it = col_pos.find(column_name);
    if (it == col_pos.end()) {
        return false;
    }
    const std::size_t column_idx = it->second;
    if (column_idx >= columns.size()) {
        return false;
    }
    if (!columns[column_idx].indexed) {
        return false;
    }

    out_plan.column_idx = column_idx;
    out_plan.op = op;
    out_plan.literal = std::move(literal);
    return true;
}

bool try_build_index_between_plan(const WhereBetween& where,
                                  const std::vector<Column>& columns,
                                  const std::unordered_map<std::string, std::size_t>& col_pos,
                                  IndexBetweenPlan& out_plan)
{
    const auto* value_col = std::get_if<ColumnRef>(&where.value);
    const auto* low_lit = std::get_if<Literal>(&where.low);
    const auto* high_lit = std::get_if<Literal>(&where.high);
    if (value_col == nullptr || low_lit == nullptr || high_lit == nullptr) {
        return false;
    }
    if (low_lit->is_null || high_lit->is_null) {
        return false;
    }

    const auto it = col_pos.find(value_col->name);
    if (it == col_pos.end()) {
        return false;
    }
    const std::size_t column_idx = it->second;
    if (column_idx >= columns.size() || !columns[column_idx].indexed) {
        return false;
    }

    out_plan.column_idx = column_idx;
    out_plan.low_literal = low_lit->text;
    out_plan.high_literal = high_lit->text;
    return true;
}

bool read_row_by_rid(const TablePageManager& manager,
                     const Rid& rid,
                     std::size_t columns_count,
                     std::vector<std::optional<std::string>>& out_row)
{
    if (rid.page_id < 0 || rid.slot_id < 0) {
        return false;
    }
    Page page;
    if (!manager.read_page(rid.page_id, page)) {
        return false;
    }
    const auto raw = page.read_record(rid.slot_id);
    if (raw.empty()) {
        return false;
    }
    const Record rec = deserialize_record(raw.data(), columns_count);
    out_row = rec.values();
    return true;
}

void collect_int_index_rids(IndexManager<int>& index, int key, ComparisonOp op, std::vector<Rid>& out_rids)
{
    if (op == ComparisonOp::eq) {
        Rid rid{};
        if (index.find(key, rid)) {
            out_rids.push_back(rid);
        }
        return;
    }

    int low = std::numeric_limits<int>::min();
    int high = std::numeric_limits<int>::max();
    if (op == ComparisonOp::lt || op == ComparisonOp::le) {
        high = key;
    } else if (op == ComparisonOp::gt || op == ComparisonOp::ge) {
        low = key;
    }
    const auto pairs = index.range(low, high);
    for (const auto& entry : pairs) {
        if (compare_with_op(entry.first, key, op)) {
            out_rids.push_back(entry.second);
        }
    }
}

void collect_string_index_rids(IndexManager<StringIndexKey>& index,
                               const StringIndexKey& key,
                               ComparisonOp op,
                               std::vector<Rid>& out_rids)
{
    if (op == ComparisonOp::eq) {
        Rid rid{};
        if (index.find(key, rid)) {
            out_rids.push_back(rid);
        }
        return;
    }

    StringIndexKey low{};
    StringIndexKey high{};
    high.bytes.fill(static_cast<char>(0x7F));
    high.length = static_cast<decltype(high.length)>(kStringIndexKeySize);

    if (op == ComparisonOp::lt || op == ComparisonOp::le) {
        high = key;
    } else if (op == ComparisonOp::gt || op == ComparisonOp::ge) {
        low = key;
    }
    const auto pairs = index.range(low, high);
    for (const auto& entry : pairs) {
        if (compare_with_op(entry.first, key, op)) {
            out_rids.push_back(entry.second);
        }
    }
}

bool collect_rids_from_index(const std::filesystem::path& idx_path,
                             const Column& indexed_column,
                             ComparisonOp op,
                             const std::string& literal,
                             bool& out_used_index,
                             std::vector<Rid>& out_rids)
{
    out_used_index = false;
    out_rids.clear();

    if (op == ComparisonOp::ne) {
        return true; // fallback to full scan for !=
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(idx_path, ec)) {
        return false;
    }

    if (indexed_column.type == "INT") {
        int key = 0;
        if (!parse_int_local(literal, key)) {
            return true;
        }
        out_used_index = true;
        IndexManager<int> index(idx_path.string());
        collect_int_index_rids(index, key, op, out_rids);
        return true;
    }

    if (indexed_column.type == "BOOL") {
        bool parsed = false;
        if (!parse_bool_local(literal, parsed)) {
            return true;
        }
        out_used_index = true;
        IndexManager<int> index(idx_path.string());
        collect_int_index_rids(index, parsed ? 1 : 0, op, out_rids);
        return true;
    }

    if (indexed_column.type == "STRING") {
        StringIndexKey key{};
        if (!StringIndexKey::from_string(literal, key)) {
            return true;
        }
        out_used_index = true;
        IndexManager<StringIndexKey> index(idx_path.string());
        collect_string_index_rids(index, key, op, out_rids);
        return true;
    }

    return false;
}

bool collect_between_rids_from_index(const std::filesystem::path& idx_path,
                                     const Column& indexed_column,
                                     const std::string& low_literal,
                                     const std::string& high_literal,
                                     bool& out_used_index,
                                     std::vector<Rid>& out_rids)
{
    out_used_index = false;
    out_rids.clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(idx_path, ec)) {
        return false;
    }

    if (indexed_column.type == "INT") {
        int low = 0;
        int high = 0;
        if (!parse_int_local(low_literal, low) || !parse_int_local(high_literal, high)) {
            return true;
        }
        out_used_index = true;
        IndexManager<int> index(idx_path.string());
        const auto pairs = index.range(low, high);
        for (const auto& entry : pairs) {
            if (entry.first >= low && entry.first <= high) {
                out_rids.push_back(entry.second);
            }
        }
        return true;
    }

    if (indexed_column.type == "BOOL") {
        bool low_bool = false;
        bool high_bool = false;
        if (!parse_bool_local(low_literal, low_bool) || !parse_bool_local(high_literal, high_bool)) {
            return true;
        }
        const int low = low_bool ? 1 : 0;
        const int high = high_bool ? 1 : 0;
        out_used_index = true;
        IndexManager<int> index(idx_path.string());
        const auto pairs = index.range(low, high);
        for (const auto& entry : pairs) {
            if (entry.first >= low && entry.first <= high) {
                out_rids.push_back(entry.second);
            }
        }
        return true;
    }

    if (indexed_column.type == "STRING") {
        StringIndexKey low{};
        StringIndexKey high{};
        if (!StringIndexKey::from_string(low_literal, low) ||
            !StringIndexKey::from_string(high_literal, high)) {
            return true;
        }
        out_used_index = true;
        IndexManager<StringIndexKey> index(idx_path.string());
        const auto pairs = index.range(low, high);
        for (const auto& entry : pairs) {
            if (!(entry.first < low) && !(high < entry.first)) {
                out_rids.push_back(entry.second);
            }
        }
        return true;
    }

    return false;
}

bool resolve_operand_value(const Operand& operand,
                           const std::vector<std::optional<std::string>>& row,
                           const std::vector<Column>& columns,
                           const std::unordered_map<std::string, std::size_t>& col_pos,
                           ResolvedOperand& out)
{
    if (const auto* col = std::get_if<ColumnRef>(&operand)) {
        auto it = col_pos.find(col->name);
        if (it == col_pos.end()) return false;
        std::size_t idx = it->second;
        if (idx >= row.size() || idx >= columns.size()) return false;
        out.value = row[idx];
        out.type = columns[idx].type;
        out.has_type = true;
        return true;
    }

    const auto* lit = std::get_if<Literal>(&operand);
    if (lit == nullptr) return false;
    out.value = lit->is_null ? std::optional<std::string>{} : std::optional<std::string>{lit->text};
    out.type.clear();
    out.has_type = false;
    return true;
}

bool evaluate_where_comparison(const WhereComparison& where,
                               const std::vector<std::optional<std::string>>& row,
                               const std::vector<Column>& columns,
                               const std::unordered_map<std::string, std::size_t>& col_pos)
{
    ResolvedOperand lhs{};
    ResolvedOperand rhs{};
    if (!resolve_operand_value(where.lhs, row, columns, col_pos, lhs)) return false;
    if (!resolve_operand_value(where.rhs, row, columns, col_pos, rhs)) return false;

    if (!lhs.value.has_value() || !rhs.value.has_value()) return false; // NULL -> false

    std::string cmp_type = "STRING";
    if (lhs.has_type && rhs.has_type) {
        if (lhs.type != rhs.type) return false;
        cmp_type = lhs.type;
    } else if (lhs.has_type) {
        cmp_type = lhs.type;
    } else if (rhs.has_type) {
        cmp_type = rhs.type;
    }

    if (cmp_type == "INT") {
        int l = 0, r = 0;
        if (!parse_int_local(*lhs.value, l) || !parse_int_local(*rhs.value, r)) return false;
        return compare_with_op(l, r, where.op);
    }

    if (cmp_type == "BOOL") {
        bool l = false, r = false;
        if (!parse_bool_local(*lhs.value, l) || !parse_bool_local(*rhs.value, r)) return false;
        return compare_with_op(l, r, where.op);
    }

    return compare_with_op(*lhs.value, *rhs.value, where.op);
}

bool evaluate_where_between(const WhereBetween& where,
                            const std::vector<std::optional<std::string>>& row,
                            const std::vector<Column>& columns,
                            const std::unordered_map<std::string, std::size_t>& col_pos)
{
    const WhereComparison lower_bound{where.value, ComparisonOp::ge, where.low};
    const WhereComparison upper_bound{where.value, ComparisonOp::le, where.high};
    return evaluate_where_comparison(lower_bound, row, columns, col_pos) &&
           evaluate_where_comparison(upper_bound, row, columns, col_pos);
}

bool evaluate_where_like(const WhereLike& where,
                         const std::vector<std::optional<std::string>>& row,
                         const std::vector<Column>& columns,
                         const std::unordered_map<std::string, std::size_t>& col_pos)
{
    ResolvedOperand value{};
    ResolvedOperand pattern{};
    if (!resolve_operand_value(where.value, row, columns, col_pos, value)) return false;
    if (!resolve_operand_value(where.pattern, row, columns, col_pos, pattern)) return false;
    if (!value.value.has_value() || !pattern.value.has_value()) return false;
    if (value.has_type && value.type != "STRING") return false;

    try {
        return std::regex_match(*value.value, std::regex(*pattern.value));
    } catch (const std::regex_error&) {
        return false;
    }
}

bool evaluate_where_condition(const WhereCondition& where,
                              const std::vector<std::optional<std::string>>& row,
                              const std::vector<Column>& columns,
                              const std::unordered_map<std::string, std::size_t>& col_pos)
{
    if (const auto* cmp = std::get_if<WhereComparison>(&where)) {
        return evaluate_where_comparison(*cmp, row, columns, col_pos);
    }
    if (const auto* between = std::get_if<WhereBetween>(&where)) {
        return evaluate_where_between(*between, row, columns, col_pos);
    }
    if (const auto* like = std::get_if<WhereLike>(&where)) {
        return evaluate_where_like(*like, row, columns, col_pos);
    }
    if (const auto* and_node = std::get_if<std::unique_ptr<WhereAnd>>(&where)) {
        return evaluate_where_condition((*and_node)->left, row, columns, col_pos)
            && evaluate_where_condition((*and_node)->right, row, columns, col_pos);
    }
    if (const auto* or_node = std::get_if<std::unique_ptr<WhereOr>>(&where)) {
        return evaluate_where_condition((*or_node)->left, row, columns, col_pos)
            || evaluate_where_condition((*or_node)->right, row, columns, col_pos);
    }
    return false;
}

std::string operand_label(const Operand& operand)
{
    if (const auto* col = std::get_if<ColumnRef>(&operand)) {
        return "`" + col->name + "`";
    }
    if (const auto* lit = std::get_if<Literal>(&operand)) {
        return lit->is_null ? "NULL" : ("`" + lit->text + "`");
    }
    return "<operand>";
}

bool validate_operand_reference(const Operand& operand,
                                const std::unordered_map<std::string, std::size_t>& col_pos,
                                std::string& error)
{
    if (const auto* col = std::get_if<ColumnRef>(&operand)) {
        if (col_pos.find(col->name) == col_pos.end()) {
            error = "Semantic error: WHERE references unknown column `" + col->name + "`";
            return false;
        }
    }
    return true;
}

std::optional<std::string> operand_declared_type(const Operand& operand,
                                                 const std::vector<Column>& columns,
                                                 const std::unordered_map<std::string, std::size_t>& col_pos)
{
    if (const auto* col = std::get_if<ColumnRef>(&operand)) {
        const auto it = col_pos.find(col->name);
        if (it == col_pos.end() || it->second >= columns.size()) {
            return std::nullopt;
        }
        return columns[it->second].type;
    }
    return std::nullopt;
}

bool validate_literal_for_type(const Literal& literal, const std::string& type, std::string& error)
{
    if (literal.is_null) {
        return true;
    }
    if (type == "INT") {
        int parsed = 0;
        if (!parse_int_local(literal.text, parsed)) {
            error = "Type error: literal `" + literal.text + "` is not INT";
            return false;
        }
    } else if (type == "BOOL") {
        bool parsed = false;
        if (!parse_bool_local(literal.text, parsed)) {
            error = "Type error: literal `" + literal.text + "` is not BOOL";
            return false;
        }
    }
    return true;
}

bool validate_comparison_condition(const WhereComparison& where,
                                   const std::vector<Column>& columns,
                                   const std::unordered_map<std::string, std::size_t>& col_pos,
                                   std::string& error)
{
    if (!validate_operand_reference(where.lhs, col_pos, error) ||
        !validate_operand_reference(where.rhs, col_pos, error)) {
        return false;
    }

    const std::optional<std::string> lhs_type = operand_declared_type(where.lhs, columns, col_pos);
    const std::optional<std::string> rhs_type = operand_declared_type(where.rhs, columns, col_pos);
    if (lhs_type.has_value() && rhs_type.has_value() && lhs_type.value() != rhs_type.value()) {
        error = "Type error: cannot compare " + operand_label(where.lhs) + " (" + lhs_type.value() +
                ") with " + operand_label(where.rhs) + " (" + rhs_type.value() + ")";
        return false;
    }

    const std::string expected_type = lhs_type.value_or(rhs_type.value_or("STRING"));
    if (const auto* lhs_lit = std::get_if<Literal>(&where.lhs)) {
        if (!validate_literal_for_type(*lhs_lit, expected_type, error)) {
            return false;
        }
    }
    if (const auto* rhs_lit = std::get_if<Literal>(&where.rhs)) {
        if (!validate_literal_for_type(*rhs_lit, expected_type, error)) {
            return false;
        }
    }
    return true;
}

bool validate_like_condition(const WhereLike& where,
                             const std::vector<Column>& columns,
                             const std::unordered_map<std::string, std::size_t>& col_pos,
                             std::string& error)
{
    if (!validate_operand_reference(where.value, col_pos, error) ||
        !validate_operand_reference(where.pattern, col_pos, error)) {
        return false;
    }

    const std::optional<std::string> value_type = operand_declared_type(where.value, columns, col_pos);
    if (value_type.has_value() && value_type.value() != "STRING") {
        error = "Type error: LIKE value " + operand_label(where.value) + " must be STRING, got " + value_type.value();
        return false;
    }

    const std::optional<std::string> pattern_type = operand_declared_type(where.pattern, columns, col_pos);
    if (pattern_type.has_value() && pattern_type.value() != "STRING") {
        error = "Type error: LIKE pattern " + operand_label(where.pattern) + " must be STRING, got " + pattern_type.value();
        return false;
    }

    if (const auto* literal = std::get_if<Literal>(&where.pattern)) {
        if (!literal->is_null) {
            try {
                std::regex re(literal->text);
                (void)re;
            } catch (const std::regex_error& e) {
                error = "Runtime error: invalid LIKE regex `" + literal->text + "`: " + e.what();
                return false;
            }
        }
    }
    return true;
}

bool validate_where_condition(const WhereCondition& where,
                              const std::vector<Column>& columns,
                              const std::unordered_map<std::string, std::size_t>& col_pos,
                              std::string& error)
{
    if (const auto* cmp = std::get_if<WhereComparison>(&where)) {
        return validate_comparison_condition(*cmp, columns, col_pos, error);
    }
    if (const auto* between = std::get_if<WhereBetween>(&where)) {
        const WhereComparison lower_bound{between->value, ComparisonOp::ge, between->low};
        const WhereComparison upper_bound{between->value, ComparisonOp::le, between->high};
        return validate_comparison_condition(lower_bound, columns, col_pos, error) &&
               validate_comparison_condition(upper_bound, columns, col_pos, error);
    }
    if (const auto* like = std::get_if<WhereLike>(&where)) {
        return validate_like_condition(*like, columns, col_pos, error);
    }
    if (const auto* and_node = std::get_if<std::unique_ptr<WhereAnd>>(&where)) {
        return validate_where_condition((*and_node)->left, columns, col_pos, error)
            && validate_where_condition((*and_node)->right, columns, col_pos, error);
    }
    if (const auto* or_node = std::get_if<std::unique_ptr<WhereOr>>(&where)) {
        return validate_where_condition((*or_node)->left, columns, col_pos, error)
            && validate_where_condition((*or_node)->right, columns, col_pos, error);
    }
    error = "Semantic error: unsupported WHERE condition";
    return false;
}

struct IndexMutation
{
    std::size_t column_idx;
    Rid rid;
    std::optional<std::string> old_value;
    std::optional<std::string> new_value;
};

IndexMutation invert_index_mutation(const IndexMutation& mutation)
{
    return IndexMutation{
        mutation.column_idx,
        mutation.rid,
        mutation.new_value,
        mutation.old_value
    };
}

bool apply_index_mutation_for_column(const Column& column,
                                     const std::filesystem::path& idx_path,
                                     const IndexMutation& mutation)
{
    if (mutation.old_value == mutation.new_value) {
        return true;
    }

    if (column.type == "INT") {
        IndexManager<int> index(idx_path.string());
        if (mutation.old_value.has_value()) {
            int old_key = 0;
            if (!parse_int_local(*mutation.old_value, old_key)) {
                return false;
            }
            if (!index.erase(old_key)) {
                return false;
            }
        }
        if (mutation.new_value.has_value()) {
            int new_key = 0;
            if (!parse_int_local(*mutation.new_value, new_key)) {
                return false;
            }
            if (!index.insert(new_key, mutation.rid)) {
                return false;
            }
        }
        return true;
    }

    if (column.type == "BOOL") {
        IndexManager<int> index(idx_path.string());
        if (mutation.old_value.has_value()) {
            bool old_key = false;
            if (!parse_bool_local(*mutation.old_value, old_key)) {
                return false;
            }
            if (!index.erase(old_key ? 1 : 0)) {
                return false;
            }
        }
        if (mutation.new_value.has_value()) {
            bool new_key = false;
            if (!parse_bool_local(*mutation.new_value, new_key)) {
                return false;
            }
            if (!index.insert(new_key ? 1 : 0, mutation.rid)) {
                return false;
            }
        }
        return true;
    }

    if (column.type == "STRING") {
        IndexManager<StringIndexKey> index(idx_path.string());
        if (mutation.old_value.has_value()) {
            StringIndexKey old_key{};
            if (!StringIndexKey::from_string(*mutation.old_value, old_key)) {
                return false;
            }
            if (!index.erase(old_key)) {
                return false;
            }
        }
        if (mutation.new_value.has_value()) {
            StringIndexKey new_key{};
            if (!StringIndexKey::from_string(*mutation.new_value, new_key)) {
                return false;
            }
            if (!index.insert(new_key, mutation.rid)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

template<typename PathResolverFn>
void rollback_applied_index_mutations(const std::vector<IndexMutation>& mutations,
                                      std::size_t applied_count,
                                      const std::vector<Column>& columns,
                                      PathResolverFn&& resolve_idx_path)
{
    while (applied_count > 0) {
        --applied_count;
        const IndexMutation rollback = invert_index_mutation(mutations[applied_count]);
        if (rollback.column_idx >= columns.size()) {
            continue;
        }
        const Column& column = columns[rollback.column_idx];
        const std::filesystem::path idx_path = resolve_idx_path(rollback.column_idx);
        (void)apply_index_mutation_for_column(column, idx_path, rollback);
    }
}

template<typename PathResolverFn>
bool apply_index_mutations_with_rollback(const std::vector<IndexMutation>& mutations,
                                         const std::vector<Column>& columns,
                                         PathResolverFn&& resolve_idx_path,
                                         std::size_t& out_applied_count)
{
    out_applied_count = 0;
    for (const IndexMutation& mutation : mutations) {
        if (mutation.column_idx >= columns.size()) {
            rollback_applied_index_mutations(mutations, out_applied_count, columns, resolve_idx_path);
            out_applied_count = 0;
            return false;
        }
        const Column& column = columns[mutation.column_idx];
        const std::filesystem::path idx_path = resolve_idx_path(mutation.column_idx);
        if (!apply_index_mutation_for_column(column, idx_path, mutation)) {
            rollback_applied_index_mutations(mutations, out_applied_count, columns, resolve_idx_path);
            out_applied_count = 0;
            return false;
        }
        ++out_applied_count;
    }
    return true;
}

bool ensure_page_in_batch(TablePageManager& manager,
                          int page_id,
                          std::unordered_map<int, Page>& original_pages,
                          std::unordered_map<int, Page>& modified_pages,
                          Page*& out_page)
{
    auto it_modified = modified_pages.find(page_id);
    if (it_modified == modified_pages.end()) {
        Page page;
        if (!manager.read_page(page_id, page)) {
            out_page = nullptr;
            return false;
        }
        original_pages.emplace(page_id, page);
        it_modified = modified_pages.emplace(page_id, page).first;
    }

    out_page = &it_modified->second;
    return true;
}

template<typename FailureFn>
bool write_modified_pages_with_rollback(TablePageManager& manager,
                                        const std::unordered_map<int, Page>& original_pages,
                                        const std::unordered_map<int, Page>& modified_pages,
                                        FailureFn&& on_failure)
{
    std::vector<int> written_pages;
    written_pages.reserve(modified_pages.size());
    for (const auto& [page_id, page] : modified_pages) {
        if (!manager.write_page(page_id, page)) {
            for (int written_page_id : written_pages) {
                const auto it_original = original_pages.find(written_page_id);
                if (it_original != original_pages.end()) {
                    manager.write_page(written_page_id, it_original->second);
                }
            }
            on_failure();
            return false;
        }
        written_pages.push_back(page_id);
    }
    return true;
}

struct OrderedPageWrite
{
    int page_id;
    Page page;
    std::optional<Page> original_page;
};

template<typename FailureFn>
bool write_ordered_pages_with_rollback(TablePageManager& manager,
                                       const std::vector<OrderedPageWrite>& page_writes,
                                       FailureFn&& on_failure)
{
    std::vector<std::size_t> written_pages;
    written_pages.reserve(page_writes.size());

    for (std::size_t i = 0; i < page_writes.size(); ++i) {
        const OrderedPageWrite& write = page_writes[i];
        if (!manager.write_page(write.page_id, write.page)) {
            if (write.original_page.has_value()) {
                manager.write_page(write.page_id, *write.original_page);
            }
            while (!written_pages.empty()) {
                const OrderedPageWrite& written = page_writes[written_pages.back()];
                written_pages.pop_back();
                if (written.original_page.has_value()) {
                    manager.write_page(written.page_id, *written.original_page);
                }
            }
            on_failure();
            return false;
        }
        written_pages.push_back(i);
    }
    return true;
}


}

std::string Executor::current_db() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _current_db;
}

std::string Executor::last_select_json() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_select_json;
}

std::string Executor::last_error() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_error;
}

bool Executor::last_operation_used_index() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last_operation_used_index;
}

bool Executor::fail(std::string message) const
{
    _last_error = std::move(message);
    return false;
}

bool Executor::execute(const Statement& stmt)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _last_error.clear();
    _last_operation_used_index = false;
    if (const auto* s = std::get_if<CreateDatabaseStmt>(&stmt)) {
        return execute_create_database(*s);
    }
    if (const auto* s = std::get_if<DropDatabaseStmt>(&stmt)) {
        return execute_drop_database(*s);
    }
    if (const auto* s = std::get_if<UseDatabaseStmt>(&stmt)) {
        return execute_use_database(*s);
    }
    if (const auto* s = std::get_if<CreateTableStmt>(&stmt)) {
        return execute_create_table(*s);
    }
    if (const auto* s = std::get_if<DropTableStmt>(&stmt)) {
        return execute_drop_table(*s);
    }
    if (const auto* s = std::get_if<InsertStmt>(&stmt)) {
        return execute_insert(*s);
    }
    if (const auto* s = std::get_if<SelectStmt>(&stmt)) {
        return execute_select(*s);
    }
    if (const auto* s = std::get_if<UpdateStmt>(&stmt)) {
        return execute_update(*s);
    }
    if (const auto* s = std::get_if<DeleteStmt>(&stmt)) {
        return execute_delete(*s);
    }
    return fail("Runtime error: unsupported statement type");
}

bool Executor::execute_create_database(const CreateDatabaseStmt& stmt)
{
    auto& dbs = _dbms.catalog().databases();
    auto it = std::find_if(dbs.begin(), dbs.end(), [&](const Database& db){ return db.name() == stmt.name; });
    if (it != dbs.end()) {
        return fail("Semantic error: database `" + stmt.name + "` already exists");
    }
    std::error_code ec;
    std::filesystem::create_directories(database_path(stmt.name), ec);
    if (ec) {
        return fail("Runtime error: cannot create database `" + stmt.name + "` at `" +
                    database_path(stmt.name).string() + "`: " + ec.message());
    }
    dbs.push_back(Database(stmt.name));
    return true;
}

bool Executor::execute_drop_database(const DropDatabaseStmt& stmt)
{
    auto& dbs = _dbms.catalog().databases();
    auto it = std::find_if(dbs.begin(), dbs.end(), [&](const Database& db){ return db.name() == stmt.name; });
    if (it == dbs.end()) {
        return fail("Semantic error: database `" + stmt.name + "` does not exist");
    }
    std::error_code ec;
    std::filesystem::remove_all(database_path(stmt.name), ec);
    if (ec) {
        return fail("Runtime error: cannot drop database `" + stmt.name + "`: " + ec.message());
    }
    dbs.erase(it);
    if (_current_db == stmt.name) {
        _current_db.clear();
    }
    return true;
}

bool Executor::execute_use_database(const UseDatabaseStmt& stmt)
{
    auto& dbs = _dbms.catalog().databases();
    auto it = std::find_if(dbs.begin(), dbs.end(), [&](const Database& db){ return db.name() == stmt.name; });
    if (it == dbs.end()) {
        return fail("Semantic error: database `" + stmt.name + "` does not exist");
    }
    _current_db = stmt.name;
    return true;
}


std::optional<ResolvedTableName> Executor::resolve_table_name(const std::string& raw_name) const
{
    const std::size_t dot = raw_name.find('.');
    if (dot == std::string::npos) {
        if (_current_db.empty()) {
            return std::nullopt;
        }
        return ResolvedTableName{_current_db, raw_name};
    }

    if (dot == 0 || dot + 1 >= raw_name.size()) {
        return std::nullopt;
    }

    return ResolvedTableName{
        raw_name.substr(0, dot),
        raw_name.substr(dot + 1)
    };
}

Database* Executor::find_database(const std::string& db_name)
{
    auto& dbs = _dbms.catalog().databases();
    auto it = std::find_if(dbs.begin(), dbs.end(), [&](const Database& db) {
        return db.name() == db_name;
    });
    if (it == dbs.end()) {
        return nullptr;
    }
    return &(*it);
}


Database* Executor::find_current_database()
{
    if (_current_db.empty()) {
        return nullptr;
    }
    auto& dbs = _dbms.catalog().databases();
    auto it = std::find_if(dbs.begin(), dbs.end(), [&](const Database& db){ return db.name() == _current_db; });
    if (it == dbs.end()) {
        return nullptr;
    }
    return &(*it);
}

Table* Executor::find_table(Database& db, const std::string& table_name)
{
    auto& tables = db.tables();
    auto it = std::find_if(tables.begin(), tables.end(), [&](const Table& table){ return table.name() == table_name; });
    if (it == tables.end()) {
        return nullptr;
    }
    return &(*it);
}

bool Executor::parse_int_strict(const std::string& text, int& out_value)
{
    if (text.empty()) {
        return false;
    }
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out_value = value;
    return true;
}

std::string Executor::to_lower_ascii(std::string text)
{
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

bool Executor::parse_bool_strict(const std::string& text, bool& out_value)
{
    const std::string lower = to_lower_ascii(text);
    if (lower == "true" || lower == "1") {
        out_value = true;
        return true;
    }
    if (lower == "false" || lower == "0") {
        out_value = false;
        return true;
    }
    return false;
}

std::filesystem::path Executor::database_path(const std::string& db_name) const
{
    return _dbms.data_root() / db_name;
}

std::filesystem::path Executor::table_path(const std::string& db_name, const std::string& table_name) const
{
    return database_path(db_name) / (table_name + ".tbl");
}

std::filesystem::path Executor::index_path(const std::string& db_name, const std::string& table_name, const std::string& column_name) const
{
    return database_path(db_name) / (table_name + "__" + column_name + ".idx");
}

bool Executor::is_type_valid(const std::string& type)
{
    return type == "INT" || type == "STRING" || type == "BOOL";
}

bool Executor::execute_create_table(const CreateTableStmt& stmt)
{
    Database* db = find_current_database();
    if (db == nullptr) {
        return fail("Semantic error: no active database selected for CREATE TABLE `" + stmt.name + "`");
    }
    auto& tables = db->tables();
    auto it = std::find_if(tables.begin(), tables.end(), [&](const Table& t){ return t.name() == stmt.name; });
    if (it != tables.end()) {
        return fail("Semantic error: table `" + _current_db + "." + stmt.name + "` already exists");
    }
    std::unordered_set<std::string> names;
    std::vector<Column> columns;
    columns.reserve(stmt.columns.size());
    for (const auto& col : stmt.columns) {
        if (!is_type_valid(col.type)) {
            return fail("Semantic error: column `" + col.name + "` has unsupported type `" + col.type + "`");
        }
        if (!names.insert(col.name).second) {
            return fail("Semantic error: duplicate column `" + col.name + "` in table `" + stmt.name + "`");
        }
        Column c{};
        c.name = col.name;
        c.type = col.type;
        c.not_null = col.not_null;
        c.indexed = col.indexed;
        c.default_value = col.default_value;
        columns.push_back(std::move(c));
    }
    std::error_code ec;
    std::filesystem::create_directories(database_path(_current_db), ec);
    if (ec) {
        return fail("Runtime error: cannot create directory for database `" + _current_db + "`: " + ec.message());
    }
    const std::filesystem::path tbl_path = table_path(_current_db, stmt.name);
    TablePageManager manager(tbl_path.string());
    if (!manager.write_schema(columns)) {
        return fail("Runtime error: cannot write schema for table `" + _current_db + "." + stmt.name + "`");
    }
    std::vector<std::filesystem::path> created_index_paths;
    for (const auto& col : columns) {
        if (!col.indexed) {
            continue;
        }
        const std::filesystem::path idx_path = index_path(_current_db, stmt.name, col.name);
        if (col.type == "INT" || col.type == "BOOL") {
            IndexManager<int> index(idx_path.string());
        }
        else if (col.type == "STRING") {
            IndexManager<StringIndexKey> index(idx_path.string());
        }

        std::error_code idx_ec;
        if (!std::filesystem::is_regular_file(idx_path, idx_ec)) {
            std::error_code cleanup_ec;
            for (const auto& created_path : created_index_paths) {
                std::filesystem::remove(created_path, cleanup_ec);
            }
            std::filesystem::remove(tbl_path, cleanup_ec);
            return fail("Runtime error: cannot create index for `" + _current_db + "." +
                        stmt.name + "." + col.name + "` at `" + idx_path.string() + "`");
        }
        created_index_paths.push_back(idx_path);
    }
    tables.push_back(Table(stmt.name, std::move(columns)));
    return true;
}

bool Executor::execute_drop_table(const DropTableStmt& stmt)
{
    Database* db = find_current_database();
    if (db == nullptr) {
        return fail("Semantic error: no active database selected for DROP TABLE `" + stmt.name + "`");
    }
    auto& tables = db->tables();
    auto it = std::find_if(tables.begin(), tables.end(), [&](const Table& t){ return t.name() == stmt.name; });
    if (it == tables.end()) {
        return fail("Semantic error: table `" + _current_db + "." + stmt.name + "` does not exist");
    }
    std::error_code ec;
    const std::filesystem::path tbl_path = table_path(_current_db, stmt.name);
    if (!std::filesystem::is_regular_file(tbl_path, ec)) {
        return fail("Runtime error: table file for `" + _current_db + "." + stmt.name +
                    "` is missing or is not a regular file");
    }
    for (const auto& col : it->columns()) {
        if (!col.indexed) {
            continue;
        }
        const std::filesystem::path idx_path = index_path(_current_db, stmt.name, col.name);
        if (std::filesystem::exists(idx_path, ec) && !std::filesystem::is_regular_file(idx_path, ec)) {
            return fail("Runtime error: index for `" + _current_db + "." + stmt.name + "." +
                        col.name + "` is not a regular file");
        }
    }

    std::filesystem::remove(tbl_path, ec);
    if (ec) {
        return fail("Runtime error: cannot remove table file for `" + _current_db + "." + stmt.name + "`: " + ec.message());
    }
    for (const auto& col : it->columns()) {
        if (!col.indexed) {
            continue;
        }
        std::filesystem::remove(index_path(_current_db, stmt.name, col.name), ec);
        if (ec) {
            return fail("Runtime error: cannot remove index for `" + _current_db + "." + stmt.name + "." + col.name + "`: " + ec.message());
        }
    }
    tables.erase(it);
    return true;
}

bool Executor::normalize_insert_row(const Table& table,
                                    const InsertStmt& stmt,
                                    const std::vector<InsertValue>& raw_row,
                                    std::vector<int>& int_index_keys,
                                    std::vector<int>& bool_index_keys,
                                    std::vector<std::string>& string_index_keys,
                                    row_values_type& out_row) const
{
    if (stmt.columns.empty() || raw_row.size() != stmt.columns.size()) {
        return fail("Semantic error: INSERT into `" + table.name() + "` has " +
                    std::to_string(raw_row.size()) + " values for " +
                    std::to_string(stmt.columns.size()) + " columns");
    }
    const auto& table_columns = table.columns();
    std::unordered_map<std::string, std::size_t> table_column_pos;
    table_column_pos.reserve(table_columns.size());
    for (std::size_t i = 0; i < table_columns.size(); ++i) {
        table_column_pos[table_columns[i].name] = i;
    }

    out_row.assign(table_columns.size(), std::nullopt);
    int_index_keys.assign(table_columns.size(), 0);
    bool_index_keys.assign(table_columns.size(), 0);
    string_index_keys.assign(table_columns.size(), std::string());
    std::vector<bool> assigned(table_columns.size(), false);

    for (std::size_t i = 0; i < stmt.columns.size(); ++i) {
        const auto it_column = table_column_pos.find(stmt.columns[i]);
        if (it_column == table_column_pos.end()) {
            return fail("Semantic error: table `" + table.name() + "` has no column `" + stmt.columns[i] + "`");
        }
        const std::size_t column_index = it_column->second;
        if (assigned[column_index]) {
            return fail("Semantic error: column `" + stmt.columns[i] + "` is specified more than once in INSERT");
        }
        assigned[column_index] = true;
        const auto& column = table_columns[column_index];
        const auto& value = raw_row[i];
        if (value.is_null) {
            if (column.not_null || column.indexed) {
                return fail("Constraint error: column `" + column.name + "` cannot be NULL" +
                            std::string(column.indexed ? " because it is INDEXED" : " because it is NOT NULL"));
            }
            out_row[column_index] = std::nullopt;
            continue;
        }
        if (column.type == "INT") {
            int parsed_int = 0;
            if (!parse_int_strict(value.text, parsed_int)) {
                return fail("Type error: value `" + value.text + "` for column `" + column.name + "` is not INT");
            }
            out_row[column_index] = std::to_string(parsed_int);
            if (column.indexed) {
                int_index_keys[column_index] = parsed_int;
            }
            continue;
        }
        if (column.type == "STRING") {
            out_row[column_index] = value.text;
            if (column.indexed) {
                string_index_keys[column_index] = value.text;
            }
            continue;
        }
        if (column.type == "BOOL") {
            bool parsed_bool = false;
            if (!parse_bool_strict(value.text, parsed_bool)) {
                return fail("Type error: value `" + value.text + "` for column `" + column.name + "` is not BOOL");
            }
            out_row[column_index] = parsed_bool ? "true" : "false";
            if (column.indexed) {
                bool_index_keys[column_index] = parsed_bool ? 1 : 0;
            }
            continue;
        }
        return fail("Semantic error: column `" + column.name + "` has unsupported type `" + column.type + "`");
    }

    for (std::size_t i = 0; i < table_columns.size(); ++i) {
        if (assigned[i]) {
            continue;
        }
        if (table_columns[i].default_value.has_value()) {
            out_row[i] = table_columns[i].default_value;
            continue;
        }
        if (table_columns[i].not_null || table_columns[i].indexed) {
            return fail("Constraint error: required column `" + table_columns[i].name +
                        "` was omitted from INSERT into `" + table.name() + "`");
        }
        out_row[i] = std::nullopt;
    }
    return true;
}

bool Executor::collect_matching_rows(const std::string& db_name,
                                     const Table& table,
                                     const std::optional<WhereCondition>& where,
                                     std::vector<std::pair<Rid, row_values_type>>& out_rows) const
{
    out_rows.clear();
    const auto& columns = table.columns();

    std::unordered_map<std::string, std::size_t> col_pos;
    col_pos.reserve(columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        col_pos[columns[i].name] = i;
    }

    if (where.has_value()) {
        std::string where_error;
        if (!validate_where_condition(where.value(), columns, col_pos, where_error)) {
            return fail(where_error);
        }
    }

    const WhereComparison* where_cmp = nullptr;
    const WhereBetween* where_between = nullptr;
    if (where.has_value()) {
        where_cmp = std::get_if<WhereComparison>(&where.value());
        where_between = std::get_if<WhereBetween>(&where.value());
    }

    TablePageManager manager(table_path(db_name, table.name()).string());

    bool used_index_candidates = false;
    if (where_cmp != nullptr) {
        IndexWherePlan index_plan{};
        if (try_build_index_where_plan(*where_cmp, columns, col_pos, index_plan)) {
            const Column& idx_col = columns[index_plan.column_idx];
            const std::filesystem::path idx_path = index_path(db_name, table.name(), idx_col.name);
            bool used_index = false;
            std::vector<Rid> rid_candidates;
            if (!collect_rids_from_index(idx_path, idx_col, index_plan.op, index_plan.literal, used_index, rid_candidates)) {
                return fail("Runtime error: cannot read index `" + idx_path.string() + "` for column `" + idx_col.name + "`");
            }
            if (used_index) {
                used_index_candidates = true;
                _last_operation_used_index = true;
                out_rows.reserve(rid_candidates.size());
                for (const Rid& rid : rid_candidates) {
                    row_values_type row;
                    if (!read_row_by_rid(manager, rid, columns.size(), row)) {
                        return fail("Runtime error: index `" + idx_path.string() +
                                    "` points to unreadable table row");
                    }
                    if (where.has_value() && !evaluate_where_condition(where.value(), row, columns, col_pos)) {
                        continue;
                    }
                    out_rows.push_back({rid, std::move(row)});
                }
            }
        }
    }
    if (!used_index_candidates && where_between != nullptr) {
        IndexBetweenPlan index_plan{};
        if (try_build_index_between_plan(*where_between, columns, col_pos, index_plan)) {
            const Column& idx_col = columns[index_plan.column_idx];
            const std::filesystem::path idx_path = index_path(db_name, table.name(), idx_col.name);
            bool used_index = false;
            std::vector<Rid> rid_candidates;
            if (!collect_between_rids_from_index(idx_path,
                                                 idx_col,
                                                 index_plan.low_literal,
                                                 index_plan.high_literal,
                                                 used_index,
                                                 rid_candidates)) {
                return fail("Runtime error: cannot read index `" + idx_path.string() + "` for column `" + idx_col.name + "`");
            }
            if (used_index) {
                used_index_candidates = true;
                _last_operation_used_index = true;
                out_rows.reserve(rid_candidates.size());
                for (const Rid& rid : rid_candidates) {
                    row_values_type row;
                    if (!read_row_by_rid(manager, rid, columns.size(), row)) {
                        return fail("Runtime error: index `" + idx_path.string() +
                                    "` points to unreadable table row");
                    }
                    if (where.has_value() && !evaluate_where_condition(where.value(), row, columns, col_pos)) {
                        continue;
                    }
                    out_rows.push_back({rid, std::move(row)});
                }
            }
        }
    }

    if (!used_index_candidates) {
        Page meta;
        if (!manager.read_page(0, meta)) {
            return fail("Runtime error: cannot read table header for `" + db_name + "." + table.name() + "`");
        }
        TableHeader th{};
        std::memcpy(&th, meta.data().data(), sizeof(TableHeader));

        for (int page_id = 1; page_id < th.next_page_id; ++page_id) {
            Page page;
            if (!manager.read_page(page_id, page)) {
                return fail("Runtime error: cannot read table page " + std::to_string(page_id) +
                            " for `" + db_name + "." + table.name() + "`");
            }
            const PageHeader ph = page.read_header();
            for (int slot = 0; slot < ph.slots_count; ++slot) {
                const auto raw = page.read_record(slot);
                if (raw.empty()) {
                    continue;
                }
                Record rec = deserialize_record(raw.data(), columns.size());
                row_values_type row = rec.values();
                if (where.has_value() && !evaluate_where_condition(where.value(), row, columns, col_pos)) {
                    continue;
                }
                out_rows.push_back({Rid{page_id, slot}, std::move(row)});
            }
        }
    }

    return true;
}

bool Executor::execute_insert(const InsertStmt& stmt)
{
    std::optional<ResolvedTableName> resolved = resolve_table_name(stmt.table_name);
    if (!resolved.has_value()) {
        return fail("Semantic error: no active database selected for INSERT into `" + stmt.table_name + "`");
    }

    Database* db = find_database(resolved->db_name);
    if (db == nullptr) {
        return fail("Semantic error: database `" + resolved->db_name + "` does not exist");
    }

    Table* table = find_table(*db, resolved->table_name);
    if (table == nullptr) {
        return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name + "` does not exist");
    }

    if (stmt.rows.empty() || stmt.columns.empty()) {
        return fail("Semantic error: INSERT into `" + resolved->db_name + "." + resolved->table_name +
                    "` requires at least one column and one row");
    }

    struct PendingRow
    {
        row_values_type values;
        std::vector<int> int_index_keys;
        std::vector<int> bool_index_keys;
        std::vector<std::string> string_index_keys;
    };

    std::vector<PendingRow> pending_rows;
    pending_rows.reserve(stmt.rows.size());

    std::unordered_map<std::size_t, std::unordered_set<int>> seen_int_keys;
    std::unordered_map<std::size_t, std::unordered_set<int>> seen_bool_keys;
    std::unordered_map<std::size_t, std::unordered_set<std::string>> seen_string_keys;

    const auto& columns = table->columns();
    for (const auto& raw_row : stmt.rows) {
        PendingRow row{};
        if (!normalize_insert_row(*table, stmt, raw_row, row.int_index_keys, row.bool_index_keys, row.string_index_keys, row.values)) {
            return false;
        }

        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (!columns[i].indexed) {
                continue;
            }
            if (!row.values[i].has_value()) {
                return fail("Constraint error: indexed column `" + columns[i].name + "` cannot be NULL");
            }
            const std::filesystem::path idx_path = index_path(resolved->db_name, table->name(), columns[i].name);
            if (columns[i].type == "INT") {
                const int key = row.int_index_keys[i];
                auto& seen = seen_int_keys[i];
                if (!seen.insert(key).second) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(key) +
                                "` for column `" + columns[i].name + "` in INSERT batch");
                }
                IndexManager<int> index(idx_path.string());
                Rid existing{};
                if (index.find(key, existing)) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(key) +
                                "` for column `" + columns[i].name + "`");
                }
                continue;
            }
            if (columns[i].type == "STRING") {
                const std::string& value = row.string_index_keys[i];
                auto& seen = seen_string_keys[i];
                if (!seen.insert(value).second) {
                    return fail("Constraint error: duplicate INDEXED key `" + value +
                                "` for column `" + columns[i].name + "` in INSERT batch");
                }
                StringIndexKey key{};
                if (!StringIndexKey::from_string(value, key)) {
                    return fail("Constraint error: value for INDEXED STRING column `" + columns[i].name +
                                "` is too long for the index key");
                }
                IndexManager<StringIndexKey> index(idx_path.string());
                Rid existing{};
                if (index.find(key, existing)) {
                    return fail("Constraint error: duplicate INDEXED key `" + value +
                                "` for column `" + columns[i].name + "`");
                }
                continue;
            }
            if (columns[i].type == "BOOL") {
                const int key = row.bool_index_keys[i];
                auto& seen = seen_bool_keys[i];
                if (!seen.insert(key).second) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(key) +
                                "` for column `" + columns[i].name + "` in INSERT batch");
                }
                IndexManager<int> index(idx_path.string());
                Rid existing{};
                if (index.find(key, existing)) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(key) +
                                "` for column `" + columns[i].name + "`");
                }
                continue;
            }
            return fail("Semantic error: indexed column `" + columns[i].name + "` has unsupported type `" + columns[i].type + "`");
        }

        pending_rows.push_back(std::move(row));
    }

    std::error_code ec;
    std::filesystem::create_directories(database_path(resolved->db_name), ec);
    if (ec) {
        return fail("Runtime error: cannot create directory for database `" + resolved->db_name + "`: " + ec.message());
    }
    TablePageManager manager(table_path(resolved->db_name, table->name()).string());

    Page original_header_page;
    if (!manager.read_page(0, original_header_page)) {
        return fail("Runtime error: cannot read table header for `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    TableHeader header{};
    std::memcpy(&header, original_header_page.data().data(), sizeof(TableHeader));
    if (header.next_page_id < 1) {
        return fail("Runtime error: invalid table header for `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    int next_page_id = header.next_page_id;
    std::vector<OrderedPageWrite> page_writes;
    page_writes.reserve(pending_rows.size() + 1u);
    std::vector<IndexMutation> index_changes;
    index_changes.reserve(pending_rows.size() * columns.size());

    for (const auto& row : pending_rows) {
        Record record(row.values);
        std::vector<unsigned char> bytes = serialize_record(record, columns.size());
        Page page;
        int slot_id = page.append_record(bytes.data(), static_cast<int>(bytes.size()));
        if (slot_id < 0) {
            return fail("Runtime error: record is too large for a table page in `" +
                        resolved->db_name + "." + table->name() + "`");
        }
        const int page_id = next_page_id;
        ++next_page_id;
        const Rid rid{page_id, slot_id};
        page_writes.push_back(OrderedPageWrite{page_id, page, std::nullopt});

        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (!columns[i].indexed) {
                continue;
            }
            index_changes.push_back(IndexMutation{i, rid, std::nullopt, row.values[i]});
        }
    }

    Page new_header_page = original_header_page;
    header.next_page_id = next_page_id;
    std::memcpy(new_header_page.data().data(), &header, sizeof(TableHeader));
    page_writes.push_back(OrderedPageWrite{0, new_header_page, original_header_page});

    std::size_t applied_changes = 0;
    const auto resolve_idx_path = [&](std::size_t column_idx) {
        return index_path(resolved->db_name, table->name(), columns[column_idx].name);
    };
    if (!apply_index_mutations_with_rollback(index_changes, columns, resolve_idx_path, applied_changes)) {
        return fail("Runtime error: cannot update one or more indexes for INSERT into `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    if (!write_ordered_pages_with_rollback(manager,
                                           page_writes,
                                           [&]() {
                                               rollback_applied_index_mutations(index_changes,
                                                                                applied_changes,
                                                                                columns,
                                                                                resolve_idx_path);
                                           })) {
        return fail("Runtime error: cannot write inserted table pages for `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    return true;
}

void Executor::build_last_select_json()
{
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& row : _last_select_rows) {
        nlohmann::json obj = nlohmann::json::object();

        for (std::size_t i = 0; i < _last_select_columns.size(); ++i) {
            const std::string& key = _last_select_columns[i];
            const std::optional<std::string> value =
                (i < row.size()) ? row[i] : std::nullopt;

            if (!value.has_value()) {
                obj[key] = nullptr;
                continue;
            }

            const std::string type =
                (i < _last_select_column_types.size()) ? _last_select_column_types[i] : "STRING";

            if (type == "INT") {
                int v = 0;
                if (parse_int_strict(*value, v)) obj[key] = v;
                else obj[key] = *value;
            } else if (type == "BOOL") {
                bool v = false;
                if (parse_bool_strict(*value, v)) obj[key] = v;
                else obj[key] = *value;
            } else {
                obj[key] = *value;
            }
        }

        arr.push_back(std::move(obj));
    }

    _last_select_json = arr.dump(2); 
}

bool Executor::execute_select(const SelectStmt& stmt)
{
    _last_select_columns.clear();
    _last_select_rows.clear();
    _last_select_column_types.clear();
    _last_select_json.clear();

    std::optional<ResolvedTableName> resolved = resolve_table_name(stmt.table_name);
    if (!resolved.has_value()) {
        return fail("Semantic error: no active database selected for SELECT from `" + stmt.table_name + "`");
    }

    Database* db = find_database(resolved->db_name);
    if (db == nullptr) {
        return fail("Semantic error: database `" + resolved->db_name + "` does not exist");
    }

    Table* table = find_table(*db, resolved->table_name);
    if (table == nullptr) {
        return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name + "` does not exist");
    }
    
    const auto& columns = table->columns();
    std::unordered_map<std::string, std::size_t> col_pos;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        col_pos[columns[i].name] = i;
    }

    // проверяем есть ли агрегаты в проекции
    const bool has_aggregates = !stmt.projection.is_star &&
        std::any_of(stmt.projection.items.begin(), stmt.projection.items.end(),
                    [](const SelectItem& it) { return it.aggregate.has_value(); });

    std::vector<std::size_t> selected_idx;
    if (!has_aggregates) {
        if (stmt.projection.is_star) {
            for (std::size_t i = 0; i < columns.size(); ++i) {
                selected_idx.push_back(i);
                _last_select_columns.push_back(columns[i].name);
                _last_select_column_types.push_back(columns[i].type);
            }
        } else {
            for (const auto& item : stmt.projection.items) {
                auto it = col_pos.find(item.column_name);
                if (it == col_pos.end()) {
                    return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name +
                                "` has no column `" + item.column_name + "` in SELECT projection");
                }
                const std::size_t column_idx = it->second;
                selected_idx.push_back(column_idx);
                _last_select_columns.push_back(item.alias.has_value() ? item.alias.value() : item.column_name);
                _last_select_column_types.push_back(columns[column_idx].type);
            }
        }
    }

    std::vector<std::pair<Rid, row_values_type>> matched_rows;
    if (!collect_matching_rows(resolved->db_name, *table, stmt.where, matched_rows)) {
        return fail(_last_error.empty()
            ? "Runtime error: cannot read matching rows from `" + resolved->db_name + "." + table->name() + "`"
            : _last_error);
    }

    if (has_aggregates) {
        row_values_type agg_row;
        for (const auto& item : stmt.projection.items) {
            auto it = col_pos.find(item.column_name);
            if (it == col_pos.end()) {
                return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name +
                            "` has no column `" + item.column_name + "` in aggregate");
            }
            const std::size_t cidx = it->second;
            const AggregateFunc func = item.aggregate.value();

            long long sum_val = 0;
            long long count_val = 0;

            for (const auto& matched : matched_rows) {
                const auto& cell = matched.second[cidx];
                if (!cell.has_value()) {
                    continue;
                }
                count_val++;
                if (func == AggregateFunc::sum || func == AggregateFunc::avg) {
                    int parsed = 0;
                    if (parse_int_strict(cell.value(), parsed)) {
                        sum_val += parsed;
                    }
                }
            }

            std::string result;
            if (func == AggregateFunc::count) {
                result = std::to_string(count_val);
            } else if (func == AggregateFunc::sum) {
                result = std::to_string(sum_val);
            } else { // avg
                if (count_val == 0) {
                    agg_row.push_back(std::nullopt);
                    const std::string col_name = item.alias.has_value() ? item.alias.value()
                        : ("AVG(" + item.column_name + ")");
                    _last_select_columns.push_back(col_name);
                    _last_select_column_types.push_back("STRING");
                    continue;
                }
                const double avg_val = static_cast<double>(sum_val) / static_cast<double>(count_val);
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(2);
                oss << avg_val;
                result = oss.str();
            }

            agg_row.push_back(result);
            const std::string col_name = item.alias.has_value() ? item.alias.value()
                : ((func == AggregateFunc::sum ? "SUM(" :
                    func == AggregateFunc::count ? "COUNT(" : "AVG(") + item.column_name + ")");
            _last_select_columns.push_back(col_name);
            _last_select_column_types.push_back("STRING");
        }
        _last_select_rows.push_back(std::move(agg_row));
        build_last_select_json();
        return true;
    }

    _last_select_rows.reserve(matched_rows.size());
    for (const auto& matched : matched_rows) {
        const row_values_type& row = matched.second;
        row_values_type projected;
        projected.reserve(selected_idx.size());
        for (std::size_t idx : selected_idx) {
            projected.push_back(row[idx]);
        }
        _last_select_rows.push_back(std::move(projected));
    }

    build_last_select_json();
    return true;
}

bool Executor::execute_update(const UpdateStmt& stmt)
{
    std::optional<ResolvedTableName> resolved = resolve_table_name(stmt.table_name);
    if (!resolved.has_value()) {
        return fail("Semantic error: no active database selected for UPDATE `" + stmt.table_name + "`");
    }

    Database* db = find_database(resolved->db_name);
    if (db == nullptr) {
        return fail("Semantic error: database `" + resolved->db_name + "` does not exist");
    }

    Table* table = find_table(*db, resolved->table_name);
    if (table == nullptr) {
        return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name + "` does not exist");
    }

    if (stmt.assignments.empty()) {
        return fail("Semantic error: UPDATE `" + resolved->db_name + "." + resolved->table_name +
                    "` requires at least one assignment");
    }
    const auto& columns = table->columns();

    std::unordered_map<std::string, std::size_t> col_pos;
    col_pos.reserve(columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        col_pos[columns[i].name] = i;
    }

    std::vector<bool> has_assignment(columns.size(), false);
    std::vector<std::optional<std::string>> assignment_values(columns.size(), std::nullopt);
    std::vector<std::size_t> assigned_indexed_columns;
    assigned_indexed_columns.reserve(stmt.assignments.size());

    for (const auto& assignment : stmt.assignments) {
        const auto it = col_pos.find(assignment.column_name);
        if (it == col_pos.end()) {
            return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name +
                        "` has no column `" + assignment.column_name + "` in UPDATE");
        }
        const std::size_t column_idx = it->second;
        if (has_assignment[column_idx]) {
            return fail("Semantic error: column `" + assignment.column_name + "` is assigned more than once");
        }
        has_assignment[column_idx] = true;

        const Column& column = columns[column_idx];
        if (assignment.value.is_null) {
            if (column.not_null || column.indexed) {
                return fail("Constraint error: column `" + column.name + "` cannot be set to NULL" +
                            std::string(column.indexed ? " because it is INDEXED" : " because it is NOT NULL"));
            }
            assignment_values[column_idx] = std::nullopt;
        } else if (column.type == "INT") {
            int parsed = 0;
            if (!parse_int_strict(assignment.value.text, parsed)) {
                return fail("Type error: value `" + assignment.value.text + "` for column `" + column.name + "` is not INT");
            }
            assignment_values[column_idx] = std::to_string(parsed);
        } else if (column.type == "STRING") {
            assignment_values[column_idx] = assignment.value.text;
        } else if (column.type == "BOOL") {
            bool parsed = false;
            if (!parse_bool_strict(assignment.value.text, parsed)) {
                return fail("Type error: value `" + assignment.value.text + "` for column `" + column.name + "` is not BOOL");
            }
            assignment_values[column_idx] = parsed ? "true" : "false";
        } else {
            return fail("Semantic error: column `" + column.name + "` has unsupported type `" + column.type + "`");
        }

        if (column.indexed) {
            assigned_indexed_columns.push_back(column_idx);
        }
    }

    struct PendingUpdate
    {
        Rid rid;
        row_values_type old_row;
        row_values_type new_row;
    };

    std::vector<std::pair<Rid, row_values_type>> matched_rows;
    if (!collect_matching_rows(resolved->db_name, *table, stmt.where, matched_rows)) {
        return fail(_last_error.empty()
            ? "Runtime error: cannot read matching rows from `" + resolved->db_name + "." + table->name() + "`"
            : _last_error);
    }

    std::vector<PendingUpdate> pending;
    pending.reserve(matched_rows.size());
    for (auto& matched : matched_rows) {
        row_values_type updated = matched.second;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (has_assignment[i]) {
                updated[i] = assignment_values[i];
            }
        }
        if (updated == matched.second) {
            continue;
        }

        pending.push_back(PendingUpdate{matched.first, std::move(matched.second), std::move(updated)});
    }

    if (pending.empty()) {
        return true;
    }

    TablePageManager manager(table_path(resolved->db_name, table->name()).string());

    for (std::size_t column_idx : assigned_indexed_columns) {
        const Column& column = columns[column_idx];
        const std::filesystem::path idx_path = index_path(resolved->db_name, table->name(), column.name);

        if (column.type == "INT" || column.type == "BOOL") {
            IndexManager<int> index(idx_path.string());
            std::unordered_set<int> seen_keys;
            seen_keys.reserve(pending.size());
            for (const auto& update : pending) {
                const auto& old_value = update.old_row[column_idx];
                const auto& new_value = update.new_row[column_idx];
                if (old_value == new_value) continue;
                if (!new_value.has_value()) {
                    return fail("Constraint error: indexed column `" + column.name + "` cannot be NULL");
                }

                int new_key = 0;
                if (column.type == "INT") {
                    if (!parse_int_strict(*new_value, new_key)) {
                        return fail("Type error: value `" + *new_value + "` for column `" + column.name + "` is not INT");
                    }
                } else {
                    bool parsed = false;
                    if (!parse_bool_strict(*new_value, parsed)) {
                        return fail("Type error: value `" + *new_value + "` for column `" + column.name + "` is not BOOL");
                    }
                    new_key = parsed ? 1 : 0;
                }

                if (!seen_keys.insert(new_key).second) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(new_key) +
                                "` for column `" + column.name + "` in UPDATE result");
                }
                Rid existing{};
                if (index.find(new_key, existing) && existing != update.rid) {
                    return fail("Constraint error: duplicate INDEXED key `" + std::to_string(new_key) +
                                "` for column `" + column.name + "`");
                }
            }
            continue;
        }

        if (column.type == "STRING") {
            IndexManager<StringIndexKey> index(idx_path.string());
            std::unordered_set<std::string> seen_keys;
            seen_keys.reserve(pending.size());
            for (const auto& update : pending) {
                const auto& old_value = update.old_row[column_idx];
                const auto& new_value = update.new_row[column_idx];
                if (old_value == new_value) continue;
                if (!new_value.has_value()) {
                    return fail("Constraint error: indexed column `" + column.name + "` cannot be NULL");
                }
                if (!seen_keys.insert(*new_value).second) {
                    return fail("Constraint error: duplicate INDEXED key `" + *new_value +
                                "` for column `" + column.name + "` in UPDATE result");
                }

                StringIndexKey key{};
                if (!StringIndexKey::from_string(*new_value, key)) {
                    return fail("Constraint error: value for INDEXED STRING column `" + column.name +
                                "` is too long for the index key");
                }
                Rid existing{};
                if (index.find(key, existing) && existing != update.rid) {
                    return fail("Constraint error: duplicate INDEXED key `" + *new_value +
                                "` for column `" + column.name + "`");
                }
            }
            continue;
        }

        return fail("Semantic error: indexed column `" + column.name + "` has unsupported type `" + column.type + "`");
    }

    std::vector<IndexMutation> index_changes;
    index_changes.reserve(pending.size() * assigned_indexed_columns.size());
    for (const auto& update : pending) {
        for (std::size_t column_idx : assigned_indexed_columns) {
            if (update.old_row[column_idx] == update.new_row[column_idx]) {
                continue;
            }
            index_changes.push_back(IndexMutation{
                column_idx,
                update.rid,
                update.old_row[column_idx],
                update.new_row[column_idx]
            });
        }
    }

    std::unordered_map<int, Page> original_pages;
    std::unordered_map<int, Page> modified_pages;
    for (const auto& update : pending) {
        Page* page = nullptr;
        if (!ensure_page_in_batch(manager, update.rid.page_id, original_pages, modified_pages, page)) {
            return fail("Runtime error: cannot read table page " + std::to_string(update.rid.page_id) +
                        " for UPDATE `" + resolved->db_name + "." + table->name() + "`");
        }

        Page& page_ref = *page;
        PageHeader ph = page_ref.read_header();
        if (update.rid.slot_id < 0 || update.rid.slot_id >= ph.slots_count) {
            return fail("Runtime error: invalid record slot while updating `" +
                        resolved->db_name + "." + table->name() + "`");
        }
        Slot slot = page_ref.read_slot(update.rid.slot_id);
        if (slot.size <= 0) {
            return fail("Runtime error: cannot update deleted record in `" +
                        resolved->db_name + "." + table->name() + "`");
        }

        Record record(update.new_row);
        std::vector<unsigned char> bytes = serialize_record(record, columns.size());
        const int record_size = static_cast<int>(bytes.size());

        if (record_size <= slot.size) {
            std::memcpy(page_ref.data().data() + slot.offset, bytes.data(), bytes.size());
            slot.size = record_size;
            page_ref.write_slot(update.rid.slot_id, slot);
            continue;
        }

        const int slot_dir_end = static_cast<int>(sizeof(PageHeader) + ph.slots_count * sizeof(Slot));
        if (ph.free_end - slot_dir_end < record_size) {
            return fail("Runtime error: updated record does not fit into its table page for `" +
                        resolved->db_name + "." + table->name() + "`");
        }
        ph.free_end -= record_size;
        std::memcpy(page_ref.data().data() + ph.free_end, bytes.data(), bytes.size());
        slot.offset = ph.free_end;
        slot.size = record_size;
        page_ref.write_slot(update.rid.slot_id, slot);
        page_ref.write_header(ph);
    }

    std::size_t applied_changes = 0;
    const auto resolve_idx_path = [&](std::size_t column_idx) {
        return index_path(resolved->db_name, table->name(), columns[column_idx].name);
    };
    if (!apply_index_mutations_with_rollback(index_changes, columns, resolve_idx_path, applied_changes)) {
        return fail("Runtime error: cannot update one or more indexes for `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    if (!write_modified_pages_with_rollback(manager,
                                            original_pages,
                                            modified_pages,
                                            [&]() {
                                                rollback_applied_index_mutations(index_changes,
                                                                                 applied_changes,
                                                                                 columns,
                                                                                 resolve_idx_path);
                                            })) {
        return fail("Runtime error: cannot write updated table pages for `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    return true;
}

bool Executor::execute_delete(const DeleteStmt& stmt)
{
   std::optional<ResolvedTableName> resolved = resolve_table_name(stmt.table_name);
    if (!resolved.has_value()) {
        return fail("Semantic error: no active database selected for DELETE from `" + stmt.table_name + "`");
    }

    Database* db = find_database(resolved->db_name);
    if (db == nullptr) {
        return fail("Semantic error: database `" + resolved->db_name + "` does not exist");
    }

    Table* table = find_table(*db, resolved->table_name);
    if (table == nullptr) {
        return fail("Semantic error: table `" + resolved->db_name + "." + resolved->table_name + "` does not exist");
    }
    
    const auto& columns = table->columns();

    std::vector<std::pair<Rid, row_values_type>> matched_rows;
    if (!collect_matching_rows(resolved->db_name, *table, stmt.where, matched_rows)) {
        return fail(_last_error.empty()
            ? "Runtime error: cannot read matching rows from `" + resolved->db_name + "." + table->name() + "`"
            : _last_error);
    }
    if (matched_rows.empty()) {
        return true;
    }

    std::vector<IndexMutation> index_changes;
    index_changes.reserve(matched_rows.size() * columns.size());
    for (const auto& matched : matched_rows) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (!columns[i].indexed) {
                continue;
            }
            const auto& value = matched.second[i];
            if (!value.has_value()) {
                return fail("Runtime error: indexed column `" + columns[i].name +
                            "` contains NULL while deleting from `" + resolved->db_name + "." + table->name() + "`");
            }
            index_changes.push_back(IndexMutation{i, matched.first, value, std::nullopt});
        }
    }

    std::size_t applied_changes = 0;
    const auto resolve_idx_path = [&](std::size_t column_idx) {
        return index_path(resolved->db_name, table->name(), columns[column_idx].name);
    };
    if (!apply_index_mutations_with_rollback(index_changes, columns, resolve_idx_path, applied_changes)) {
        return fail("Runtime error: cannot update one or more indexes for DELETE from `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    TablePageManager manager(table_path(resolved->db_name, table->name()).string());
    std::unordered_map<int, Page> original_pages;
    std::unordered_map<int, Page> modified_pages;
    for (const auto& matched : matched_rows) {
        const Rid& rid = matched.first;
        Page* page = nullptr;
        if (!ensure_page_in_batch(manager, rid.page_id, original_pages, modified_pages, page)) {
            rollback_applied_index_mutations(index_changes, applied_changes, columns, resolve_idx_path);
            return fail("Runtime error: cannot read table page " + std::to_string(rid.page_id) +
                        " for DELETE from `" + resolved->db_name + "." + table->name() + "`");
        }

        if (!page->remove_record(rid.slot_id)) {
            rollback_applied_index_mutations(index_changes, applied_changes, columns, resolve_idx_path);
            return fail("Runtime error: cannot remove record slot " + std::to_string(rid.slot_id) +
                        " from `" + resolved->db_name + "." + table->name() + "`");
        }
    }

    if (!write_modified_pages_with_rollback(manager,
                                            original_pages,
                                            modified_pages,
                                            [&]() {
                                                rollback_applied_index_mutations(index_changes,
                                                                                 applied_changes,
                                                                                 columns,
                                                                                 resolve_idx_path);
                                            })) {
        return fail("Runtime error: cannot write table pages for DELETE from `" +
                    resolved->db_name + "." + table->name() + "`");
    }

    return true;
}
}
