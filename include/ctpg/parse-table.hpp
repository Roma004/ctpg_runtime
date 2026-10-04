#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <memory>

namespace ctpg {

class parse_table_view
{
public:
    enum class entry_kind : uint8_t
    {
        error                      = 0,
        success                    = 1,
        shift                      = 2,
        shift_error_recovery_token = 3,
        reduce                     = 4,
        rr_conflict                = 5,
    };

    struct entry
    {
        entry_kind kind = entry_kind::error;
        uint16_t   arg  = 0xffffu;
    };

    static constexpr uint32_t magic       = 0x47505443u;   // "CTPG" LE
    static constexpr uint32_t version     = 1u;
    static constexpr size_t   header_size = 40;
    static constexpr size_t   entry_size  = 4;

    parse_table_view() = default;

    parse_table_view(const uint8_t* data, size_t size)
        : data_(data), size_(size)
    {
        if (size_ < header_size || read_u32(0) != magic || read_u32(4) != version)
        {
            data_ = nullptr;
            return;
        }

        state_count_  = read_u32(8);
        symbol_count_ = read_u32(12);
        term_count_   = read_u32(16);
        nterm_count_  = read_u32(20);
        rule_count_   = read_u32(24);
        grammar_hash_ = read_u64(32);

        if (symbol_count_ != term_count_ + nterm_count_)
            data_ = nullptr;
        else if (size_ < compute_size(state_count_, symbol_count_))
            data_ = nullptr;
    }

    bool valid() const { return data_ != nullptr; }

    uint32_t state_count()  const { return state_count_; }
    uint32_t symbol_count() const { return symbol_count_; }
    uint32_t term_count()   const { return term_count_; }
    uint32_t nterm_count()  const { return nterm_count_; }
    uint32_t rule_count()   const { return rule_count_; }
    uint64_t grammar_hash() const { return grammar_hash_; }

    const uint8_t* data() const { return data_; }
    size_t         size() const { return size_; }

    bool matches(uint64_t expected) const
    {
        return data_ && grammar_hash_ == expected;
    }

    entry action(uint32_t state, uint32_t symbol) const
    {
        const uint8_t* p = data_ + header_size
                         + (static_cast<size_t>(state) * symbol_count_ + symbol) * entry_size;
        entry e;
        e.kind = static_cast<entry_kind>(p[0]);
        e.arg  = static_cast<uint16_t>(p[2] | (p[3] << 8));
        return e;
    }

    static size_t compute_size(uint32_t states, uint32_t symbols)
    {
        return header_size + static_cast<size_t>(states) * symbols * entry_size;
    }

private:
    uint32_t read_u32(size_t off) const
    {
        uint32_t v; std::memcpy(&v, data_ + off, sizeof(v)); return v;
    }
    uint64_t read_u64(size_t off) const
    {
        uint64_t v; std::memcpy(&v, data_ + off, sizeof(v)); return v;
    }

    const uint8_t* data_         = nullptr;
    size_t         size_         = 0;
    uint32_t       state_count_  = 0;
    uint32_t       symbol_count_ = 0;
    uint32_t       term_count_   = 0;
    uint32_t       nterm_count_  = 0;
    uint32_t       rule_count_   = 0;
    uint64_t       grammar_hash_ = 0;
};


class parse_table
{
public:
    parse_table() = default;
    parse_table(parse_table&&) = default;
    parse_table& operator=(parse_table&&) = default;
    parse_table(const parse_table&) = delete;
    parse_table& operator=(const parse_table&) = delete;

    parse_table_view view() const
    {
        return parse_table_view{ blob_.data(), blob_.size() };
    }

    const std::vector<uint8_t>& blob() const { return blob_; }

    void save(const char* path) const
    {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) throw std::runtime_error(std::string("cannot open ") + path);
        size_t n = std::fwrite(blob_.data(), 1, blob_.size(), f);
        std::fclose(f);
        if (n != blob_.size())
            throw std::runtime_error(std::string("write failed: ") + path);
    }

    static std::unique_ptr<parse_table> load(const char* path)
    {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) throw std::runtime_error(std::string("cannot open ") + path);
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);

        auto tbl = std::make_unique<parse_table>();
        tbl->blob_.resize(static_cast<size_t>(sz));
        size_t n = std::fread(tbl->blob_.data(), 1, sz, f);
        std::fclose(f);
        if (n != static_cast<size_t>(sz))
            throw std::runtime_error(std::string("read failed: ") + path);
        if (!tbl->view().valid())
            throw std::runtime_error("invalid parse table file");
        return tbl;
    }

    static std::unique_ptr<parse_table> from_blob(std::vector<uint8_t> blob)
    {
        auto tbl = std::make_unique<parse_table>();
        tbl->blob_ = std::move(blob);
        if (!tbl->view().valid())
            throw std::runtime_error("invalid parse table blob");
        return tbl;
    }

    static std::unique_ptr<parse_table> create(
        uint64_t grammar_hash,
        uint32_t state_count,
        uint32_t symbol_count,
        uint32_t term_count,
        uint32_t nterm_count,
        uint32_t rule_count,
        const std::vector<parse_table_view::entry>& entries)
    {
        auto tbl = std::make_unique<parse_table>();
        size_t total = parse_table_view::compute_size(state_count, symbol_count);
        tbl->blob_.assign(total, 0);

        uint8_t* p = tbl->blob_.data();
        write_u32(p +  0, parse_table_view::magic);
        write_u32(p +  4, parse_table_view::version);
        write_u32(p +  8, state_count);
        write_u32(p + 12, symbol_count);
        write_u32(p + 16, term_count);
        write_u32(p + 20, nterm_count);
        write_u32(p + 24, rule_count);
        write_u32(p + 28, 0);
        write_u64(p + 32, grammar_hash);

        uint8_t* ep = p + parse_table_view::header_size;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            ep[i * 4 + 0] = static_cast<uint8_t>(entries[i].kind);
            ep[i * 4 + 1] = 0;
            ep[i * 4 + 2] = static_cast<uint8_t>(entries[i].arg & 0xff);
            ep[i * 4 + 3] = static_cast<uint8_t>((entries[i].arg >> 8) & 0xff);
        }
        return tbl;
    }

private:
    static void write_u32(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
    static void write_u64(uint8_t* p, uint64_t v)
    {
        for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
    }

    std::vector<uint8_t> blob_;
};

};
