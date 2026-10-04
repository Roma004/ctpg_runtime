#pragma once

#include <ctpg/parse-table.hpp>

// The user must define:
//   CTPG_TABLE_DATA  — fully qualified name of the array symbol
//
// Example:
//   #define CTPG_TABLE_DATA  mylang::mylang_table_data
//   #include <ctpg/table_view/static_factory.hpp>

#ifndef CTPG_TABLE_DATA
#  error "CTPG_TABLE_DATA must be defined before including static_factory.hpp"
#endif

#define CTPG_TABLE_SIZE(data) data##_size

namespace ctpg
{

inline parse_table_view make_parse_table_view()
{
    return parse_table_view{ CTPG_TABLE_DATA, CTPG_TABLE_SIZE(CTPG_TABLE_DATA) };
}

} // namespace ctpg
