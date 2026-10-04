#pragma once

#include <ctpg/parse-table.hpp>

// The user must define:
//   CTPG_TABLE_DATA  — name of the array symbol
//
// Example:
//   #define CTPG_TABLE_DATA  mylang_table_data
//   #include <ctpg/parse-table/static.hpp>

#ifndef CTPG_TABLE_DATA
#error "CTPG_TABLE_DATA must be defined before including static.hpp"
#endif

#define CAT_(a, b)      a##b
#define CAT(a, b)       CAT_(a, b)
#define CTPG_TABLE_SIZE CAT(CTPG_TABLE_DATA, _size)

extern const unsigned char CTPG_TABLE_DATA[];
extern const size_t CTPG_TABLE_SIZE;

namespace ctpg {

inline parse_table_view make_parse_table_view() {
    return parse_table_view{CTPG_TABLE_DATA, CTPG_TABLE_SIZE};
}

} // namespace ctpg

#undef CAT
#undef CAT_
#undef CTPG_TABLE_SIZE
