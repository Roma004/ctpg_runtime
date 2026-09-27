#ifndef CTPG_H
#define CTPG_H

#include <utility>
#include <type_traits>
#include <array>
#include <cstdint>
#include <tuple>
#include <algorithm>
#include <vector>
#include <optional>
#include <variant>
#include <string_view>
#include <string>
#include <iostream>
#include <stdexcept>
#include <ostream>
#include <regex>
#include <cstring>
#include <cstdio>
#include <memory>

namespace ctpg
{

namespace hash_detail
{
    constexpr uint64_t fnv_offset = 0xcbf29ce484222325ull;
    constexpr uint64_t fnv_prime  = 0x100000001b3ull;

    constexpr uint64_t fnv1a_byte(uint64_t h, uint8_t b)
    {
        return (h ^ b) * fnv_prime;
    }

    template<typename UInt>
    constexpr uint64_t fnv1a_uint(uint64_t h, UInt v)
    {
        static_assert(std::is_unsigned_v<UInt>);
        for (size_t i = 0; i < sizeof(UInt); ++i)
        {
            h = fnv1a_byte(h, static_cast<uint8_t>(v & 0xff));
            v >>= 8;
        }
        return h;
    }

    constexpr uint64_t fnv1a_str(uint64_t h, const char* s)
    {
        if (!s) return fnv1a_byte(h, 0xff);
        while (*s) { h = fnv1a_byte(h, static_cast<uint8_t>(*s)); ++s; }
        return fnv1a_byte(h, 0);
    }
}

using size_t = std::size_t;
using size8_t = std::uint8_t;
using size16_t = std::uint16_t;
using size32_t = std::uint32_t;

template<size_t N>
using str_table = const char* [N];

constexpr size_t uninitialized = size_t(-1);
constexpr size16_t uninitialized16 = size16_t(-1);
constexpr size32_t uninitialized32 = size32_t(-1);

namespace meta
{
    template<typename T>
    struct contains_type
    {};

    template<typename... T>
    struct unique_type_list : contains_type<T>...
    {
        using variant_type = std::variant<T...>;

        template<typename New>
        struct insert
        {
            static unique_type_list<T...> foo(contains_type<New>*);
            static unique_type_list<T..., New> foo(...);

            using type = decltype(foo(std::declval<unique_type_list<T...>*>()));
        };

        template<typename New>
        typename insert<New>::type operator + (New);
    };

    template<typename... T>
    struct unique_types_variant
    {
        using unique_list = decltype((std::declval<unique_type_list<>>() + ... + std::declval<T>()));
        using type = typename unique_list::variant_type;
    };

    template<typename... T>
    using unique_types_variant_t = typename unique_types_variant<T...>::type;

    template<typename... T>
    constexpr int sum_size = (0 + ... + sizeof(T));

    template<typename T>
    constexpr size_t distinct_values_count = 1 << (sizeof(T) * 8);

    constexpr size_t distinct_chars_count = distinct_values_count<char>;

    template<size_t First, size_t... Rest>
    struct max
    {
        static const size_t value = std::max(First, max<Rest...>::value);
    };

    template<size_t X>
    struct max<X>
    {
        static const size_t value = X;
    };

    template<size_t... X>
    constexpr size_t max_v = max<X...>::value;

    template<size_t... X>
    constexpr size_t count_zeros = (0 + ... + (X == 0 ? 1 : 0));
}

namespace stdex
{
    class dyn_bitset
    {
    public:
        dyn_bitset() = default;
        explicit dyn_bitset(size_t n) : data_((n + 63) >> 6, 0), bit_count_(n) {}

        void resize(size_t n) { data_.assign((n + 63) >> 6, 0); bit_count_ = n; }

        void set(size_t i)        { data_[i >> 6] |=  uint64_t(1) << (i & 63); }
        void reset(size_t i)      { data_[i >> 6] &= ~(uint64_t(1) << (i & 63)); }
        bool test(size_t i) const { return (data_[i >> 6] >> (i & 63)) & 1; }

        bool add(const dyn_bitset& o)
        {
            bool changed = false;
            for (size_t k = 0; k < data_.size(); ++k)
            {
                uint64_t nv = data_[k] | o.data_[k];
                if (nv != data_[k]) { data_[k] = nv; changed = true; }
            }
            return changed;
        }

        bool operator==(const dyn_bitset& o) const { return data_ == o.data_; }
        bool operator!=(const dyn_bitset& o) const { return !(*this == o); }
        size_t size() const { return bit_count_; }

        const std::vector<uint64_t>& raw() const { return data_; }
        std::vector<uint64_t>&       raw()       { return data_; }

    private:
        std::vector<uint64_t> data_;
        size_t bit_count_ = 0;
    };

    template<typename T>
    struct is_cvector_compatible : std::bool_constant<std::is_default_constructible_v<T> && std::is_trivially_destructible_v<T>>
    {};

    template<typename T, std::size_t N, typename = void>
    class cvector
    {};

    template<typename T, std::size_t N>
    class cvector<T, N, std::enable_if_t<is_cvector_compatible<T>::value>>
    {
    public:
        using size_type = std::size_t;

        constexpr cvector() :
            the_data{}, current_size(0)
        {}

        constexpr cvector(const T& arg, size_t count):
            the_data{}, current_size(0)
        {
            for (size_t i = 0; i < count; ++i)
                push_back(arg);
        }

    private:
        template<typename Derived>
        struct iterator_base
        {
            using it_type = Derived;
            constexpr it_type* cast() { return static_cast<it_type*>(this); }
            constexpr const it_type* cast() const { return static_cast<const it_type*>(this); }

            constexpr bool operator == (const it_type& other) const { return cast()->ptr == other.ptr; }
            constexpr bool operator != (const it_type& other) const { return cast()->ptr != other.ptr; }
            constexpr it_type operator - (size_type amount) const { return it_type{ cast()->ptr - amount }; }
            constexpr size_type operator - (const it_type& other) const { return size_type(cast()->ptr - other.ptr); }
            constexpr it_type operator + (size_type amount) const { return it_type{ cast()->ptr + amount }; }
            constexpr it_type operator ++(int) { it_type it{ cast()->ptr }; ++(cast()->ptr); return it; }
            constexpr it_type& operator ++() { ++(cast()->ptr); return *cast(); }
            constexpr bool operator > (const it_type& other) const { return cast()->ptr > other.ptr; }
            constexpr bool operator < (const it_type& other) const { return cast()->ptr < other.ptr; }
        };

    public:
        struct iterator : iterator_base<iterator>
        {
            T* ptr;
            constexpr iterator(T* ptr) : ptr(ptr) {}
            constexpr T& operator *() const { return *ptr; }
        };

        struct const_iterator : iterator_base<const_iterator>
        {
            const T* ptr;
            constexpr const_iterator(const T* ptr) : ptr(ptr) {}
            constexpr const T& operator *() const { return *ptr; }
        };

        constexpr const T* data() const { return the_data; }
        constexpr T* data() { return the_data; }
        constexpr size_type size() const { return current_size; }
        constexpr bool empty() const { return current_size == 0; }
        constexpr void reserve(size_type) const {};
        constexpr const T& operator[](size_type idx) const { return the_data[idx]; }
        constexpr T& operator[](size_type idx) { return the_data[idx]; }
        constexpr void push_back(const T& v) { the_data[current_size++] = v; }
        constexpr void emplace_back(T&& v) { the_data[current_size++] = std::move(v); }
        constexpr const T& front() const { return the_data[0]; }
        constexpr T& front() { return the_data[0]; }
        constexpr T& back() { return the_data[current_size - 1]; }
        constexpr const T& back() const { return the_data[current_size - 1]; }
        constexpr const_iterator begin() const { return const_iterator(the_data); }
        constexpr const_iterator end() const { return const_iterator(the_data + current_size); }
        constexpr iterator begin() { return iterator(the_data); }
        constexpr iterator end() { return iterator(the_data + current_size); }
        constexpr void clear() { current_size = 0; }
        constexpr void pop_back() { current_size--; }
        constexpr iterator erase(iterator first, iterator last)
        {
            if (!(first < last))
                return end();
            auto from = first < begin() ? begin() : first;
            auto to = last > end() ? end() : last;
            size_type diff = to - from;
            iterator it = to;
            while (!(it == end()))
            {
                *from = std::move(*it);
                ++from; ++it;
            }
            current_size -= diff;

            return end();
        }

    private:
        T the_data[N];
        size_type current_size;
    };

    template<std::size_t N>
    class cbitset
    {
    public:
        using size_type = std::size_t;
        using underlying_type = std::uint64_t;

        constexpr cbitset& set(size_type idx)
        {
            check_idx(idx);
            data[idx / underlying_size] |= (underlying_type(1) << (idx % underlying_size));
            return *this;
        }

        constexpr cbitset& set(size_type idx, bool value)
        {
            check_idx(idx);
            data[idx / underlying_size] ^= (-!!value ^ data[idx / underlying_size]) & (underlying_type(1) << (idx % underlying_size));
            return *this;
        }

        constexpr cbitset& reset(size_type idx)
        {
            check_idx(idx);
            data[idx / underlying_size] &= ~(underlying_type(1) << (idx % underlying_size));
            return *this;
        }

        constexpr cbitset& flip(size_type idx)
        {
            check_idx(idx);
            data[idx / underlying_size] ^= (underlying_type(1) << (idx % underlying_size));
            return *this;
        }

        constexpr bool test(size_type idx) const
        {
            check_idx(idx);
            return (data[idx / underlying_size] >> (idx % underlying_size)) & underlying_type(1);
        }

        constexpr cbitset& flip()
        {
            for (auto& d : data)
                d = ~d;
            return *this;
        }

        constexpr cbitset& set()
        {
            for (auto& d : data)
                d = underlying_type(-1);
            return *this;
        }

        constexpr cbitset& reset()
        {
            for (auto& d : data)
                d = underlying_type(0);
            return *this;
        }

        constexpr size_type size() const
        {
            return N;
        }

        constexpr void add(const cbitset<N>& other)
        {
            for (auto i = 0u; i < underlying_count; ++i)
                data[i] |= other.data[i];
        }

        constexpr bool operator == (const cbitset<N>& other) const
        {
            for (auto i = 0u; i < underlying_count; ++i)
                if (data[i] != other.data[i])
                    return false;
            return true;
        }

    private:
        constexpr void check_idx(size_type idx) const
        {
            if (idx >= N)
                throw std::runtime_error("Index access out of range");
        }

        static const size_type underlying_size = sizeof(underlying_type) * 8;
        static const size_type underlying_count = (N / underlying_size) + ((N % underlying_size) ? 1 : 0);
        underlying_type data[underlying_count] = {};
    };

    template<typename T>
    struct is_cqueue_compatible : std::bool_constant<std::is_default_constructible_v<T> && std::is_trivially_destructible_v<T>>
    {};

    template<typename T, std::size_t N, typename = void>
    class cqueue
    {};

    template<typename T, std::size_t N>
    class cqueue<T, N, std::enable_if_t<is_cqueue_compatible<T>::value>>
    {
    public:
        constexpr cqueue():
            data{}, start{0}, end{0}, size_{0}
        {}

        constexpr void push(const T& v)
        {
            if (size_ >= N)
                throw std::runtime_error("Pushing out of range");

            data[end++] = v;
            if (end == N)
                end = 0;
            size_++;
        }

        constexpr void pop()
        {
            if (empty())
                throw std::runtime_error("Pop on empty");

            start++;
            if (start == N)
                start = 0;
            size_--;
        }

        constexpr T& top()
        {
            if (empty())
                throw std::runtime_error("Top on empty");
            return data[start];
        }

        constexpr const T& top() const
        {
            return std::as_const(*this).top();
        }

        constexpr bool empty() const
        {
            return size_ == 0;
        }

        constexpr std::size_t size() const
        {
            return size_;
        }

    private:
        T data[N];
        std::size_t start;
        std::size_t end;
        std::size_t size_;
    };

    template<typename Container, typename Pred>
    constexpr Container& sort(Container& c, Pred p)
    {
        bool swap = true;
        while (swap)
        {
            swap = false;
            for (auto i = 0u; i < std::size(c) - 1; i++)
            {
                if (p(c[i + 1], c[i]))
                {
                    auto x = c[i];
                    c[i] = c[i + 1];
                    c[i + 1] = x;
                    swap = true;
                }
            }
        }
        return c;
    }
}

namespace utils
{
    struct no_stream
    {
        template<typename T>
        constexpr const no_stream& operator <<(T&&) const { return *this; }
    };

    template<typename T, typename... Args>
    constexpr T construct_default(Args...)
    {
        return T();
    }

    template<typename T, size_t... I>
    constexpr void copy_array(T *a1, const T* a2, std::index_sequence<I...>)
    {
        (void(a1[I] = a2[I]), ...);
    }

    constexpr size_t char_to_idx(char c)
    {
        return static_cast<size_t>(static_cast<unsigned char>(c)) & 0xff;
    }

    constexpr char idx_to_char(size_t idx)
    {
        return static_cast<char>(static_cast<unsigned char>(idx & 0xff));
    }

    constexpr bool is_printable(char c)
    {
        return c >= 0x20 && c <= 0x7e;
    }

    constexpr bool is_hex_digit(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    constexpr bool is_dec_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    class char_names
    {
    public:
        const static size_t name_size = 5;

        constexpr char_names()
        {
            for (size_t i = 0; i < meta::distinct_chars_count; ++i)
            {
                char d[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
                if (idx_to_char(i) > 32 && idx_to_char(i) < 127)
                {
                    arr[i][0] = idx_to_char(i);
                    arr[i][1] = 0;
                }
                else
                {
                    arr[i][0] = '\\';
                    arr[i][1] = 'x';
                    arr[i][2] = d[i / 16];
                    arr[i][3] = d[i % 16];
                    arr[i][4] = 0;
                }
            }
        }

        constexpr const char* name(char c) const { return arr[char_to_idx(c)]; }

    private:
        char arr[meta::distinct_chars_count][name_size] = {};
    };

    constexpr char_names c_names = {};

    constexpr bool str_equal(const char* str1, const char* str2)
    {
        if ((str1 == nullptr) || (str2 == nullptr))
            throw std::runtime_error("null pointers not allowed here");
        while (*str1 == *str2)
        {
            if (*str1 == 0)
                return true;
            str1++; str2++;
        }
        return false;
    }

    template<size_t N>
    constexpr size_t find_str(const str_table<N>& table, const char* str)
    {
        size_t res = 0;
        for (const auto& n : table)
        {
            if (str_equal(n, str))
                return res;
            res++;
        }
        if (res == N)
            throw std::runtime_error("string not found");
        return uninitialized;
    }

    constexpr size_t find_char(char c, const char* str)
    {
        size_t i = 0;
        while (*str)
        {
            if (*str == c)
                return i;
            str++; i++;
        }
        return uninitialized;
    }

    constexpr std::size_t str_len(const char* str)
    {
        std::size_t i = 0;
        const char* p = str;
        while (*p) { ++i; ++p; }
        return i;
    }

    struct slice
    {
        size32_t start;
        size32_t n;
    };

    template<typename T>
    struct fake_table
    {
        T operator[](size_t) const { return val; }
        T val;
    };

    constexpr std::string_view pass_sv(const std::string_view& sv)
    {
        return sv;
    }

    constexpr char first_sv_char(const std::string_view& sv)
    {
        return sv[0];
    }
}

namespace ftors
{
    template<size_t>
    struct ignore
    {
        template<typename T>
        constexpr ignore(T&&){}
    };

    template<size_t X, typename = std::make_index_sequence<X - 1>>
    class element
    {};

    template<size_t X, size_t... I>
    class element<X, std::index_sequence<I...>>
    {
    public:
        template<typename First, typename... Rest>
        constexpr decltype(auto) operator ()(ignore<I>..., First&& arg, Rest&&...) const
        {
            return std::forward<First>(arg);
        }
    };

    constexpr element<1> _e1;
    constexpr element<2> _e2;
    constexpr element<3> _e3;
    constexpr element<4> _e4;
    constexpr element<5> _e5;
    constexpr element<6> _e6;
    constexpr element<7> _e7;
    constexpr element<8> _e8;
    constexpr element<9> _e9;

    template<typename T>
    class val
    {
    public:
        constexpr val(T&& v):
            v(std::forward<T>(v))
        {}

        template<typename... Args>
        constexpr auto operator ()(Args&&...) const
        {
            return v;
        }

    private:
        T v;
    };

    template<typename T>
    val(T&&) -> val<std::decay_t<T>>;

    template<typename T>
    class create
    {
    public:
        template<typename... Args>
        constexpr auto operator ()(Args&&...) const
        {
            return T{};
        }
    };

    template<typename T, std::size_t FromIdx = 1, typename = std::make_index_sequence<FromIdx - 1>>
    struct construct
    {};

    template<
        typename T,
        std::size_t FromIdx,
        std::size_t... Skip
    >
    struct construct<
        T,
        FromIdx,
        std::index_sequence<Skip...>>
    {
        template<typename Arg, typename... Rest>
        constexpr auto operator()(ignore<Skip>..., Arg&& arg, Rest&&...) const
        {
            return T{std::forward<Arg>(arg)};
        }
    };

    template<
        std::size_t ContIdx = 1,
        std::size_t ArgIdx = 2,
        typename = std::make_index_sequence<std::min(ContIdx, ArgIdx) - 1>,
        typename = std::make_index_sequence<std::max(ContIdx, ArgIdx) - std::min(ContIdx, ArgIdx) - 1>,
        bool container_first = ContIdx < ArgIdx
    >
    struct emplace_back
    {};

    template<
        std::size_t ContIdx,
        std::size_t ArgIdx,
        std::size_t... Skip1,
        std::size_t... Skip2
    >
    struct emplace_back<
        ContIdx,
        ArgIdx,
        std::index_sequence<Skip1...>,
        std::index_sequence<Skip2...>,
        true>
    {
        template<typename Container, typename Arg, typename... Rest>
        constexpr decltype(auto) operator()(ignore<Skip1>..., Container &&container, ignore<Skip2>..., Arg&& arg, Rest&&...) const
        {
            container.emplace_back(std::move(arg));
            return std::move(container);
        }
    };

    template<
        std::size_t ContIdx,
        std::size_t ArgIdx,
        std::size_t... Skip1,
        std::size_t... Skip2
    >
    struct emplace_back<
        ContIdx,
        ArgIdx,
        std::index_sequence<Skip1...>,
        std::index_sequence<Skip2...>,
        false>
    {
        template<typename Container, typename Arg, typename... Rest>
        constexpr decltype(auto) operator()(ignore<Skip1>..., Arg&& arg, ignore<Skip2>..., Container &&container, Rest&&...) const
        {
            container.emplace_back(std::move(arg));
            return std::move(container);
        }
    };

    template<
        std::size_t ContIdx = 1,
        std::size_t ArgIdx = 2,
        typename = std::make_index_sequence<std::min(ContIdx, ArgIdx) - 1>,
        typename = std::make_index_sequence<std::max(ContIdx, ArgIdx) - std::min(ContIdx, ArgIdx) - 1>,
        bool container_first = ContIdx < ArgIdx
    >
    struct push_back
    {};

    template<
        std::size_t ContIdx,
        std::size_t ArgIdx,
        std::size_t... Skip1,
        std::size_t... Skip2
    >
    struct push_back<
        ContIdx,
        ArgIdx,
        std::index_sequence<Skip1...>,
        std::index_sequence<Skip2...>,
        true>
    {
        template<typename Container, typename Arg, typename... Rest>
        constexpr decltype(auto) operator()(ignore<Skip1>..., Container &&container, ignore<Skip2>..., const Arg& arg, Rest&&...) const
        {
            container.push_back(arg);
            return std::move(container);
        }
    };

    template<
        std::size_t ContIdx,
        std::size_t ArgIdx,
        std::size_t... Skip1,
        std::size_t... Skip2
    >
    struct push_back<
        ContIdx,
        ArgIdx,
        std::index_sequence<Skip1...>,
        std::index_sequence<Skip2...>,
        false>
    {
        template<typename Container, typename Arg, typename... Rest>
        constexpr decltype(auto) operator()(ignore<Skip1>..., const Arg& arg, ignore<Skip2>..., Container &&container, Rest&&...) const
        {
            container.push_back(arg);
            return std::move(container);
        }
    };
}

namespace buffers
{
    template<size_t N>
    class cstring_buffer
    {
    public:
        template<size_t N1>
        constexpr cstring_buffer(const char(&source)[N1])
        {
            utils::copy_array(data, source, std::make_index_sequence<N1>{});
        }

        struct iterator
        {
            const char* ptr;

            constexpr char operator *() const { return *ptr; }
            constexpr iterator& operator ++() { ++ptr; return *this; }
            constexpr iterator operator ++(int) { iterator i(*this); ++ptr; return i; }
            constexpr bool operator == (const iterator& other) const { return ptr == other.ptr; }
            constexpr bool operator != (const iterator& other) const { return ptr != other.ptr; }
            constexpr iterator& operator += (size_t len) { ptr += len; return *this; }
            constexpr iterator operator + (size_t len) { iterator i(*this); i.ptr += len; return i; }
        };

        constexpr iterator begin() const { return iterator{ data }; }
        constexpr iterator end() const { return iterator{ data + N - 1 }; }
        constexpr std::string_view get_view(iterator start, iterator end) const { return std::string_view(start.ptr, end.ptr - start.ptr); }

    private:
        char data[N] = { 0 };
    };

    template<size_t N>
    cstring_buffer(const char(&)[N])->cstring_buffer<N>;

    class string_buffer
    {
    public:
        string_buffer(std::string&& str):
            str(std::move(str))
        {}

        string_buffer(const char* str):
            str(str)
        {}

        auto begin() const { return str.cbegin(); }
        auto end() const { return str.cend(); }

        using iterator = std::string::const_iterator;

        std::string_view get_view(iterator start, iterator end) const
        {
            return std::string_view(str.data() + (start - str.begin()), end - start);
        }

    private:
        std::string str;
    };

    class string_view_buffer
    {
    public:
        string_view_buffer(const std::string_view& str):
            str(str)
        {}

        auto begin() const { return str.cbegin(); }
        auto end() const { return str.cend(); }

        using iterator = std::string_view::const_iterator;

        std::string_view get_view(iterator start, iterator end) const
        {
            return std::string_view(str.data() + (start - str.begin()), end - start);
        }

    private:
        std::string_view str;
    };

    template<typename Buffer>
    using iterator_t = typename Buffer::iterator;
}

template<typename ValueType>
class nterm
{
public:
    using value_type = ValueType;

    constexpr nterm(const char* name) :
        name(name)
    {
        if (name[0] == 0)
            throw std::runtime_error("empty name not allowed");
    }

    constexpr const char* get_name() const { return name; }

    template<typename... Args>
    constexpr auto operator()(Args&&... args) const;

private:
    const char* name;
};

enum class associativity { no_assoc, ltor, rtol };

struct source_point
{
    size32_t line = 1;
    size32_t column = 1;

    template<typename Iterator>
    constexpr void update(Iterator start, Iterator end)
    {
        while (!(start == end))
        {
            if (*start == '\n')
            {
                ++line;
                column = 1;
            }
            else
                ++column;
            ++start;
        }
    }

    friend std::ostream& operator << (std::ostream& o, const source_point& sp);
};

inline std::ostream& operator << (std::ostream& o, const source_point& sp)
{
    o << "[" << sp.line << ":" << sp.column << "]";
    return o;
}

template<typename VT>
class term_value
{
public:
    constexpr term_value(VT v, source_point sp):
        value(v), sp{ sp }
    {}

    constexpr operator VT() const { return value; }
    constexpr size32_t get_line() const { return sp.line; }
    constexpr size32_t get_column() const { return sp.column; }
    constexpr const VT& get_value() const { return value; }
    constexpr source_point get_sp() const { return sp; }

private:
    VT value;
    source_point sp;
};

class term
{
public:
    constexpr term(int precedence = 0, associativity a = associativity::no_assoc) :
        precedence(precedence), ass(a)
    {}

    constexpr associativity get_associativity() const { return ass; }
    constexpr int get_precedence() const { return precedence; }

protected:
    int precedence;
    associativity ass;
};

class char_term : public term
{
public:
    using internal_value_type = char;

    static const size_t dfa_size = 2;
    static const bool is_trivial = true;

    constexpr char_term(char c, int precedence = 0, associativity a = associativity::no_assoc):
        term(precedence, a), c(c)
    {
        utils::copy_array(id, utils::c_names.name(c), std::make_index_sequence<utils::char_names::name_size>{});
    }

    constexpr const char* get_id() const { return id; }
    constexpr const char* get_name() const { return get_id(); }
    constexpr char get_char() const { return c; }
    constexpr char get_data() const { return c; }

    constexpr const auto& get_ftor() const { return utils::first_sv_char; }

    size_t match(std::string_view sv) const
    {
        return (!sv.empty() && sv[0] == c) ? 1 : 0;
    }

private:
    char c;
    char id[utils::char_names::name_size] = {};
};

template<size_t DataSize>
class string_term : public term
{
public:
    using internal_value_type = std::string_view;
    static const size_t dfa_size = (DataSize - 1) * 2;
    static const bool is_trivial = true;

    constexpr string_term(const char (&str)[DataSize], int precedence = 0, associativity a = associativity::no_assoc):
        term(precedence, a)
    {
        utils::copy_array(data, str, std::make_index_sequence<DataSize>{});
    }

    constexpr const char* get_id() const { return data; }
    constexpr const char* get_name() const { return get_id(); }
    constexpr const auto& get_data() const { return data; }

    constexpr const auto& get_ftor() const { return utils::pass_sv; }

    size_t match(std::string_view sv) const
    {
        if (sv.size() < DataSize - 1) return 0;
        for (size_t i = 0; i < DataSize - 1; ++i)
            if (sv[i] != data[i]) return 0;
        return DataSize - 1;
    }

private:
    char data[DataSize] = {};
};

template<typename Term, typename Ftor>
class typed_term
{
public:
    using internal_value_type = std::invoke_result_t<Ftor, std::string_view>;
    static const size_t dfa_size = Term::dfa_size;
    static const bool is_trivial = Term::is_trivial;

    constexpr typed_term(Term t, Ftor f):
        term(t), ftor(f)
    {}

    constexpr const char* get_id() const { return term.get_id(); }
    constexpr const char* get_name() const { return term.get_name(); }
    constexpr decltype(auto) get_data() const { return term.get_data(); }

    constexpr associativity get_associativity() const { return term.get_associativity(); }
    constexpr int get_precedence() const { return term.get_precedence(); }

    using ftor_type = Ftor;

    constexpr const ftor_type& get_ftor() const { return ftor; }

    size_t match(std::string_view sv) const { return term.match(sv); }

private:
    Term term;
    ftor_type ftor;
};

template<typename Ftor>
class custom_term : public term
{
public:
    using internal_value_type = std::invoke_result_t<Ftor, std::string_view>;
    static const size_t dfa_size = 0;
    static const bool is_trivial = false;

    constexpr custom_term(const char* custom_name, Ftor ftor, int precedence = 0, associativity a = associativity::no_assoc):
        term(precedence, a), custom_name(custom_name), ftor(ftor)
    {}

    constexpr const char* get_id() const { return get_name(); }
    constexpr const char* get_name() const { return custom_name; }

    using ftor_type = Ftor;

    constexpr const ftor_type& get_ftor() const { return ftor; }

    size_t match(std::string_view) const { return 0; }

private:
    const char* custom_name;
    Ftor ftor;
};

struct error_recovery_token
{
    constexpr static const char* get_name() { return "<error_recovery_token>"; }
    constexpr static const char* get_id() { return get_name(); }
};

constexpr error_recovery_token error;

template<typename T, typename Enable = void>
struct value_type
{};

template<typename T>
struct value_type<T, std::enable_if_t<std::is_base_of_v<term, T>>>
{
    using type = term_value<typename T::internal_value_type>;
};

template<typename ValueType>
struct value_type<nterm<ValueType>>
{
    using type = ValueType;
};

template<typename Term, typename Ftor>
struct value_type<typed_term<Term, Ftor>>
{
    using type = term_value<typename typed_term<Term, Ftor>::internal_value_type>;
};

struct no_type {};

template<>
struct value_type<error_recovery_token>
{
    using type = no_type;
};

template<typename T>
using value_type_t = typename value_type<T>::type;

struct parse_options
{
    constexpr parse_options& set_verbose(bool val = true) { verbose = val; return *this; }
    constexpr parse_options& set_skip_whitespace(bool val = true) { skip_whitespace = val; return *this; }
    constexpr parse_options& set_skip_newline(bool val = true) { skip_newline = val; return *this; }

    bool verbose = false;
    bool skip_whitespace = true;
    bool skip_newline = true;
};

struct match_options
{
    bool verbose = false;
    constexpr match_options& set_verbose(bool val = true) { verbose = val; return *this; }
};

struct recognized_term
{
    constexpr recognized_term() = default;

    constexpr recognized_term(size16_t term_idx, size_t len):
        term_idx(term_idx), len(len)
    {}

    size16_t term_idx = uninitialized16;
    size_t len = uninitialized16;
};

namespace detail
{
    template<typename Arg>
    constexpr decltype(auto) make_term(Arg&& arg)
    {
        return std::forward<Arg>(arg);
    }

    constexpr auto make_term(char c)
    {
        return char_term(c);
    }

    template<size_t N>
    constexpr auto make_term(const char (&str)[N])
    {
        return string_term<N>(str);
    }

    template<typename ValueType>
    struct fake_root
    {
        using value_type = ValueType;

        constexpr auto operator()(const nterm<ValueType>& nt) const;

        constexpr static const char* get_name() { return "##"; };
    };

    struct eof
    {
        constexpr static const char* get_name() { return "<eof>"; }
    };

    template<bool RequiresContext, typename F, typename L, typename...R>
    class rule
    {
    public:
        using f_type = F;
        static const size_t n = sizeof...(R);

        constexpr rule(L l, std::tuple<R...> r) :
            f(nullptr), l(l), r(r), precedence(0)
        {}

        template<typename F1>
        constexpr rule(F1&& f, L l, std::tuple<R...> r) :
            f(std::move(f)), l(l), r(r), precedence(0)
        {}

        template<typename F1>
        constexpr rule(F1&& f, L l, std::tuple<R...> r, int precedence) :
            f(std::move(f)), l(l), r(r), precedence(precedence)
        {}

        constexpr auto operator[](int prec)
        {
            return rule<RequiresContext, F, L, R...>(std::move(f), l, r, prec);
        }

        template<typename F1>
        constexpr auto operator >= (F1&& f)
        {
            return rule<false, std::decay_t<F1>, L, R...>(std::move(f), l, r, precedence);
        }

        template<typename F1>
        constexpr auto operator >>= (F1&& f)
        {
            return rule<true, std::decay_t<F1>, L, R...>(std::move(f), l, r, precedence);
        }

        constexpr const F& get_f() const { return f; }
        constexpr const L& get_l() const { return l; }
        constexpr const auto& get_r() const { return r; }
        constexpr int get_precedence() const { return precedence; }

    private:
        F f;
        L l;
        std::tuple<R...> r;
        int precedence;
    };

    template<typename L, typename... R>
    rule(L l, std::tuple<R...> r) -> rule<false, std::nullptr_t, L, R...>;

    template<typename Arg>
    constexpr auto make_rule_item(Arg&& arg)
    {
        return make_term(arg);
    }

    template<typename ValueType>
    constexpr auto make_rule_item(const nterm<ValueType>& nt)
    {
        return nt;
    }

    template<typename ValueType>
    constexpr auto fake_root<ValueType>::operator()(const nterm<ValueType>& nt) const
    {
        return rule(*this, std::make_tuple(nt));
    }
}

template<typename ValueType>
template<typename... Args>
constexpr auto nterm<ValueType>::operator()(Args&&... args) const
{
    return detail::rule(
        *this,
        std::make_tuple(detail::make_rule_item(args)...)
    );
}

namespace detail
{
    constexpr size_t value_stack_initial_capacity = 1 << 10;
    constexpr size_t cursor_stack_initial_capacity = 1 << 10;

    template<typename Context, typename ValueVariantType, typename RuleTupleType, size_t RuleCount>
    struct value_reductors
    {
        constexpr value_reductors(const RuleTupleType& rule_tuple): rule_tuple(rule_tuple) {
            init_reductors(rule_tuple, std::make_index_sequence<std::tuple_size_v<RuleTupleType>>{});
        }

        constexpr ValueVariantType invoke(Context&& context, size_t i, ValueVariantType* args) const {
            return reductors[i](std::forward<Context>(context), rule_tuple, args);
        }

    private:
        template<size_t... I>
        constexpr void init_reductors(const RuleTupleType& rule_tuple, std::index_sequence<I...>)
        {
            (void(init_nth_reductor<I>(std::get<I>(rule_tuple))), ...);
        }

        template<size_t Nr, bool RequiresContext, typename F, typename L, typename... R>
        constexpr void init_nth_reductor(const detail::rule<RequiresContext, F, L, R...>&)
        {
            reductors[Nr] = &reduce_value<Nr, RequiresContext, F, value_type_t<L>, value_type_t<R>...>;
        }

        template<bool RequiresContext, typename F, typename LValueType, typename... RValueType, size_t... I>
        constexpr static LValueType reduce_value_impl([[maybe_unused]] Context&& ctx, [[maybe_unused]] const F& f, ValueVariantType* start, std::index_sequence<I...>)
        {
            if constexpr (std::is_same_v<F, std::nullptr_t>)
                return LValueType(std::get<RValueType>(std::move(*(start + I)))...);
            else
            {
                if constexpr (RequiresContext)
                    return LValueType(f(std::forward<Context>(ctx), std::get<RValueType>(std::move(*(start + I)))...));
                else
                    return LValueType(f(std::get<RValueType>(std::move(*(start + I)))...));
            }
        }

        template<size_t RuleIdx, bool RequiresContext, typename F, typename LValueType, typename... RValueType>
        constexpr static ValueVariantType reduce_value(Context&& ctx, const RuleTupleType& rules, ValueVariantType* start)
        {
            return ValueVariantType(
                reduce_value_impl<RequiresContext, F, LValueType, RValueType...>(
                    std::forward<Context>(ctx), std::get<RuleIdx>(rules).get_f(), start, std::index_sequence_for<RValueType...>{})
            );
        }

        const RuleTupleType& rule_tuple;

        using value_reductor = ValueVariantType(*)(Context&&, const RuleTupleType&, ValueVariantType*);
        value_reductor reductors[RuleCount] = {};
    };

    template<typename CursorStack, typename ValueStack, typename ErrorStream, typename Iterator, typename ValueReductors>
    struct parse_state
    {
        constexpr parse_state(
            CursorStack& cursor_stack,
            ValueStack& value_stack,
            ErrorStream& error_stream,
            parse_options options,
            Iterator buffer_begin,
            Iterator buffer_end,
            const ValueReductors& reductors):
            cursor_stack(cursor_stack),
            value_stack(value_stack),
            error_stream(error_stream),
            options(options),
            current_sp{1, 1},
            current_it(buffer_begin),
            current_end_it(buffer_begin),
            buffer_end(buffer_end),
            reductors(reductors),
            current_term_idx(uninitialized16),
            recovery_mode(false),
            consume_mode(false)
        {
            cursor_stack.reserve(cursor_stack_initial_capacity);
            value_stack.reserve(value_stack_initial_capacity);
        }

        constexpr void enter_recovery_mode() { recovery_mode = true; }
        constexpr void leave_recovery_mode() { recovery_mode = false; }
        constexpr void enter_consume_mode() { consume_mode = true; }
        constexpr void leave_consume_mode() { consume_mode = false; }
        constexpr bool in_recovery_mode() const { return recovery_mode; }
        constexpr bool in_consume_mode() const { return consume_mode; }

        using iterator = Iterator;

        CursorStack& cursor_stack;
        ValueStack& value_stack;
        ErrorStream& error_stream;
        parse_options options;
        source_point current_sp;
        iterator current_it;
        iterator current_end_it;
        iterator buffer_end;
        const ValueReductors& reductors;
        size16_t current_term_idx;
        bool recovery_mode;
        bool consume_mode;
    };

    template<typename Buffer, size_t EmptyRulesCount>
    struct parse_table_cursor_stack_type
    {
        using type = std::vector<size16_t>;
    };

    template<size_t N, size_t EmptyRulesCount>
    struct parse_table_cursor_stack_type<buffers::cstring_buffer<N>, EmptyRulesCount>
    {
        using type = stdex::cvector<size16_t, N + EmptyRulesCount + 1>;
    };

    template<typename Buffer, size_t EmptyRulesCount>
    using parse_table_cursor_stack_type_t = typename parse_table_cursor_stack_type<Buffer, EmptyRulesCount>::type;

    template<typename Buffer, size_t EmptyRulesCount, typename ValueVariantType, typename = void>
    struct parser_value_stack_type
    {
        using type = std::vector<ValueVariantType>;
    };

    template<size_t N, size_t EmptyRulesCount, typename ValueVariantType>
    struct parser_value_stack_type<
        buffers::cstring_buffer<N>,
        EmptyRulesCount,
        ValueVariantType,
        std::enable_if_t<!stdex::is_cvector_compatible<ValueVariantType>::value>
    >
    {
        using type = std::vector<ValueVariantType>;
    };

    template<size_t N, size_t EmptyRulesCount, typename ValueVariantType>
    struct parser_value_stack_type<
        buffers::cstring_buffer<N>,
        EmptyRulesCount,
        ValueVariantType,
        std::enable_if_t<stdex::is_cvector_compatible<ValueVariantType>::value>
    >
    {
        using type = stdex::cvector<ValueVariantType, N + EmptyRulesCount + 1>;
    };

    template<typename Buffer, size_t EmptyRulesCount, typename ValueVariantType>
    using parser_value_stack_type_t = typename parser_value_stack_type<Buffer, EmptyRulesCount, ValueVariantType>::type;
}

template<typename Lexer>
struct use_lexer
{
    using type = Lexer;
};

struct use_generated_lexer
{
    using type = no_type;
};

struct default_limits
{};

template<typename T, size_t SituationCount>
struct get_limits
{
    static const size_t state_count_cap = T::state_count_cap;
    static const size_t max_sit_count_per_state_cap = T::max_sit_count_per_state_cap;
};

template<size_t SituationCount>
struct get_limits<default_limits, SituationCount>
{
    static const size_t state_count_cap = SituationCount;
    static const size_t max_sit_count_per_state_cap = SituationCount;
};


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

template<typename Root, typename Terms, typename NTerms, typename Rules, typename LexerUsage, typename Limits>
class parser
{};

template<typename RootValueType, typename... Terms, typename... NTerms, typename... Rules, typename LexerUsage, typename Limits>
class parser<
    nterm<RootValueType>,
    std::tuple<Terms...>,
    std::tuple<NTerms...>,
    std::tuple<Rules...>,
    LexerUsage,
    Limits
>
{
private:
    using term_tuple_type = std::tuple<Terms...>;
    using nterm_tuple_type = std::tuple<NTerms...>;
    using rule_tuple_type = std::tuple<Rules...>;
    using root_nterm_type = nterm<RootValueType>;
    using root_value_type = RootValueType;

    static const bool generate_lexer = std::is_same_v<LexerUsage, use_generated_lexer>;
    using lexer_type = typename LexerUsage::type;

public:
    parser(
        root_nterm_type grammar_root,
        term_tuple_type terms,
        nterm_tuple_type nterms,
        rule_tuple_type&& rules,
        LexerUsage,
        Limits):
        parser(grammar_root, terms, nterms, std::move(rules))
    {}

    parser(
        root_nterm_type grammar_root,
        term_tuple_type terms,
        nterm_tuple_type nterms,
        rule_tuple_type&& rules,
        LexerUsage):
        parser(grammar_root, terms, nterms, std::move(rules))
    {}

    parser(
        root_nterm_type grammar_root,
        term_tuple_type terms,
        nterm_tuple_type nterms,
        rule_tuple_type&& rules):
        term_tuple(terms),
        nterm_tuple(nterms),
        rule_tuple(std::move(rules))
    {
        auto seq_for_terms = std::make_index_sequence<std::tuple_size_v<term_tuple_type>>{};
        analyze_nterms(std::make_index_sequence<std::tuple_size_v<nterm_tuple_type>>{});
        analyze_nterm(detail::fake_root<value_type_t<root_nterm_type>>{});
        analyze_terms(seq_for_terms);
        analyze_eof();
        analyze_error_recovery_token();
        analyze_rules(std::make_index_sequence<std::tuple_size_v<rule_tuple_type>>{}, grammar_root);
    }

    // ---- Хэш грамматики ----
    uint64_t compute_grammar_hash() const
    {
        using namespace hash_detail;
        uint64_t h = fnv_offset;

        h = fnv1a_uint(h, static_cast<uint64_t>(term_count));
        h = fnv1a_uint(h, static_cast<uint64_t>(nterm_count));
        h = fnv1a_uint(h, static_cast<uint64_t>(symbol_count));
        h = fnv1a_uint(h, static_cast<uint64_t>(rule_count));
        h = fnv1a_uint(h, static_cast<uint64_t>(situation_size));
        h = fnv1a_uint(h, static_cast<uint64_t>(max_rule_element_count));

        for (size_t i = 0; i < term_count; ++i)
            h = fnv1a_str(h, term_ids[i]);
        for (size_t i = 0; i < nterm_count; ++i)
            h = fnv1a_str(h, nterm_names[i]);

        for (size_t i = 0; i < term_count; ++i)
        {
            h = fnv1a_uint(h, static_cast<uint64_t>(gi.term_precedences[i]));
            h = fnv1a_uint(h, static_cast<uint64_t>(gi.term_associativities[i]));
        }

        for (size_t i = 0; i < rule_count; ++i)
        {
            const auto& ri = gi.rule_infos[i];
            h = fnv1a_uint(h, static_cast<uint64_t>(ri.l_idx));
            h = fnv1a_uint(h, static_cast<uint64_t>(ri.r_idx));
            h = fnv1a_uint(h, static_cast<uint64_t>(ri.r_elements));
            for (size_t j = 0; j < ri.r_elements; ++j)
            {
                const auto& s = gi.right_sides[ri.r_idx][j];
                h = fnv1a_uint(h, static_cast<uint64_t>(s.term ? 1 : 0));
                h = fnv1a_uint(h, static_cast<uint64_t>(s.idx));
            }
            h = fnv1a_uint(h, static_cast<uint64_t>(gi.rule_precedences[ri.r_idx]));
            h = fnv1a_uint(h, static_cast<uint64_t>(gi.rule_associativities[ri.r_idx]));
        }
        return h;
    }

    // ---- Построение таблицы разбора (только для генератора) ----
    std::unique_ptr<parse_table> create_table() const
    {
        std::vector<parse_table_entry> entries(state_count_cap * symbol_count);
        state_analyzer sa(gi, term_names, nterm_names, entries);
        size_t st_count = sa.analyze_states();
        entries.resize(st_count * symbol_count);

        return parse_table::create(
            compute_grammar_hash(),
            static_cast<uint32_t>(st_count),
            static_cast<uint32_t>(symbol_count),
            static_cast<uint32_t>(term_count),
            static_cast<uint32_t>(nterm_count),
            static_cast<uint32_t>(rule_count),
            entries);
    }

    template<typename Buffer>
    std::optional<root_value_type> parse(const parse_table_view& table, const Buffer& buffer) const
    {
        utils::no_stream error_stream;
        return parse(table, buffer, error_stream);
    }

    template<typename Context, typename Buffer>
    std::optional<root_value_type> context_parse(Context&& ctx, const parse_table_view& table, const Buffer& buffer) const
    {
        utils::no_stream error_stream;
        return context_parse(std::forward<Context>(ctx), table, buffer, error_stream);
    }

    template<typename Buffer, typename ErrorStream>
    std::optional<root_value_type> parse(const parse_table_view& table, const Buffer& buffer, ErrorStream& error_stream) const
    {
        return parse(table, parse_options{}, buffer, error_stream);
    }

    template<typename Context, typename Buffer, typename ErrorStream>
    std::optional<root_value_type> context_parse(Context&& ctx, const parse_table_view& table, const Buffer& buffer, ErrorStream& error_stream) const
    {
        return context_parse(std::forward<Context>(ctx), table, parse_options{}, buffer, error_stream);
    }

    template<typename Buffer, typename ErrorStream>
    std::optional<root_value_type> parse(const parse_table_view& table, parse_options options, const Buffer& buffer, ErrorStream& error_stream) const
    {
        return context_parse(no_type{}, table, options, buffer, error_stream);
    }

    template<typename Context, typename Buffer, typename ErrorStream>
    std::optional<root_value_type> context_parse(Context&& ctx, const parse_table_view& table, parse_options options, const Buffer& buffer, ErrorStream& error_stream) const
    {
        if (!table.valid())
            throw std::runtime_error("parse_table_view is invalid");
        if (!table.matches(compute_grammar_hash()))
            throw std::runtime_error("parse table does not match grammar hash");
        if (table.term_count() != term_count || table.nterm_count() != nterm_count
            || table.rule_count() != rule_count)
            throw std::runtime_error("parse table dimensions mismatch");

        detail::parser_value_stack_type_t<Buffer, empty_rules_count, value_variant_type> value_stack{};
        detail::parse_table_cursor_stack_type_t<Buffer, empty_rules_count> cursor_stack{};

        detail::value_reductors<Context, value_variant_type, rule_tuple_type, rule_count> reductors(rule_tuple);
        detail::parse_state ps(cursor_stack, value_stack, error_stream, options, buffer.begin(), buffer.end(), reductors);

        ps.cursor_stack.push_back(0);

        std::optional<root_value_type> root_value;

        while (true)
        {
            size16_t cursor = ps.cursor_stack.back();

            auto t_idx = get_current_term(buffer, ps);
            if (t_idx == uninitialized16)
                break;

            auto entry = table.action(cursor, get_parse_table_idx(true, t_idx));

            if (entry.kind == parse_table_entry_kind::error)
            {
                if (ps.in_consume_mode())
                {
                    if (!consume_term_recovering(ps))
                        break;
                    continue;
                }
                if (!ps.in_recovery_mode())
                {
                    syntax_error(ps);
                    enter_recovery_mode(ps);
                }
                if (!pop_stacks(ps))
                    break;
                continue;
            }
            else
            {
                if (ps.in_consume_mode())
                    leave_consume_mode(ps);
            }

            if (entry.kind == parse_table_entry_kind::shift)
            {
                shift(ps, buffer.get_view(ps.current_it, ps.current_end_it), t_idx, entry.arg);
                consume_term(ps);
            }
            else if (entry.kind == parse_table_entry_kind::shift_error_recovery_token)
            {
                shift_recovery_token(ps, entry.arg);
                leave_recovery_mode(ps);
                enter_consume_mode(ps);
            }
            else if (entry.kind == parse_table_entry_kind::reduce)
                reduce(std::forward<Context>(ctx), table, ps, entry.arg);
            else if (entry.kind == parse_table_entry_kind::rr_conflict)
                rr_conflict(std::forward<Context>(ctx), table, ps, entry.arg);
            else if (entry.kind == parse_table_entry_kind::success)
            {
                root_value = std::optional(std::move(success(ps)));
                break;
            }
        }

        return root_value;
    }

    template<typename Stream>
    void write_diag_str(Stream& s, const parse_table_view& table) const
    {
        s << "PARSER" << "\n\n";
        s << "Parser object size: " << sizeof(*this) << "\n";
        if (table.valid())
            s << "Number of states: " << table.state_count() << "(cap: " << state_count_cap << ")\n";
        s << "\n";

        s << "RULES\n\n";
        for (size16_t i = 0; i < rule_count; ++i)
        {
            s << i << "    ";
            write_rule_diag_str(s, gi.rule_infos[i]);
            s << "\n";
        }
        s << "\n\n";
    }

private:
    static const size_t max_rule_element_count = meta::max_v<1, Rules::n...>;
    static const size16_t eof_idx = sizeof...(Terms);
    static const size16_t error_recovery_token_idx = sizeof...(Terms) + 1;
    static const size_t term_count = sizeof...(Terms) + 2;
    static const size16_t fake_root_idx = sizeof...(NTerms);
    static const size_t nterm_count = sizeof...(NTerms) + 1;
    static const size_t symbol_count = term_count + nterm_count;
    static const size_t root_rule_idx = sizeof...(Rules);
    static const size_t rule_count = sizeof...(Rules) + 1;
    static const size_t empty_rules_count = meta::count_zeros<Rules::n...>;
    static const size_t situation_size = max_rule_element_count + 1;
    static const size_t situation_address_space_size = rule_count * situation_size * term_count;
    static const size_t situation_count = (0 + ... + (Rules::n + 1)) * term_count + 2;
        
    static const size_t state_count_cap = get_limits<Limits, situation_count>::state_count_cap;
    static const size_t max_sit_count_per_state_cap = get_limits<Limits, situation_count>::max_sit_count_per_state_cap;
    
    using value_variant_type = meta::unique_types_variant_t<
        std::nullptr_t,
        no_type,
        value_type_t<NTerms>...,
        value_type_t<Terms>...
    >;

    struct rule_info
    {
        size16_t l_idx = uninitialized16;
        size16_t r_idx = uninitialized16;
        size16_t r_elements = uninitialized16;
    };

    struct symbol
    {
        constexpr symbol() :
            term(false), idx(uninitialized16)
        {}

        constexpr symbol(bool term, size16_t idx) :
            term(term), idx(idx)
        {}

        constexpr size16_t get_parse_table_idx() const
        {
            return parser::get_parse_table_idx(term, idx);
        }

        bool term;
        size16_t idx;
    };

    struct grammar_info
    {
        symbol right_sides[rule_count][max_rule_element_count] = { };
        rule_info rule_infos[rule_count] = { };
        utils::slice nterm_rule_slices[nterm_count] = { };
        int term_precedences[term_count] = { };
        associativity term_associativities[term_count] = { };
        int rule_precedences[rule_count] = { };
        associativity rule_associativities[rule_count] = { };
        size16_t rule_last_terms[rule_count] = { };
    };

    struct situation_info
    {
        size16_t rule_info_idx = uninitialized16;
        size16_t after = uninitialized16;
        size16_t t = uninitialized16;
    };

    constexpr static size32_t make_situation_idx(situation_info info)
    {
        return info.rule_info_idx * situation_size * term_count + info.after * term_count + info.t;
    }

    constexpr static situation_info make_situation_info(size32_t idx)
    {
        size16_t t = size16_t(idx % term_count);
        idx /= term_count;
        size16_t after = size16_t(idx % situation_size);
        size16_t rule_info_idx = size16_t(idx / situation_size);
        return situation_info{ rule_info_idx, after, t };
    }

    using parse_table_entry_kind = parse_table_view::entry_kind;
    using parse_table_entry      = parse_table_view::entry;

    constexpr static bool is_shift(parse_table_entry_kind kind)
    {
        return kind == parse_table_entry_kind::shift
            || kind == parse_table_entry_kind::shift_error_recovery_token;
    }

    struct state_analyzer
    {
        state_analyzer(const grammar_info& gi,
                       const char* const* term_names_ref,
                       const char* const* nterm_names_ref,
                       std::vector<parse_table_entry>& parse_table):
            gi(gi),
            term_names_ref(term_names_ref),
            nterm_names_ref(nterm_names_ref),
            parse_table(parse_table)
        {
            simple_states.resize(state_count_cap, stdex::dyn_bitset(situation_address_space_size));
            states.reserve(state_count_cap);

            closures.resize(situation_address_space_size);
            closures_analyzed.resize(situation_address_space_size);

            right_side_slice_empty_analyzed.resize(situation_size * rule_count);
            right_side_slice_empty.resize(situation_size * rule_count);
            right_side_slice_first.resize(situation_size * rule_count,
                                          stdex::dyn_bitset(term_count));
            right_side_slice_first_analyzed.resize(situation_size * rule_count);
            nterm_empty.resize(nterm_count);
            nterm_first.resize(nterm_count, stdex::dyn_bitset(term_count));
            nterm_empty_analyzed.resize(nterm_count);
            nterm_first_analyzed.resize(nterm_count);
        }

        using term_subset = stdex::dyn_bitset;
        using nterm_subset = stdex::dyn_bitset;
        using right_side_slice_subset = stdex::dyn_bitset;
        using situation_vector = std::vector<size32_t>;

        struct state
        {
            situation_vector all_situations_vec;
            stdex::dyn_bitset kernel;
            std::array<situation_vector, symbol_count> situations_by_symbol;

            state() : kernel(situation_address_space_size) {}
        };
        
        bool add_situation(size16_t state_idx, size32_t sit_idx, bool to_kernel)
        {
            if (!simple_states[state_idx].test(sit_idx))
            {
                simple_states[state_idx].set(sit_idx);
                states[state_idx].all_situations_vec.push_back(sit_idx);
                situation_info info = make_situation_info(sit_idx);
                const rule_info& ri = gi.rule_infos[info.rule_info_idx];

                if (info.after < ri.r_elements)
                {
                    const symbol& sm = gi.right_sides[ri.r_idx][info.after];
                    states[state_idx].situations_by_symbol[sm.get_parse_table_idx()].push_back(sit_idx);
                }
                else
                {
                    states[state_idx].situations_by_symbol[get_parse_table_idx(true, info.t)].push_back(sit_idx);
                }

                if (to_kernel)
                {
                    states[state_idx].kernel.set(sit_idx);
                }
                return true;
            }
            return false;
        }

        size16_t analyze_states()
        {
            situation_info root_situation_info{ root_rule_idx, 0, eof_idx };
            size32_t root_sit_idx = make_situation_idx(root_situation_info);
            state_count = 1;

            states.resize(1);
            size16_t current_state = 0;
            add_situation(current_state, root_sit_idx, true);

            while (current_state < state_count)
            {
                state& s = states[current_state];
                for (auto i = 0u; i < s.all_situations_vec.size(); ++i)
                {
                    closure(current_state, s.all_situations_vec[i]);
                }

                for (size16_t symbol_idx = 0u; symbol_idx < symbol_count; ++symbol_idx)
                {
                    transitions(current_state, symbol_idx, s.situations_by_symbol[symbol_idx]);
                }
                current_state++;
            }
            return state_count;
        }

        void closure(size16_t state_idx, size32_t sit_idx)
        {
            if (closures_analyzed.test(sit_idx))
            {
                for(auto i = 0u; i < closures[sit_idx].size(); ++i)
                {
                    add_situation(state_idx, closures[sit_idx][i], false);
                }
                return;
            }

            closures_analyzed.set(sit_idx);

            situation_info info = make_situation_info(sit_idx);
            const rule_info& ri = gi.rule_infos[info.rule_info_idx];
            if (info.after >= ri.r_elements)
                return;

            const symbol& sm = gi.right_sides[ri.r_idx][info.after];
            if (sm.term)
                return;

            size16_t nt = sm.idx;
            bool after_empty = make_right_side_slice_empty(ri, info.after + 1);
            const utils::slice& sl = gi.nterm_rule_slices[nt];

            const term_subset& first = make_right_side_slice_first(ri, info.after + 1);
            for (size16_t t = 0; t < term_count; ++t)
            {
                if (first.test(t))
                {
                    for (auto i = 0u; i < sl.n; ++i)
                    {
                        size32_t new_sit_idx = make_situation_idx(situation_info{ size16_t(sl.start + i), 0, t });
                        if (add_situation(state_idx, new_sit_idx, false))
                            closures[sit_idx].push_back(new_sit_idx);
                    }
                }
            }

            if (after_empty)
            {
                for (auto i = 0u; i < sl.n; ++i)
                {
                    size32_t new_sit_idx = make_situation_idx(situation_info{ size16_t(sl.start + i), 0, info.t });
                    if (add_situation(state_idx, new_sit_idx, false))
                        closures[sit_idx].push_back(new_sit_idx);
                }
            }
        }

        void transitions(size16_t state_idx, size16_t symbol_idx, const situation_vector& symbol_situations)
        {
            if (symbol_situations.size() == 0)
                return;

            bool has_reduction = false;
            bool has_shift = false;
            bool sr_resolved = false;

            stdex::dyn_bitset kernel(situation_address_space_size);
            situation_vector kernel_vec;
            auto& entry = parse_table[state_idx * symbol_count + symbol_idx];
            size16_t reduction_rule_idx = uninitialized16;

            for (size32_t sit_idx : symbol_situations)
            {
                situation_info info = make_situation_info(sit_idx);
                const rule_info& ri = gi.rule_infos[info.rule_info_idx];

                bool reduction = info.after >= ri.r_elements;
                if (reduction)
                {
                    if (ri.r_idx == root_rule_idx)
                    {
                        entry.kind = parse_table_entry_kind::success;
                        break;
                    }

                    if (has_reduction)
                    {
                        throw_rr_conflict(state_idx, symbol_idx, reduction_rule_idx, info.rule_info_idx);
                    }
                    if (has_shift)
                    {
                        if (!sr_resolved)
                        {
                            auto res = solve_conflict(info.rule_info_idx, info.t);
                            if (res == parse_table_entry_kind::error)
                                throw_sr_conflict(state_idx, symbol_idx, info.rule_info_idx);
                            entry.kind = res;
                            sr_resolved = true;
                        }
                    }
                    else
                    {
                        entry.kind = parse_table_entry_kind::reduce;
                    }
                    has_reduction = true;
                    reduction_rule_idx = info.rule_info_idx;
                }
                else
                {
                    if (has_reduction)
                    {
                        if (!sr_resolved)
                        {
                            const auto& sm = gi.right_sides[ri.r_idx][info.after];
                            auto res = solve_conflict(reduction_rule_idx, sm.idx);
                            if (res == parse_table_entry_kind::error)
                                throw_sr_conflict(state_idx, symbol_idx, reduction_rule_idx);
                            entry.kind = res;
                            sr_resolved = true;
                        }
                    }
                    else
                    {
                        entry.kind = parse_table_entry_kind::shift;
                    }
                    has_shift = true;
                    situation_info new_info = situation_info{ info.rule_info_idx, size16_t(info.after + 1), info.t };
                    size32_t new_idx = make_situation_idx(new_info);
                    if (!kernel.test(new_idx))
                    {
                        kernel.set(new_idx);
                        kernel_vec.push_back(new_idx);
                    }
                }
            }

            if (entry.kind == parse_table_entry_kind::shift)
            {
                size16_t new_state_idx = uninitialized16;
                for (size16_t i = 0u; i < state_count; i++)
                {
                    if (states[i].kernel == kernel)
                    {
                        new_state_idx = i;
                    }
                }
                if (new_state_idx == uninitialized16)
                {
                    new_state_idx = state_count++;
                    if (state_count > state_count_cap)
                        throw std::runtime_error("State count exceeds the cap");
                    states.resize(state_count);
                    states[new_state_idx].kernel = kernel;
                }

                entry.arg = new_state_idx;
                if (symbol_idx == get_parse_table_idx(true, error_recovery_token_idx))
                    entry.kind = parse_table_entry_kind::shift_error_recovery_token;

                for (auto sit_idx : kernel_vec)
                    add_situation(new_state_idx, sit_idx, true);
            }
            else if (entry.kind == parse_table_entry_kind::reduce)
            {
                entry.arg = reduction_rule_idx;
            }
        }

        // Bison-подобное разрешение S/R:
        //   * если приоритеты заданы и различны — побеждает больший
        //   * при равенстве приоритетов используется ассоциативность
        //   * если приоритеты не заданы или ассоциативность no_assoc,
        //     решение неоднозначно и приводит к исключению
        auto solve_conflict(size16_t rule_info_idx, size16_t term_idx) const
        {
            size16_t rule_idx = gi.rule_infos[rule_info_idx].r_idx;
            int r_p = gi.rule_precedences[rule_idx];
            int t_p = gi.term_precedences[term_idx];

            if (r_p == 0 || t_p == 0)
                return parse_table_entry_kind::shift;

            if (r_p > t_p) return parse_table_entry_kind::reduce;
            if (r_p < t_p) return parse_table_entry_kind::shift;

            switch (gi.rule_associativities[rule_idx])
            {
                case associativity::ltor: return parse_table_entry_kind::reduce;
                case associativity::rtol: return parse_table_entry_kind::shift;
                default:                  return parse_table_entry_kind::error;
            }
        }

        const char* symbol_name_of(size_t sym_idx) const
        {
            return sym_idx < nterm_count
                ? nterm_names_ref[sym_idx]
                : term_names_ref[sym_idx - nterm_count];
        }

        std::string format_rule(size_t rule_info_idx) const
        {
            const auto& ri = gi.rule_infos[rule_info_idx];
            std::string s = nterm_names_ref[ri.l_idx];
            s += " ->";
            for (size_t i = 0; i < ri.r_elements; ++i)
            {
                const auto& sym = gi.right_sides[ri.r_idx][i];
                s += " ";
                s += (sym.term ? term_names_ref[sym.idx] : nterm_names_ref[sym.idx]);
            }
            return s;
        }

        void log_sr_resolved(size_t state_idx, size_t sym_idx,
                             size_t rule_info_idx, parse_table_entry_kind res) const
        {
            std::cerr
                << "S/R conflict at state " << state_idx
                << " on '" << symbol_name_of(sym_idx) << "': resolved as "
                << (res == parse_table_entry_kind::reduce ? "reduce" : "shift")
                << " by '" << format_rule(rule_info_idx) << "'\n";
        }

        [[noreturn]] void throw_sr_conflict(size_t state_idx, size_t sym_idx,
                                            size_t rule_info_idx) const
        {
            std::string msg = "S/R conflict (unresolved) at state ";
            msg += std::to_string(state_idx);
            msg += " on '";
            msg += symbol_name_of(sym_idx);
            msg += "': reduce by '";
            msg += format_rule(rule_info_idx);
            msg += "' vs shift";
            throw std::runtime_error(msg);
        }

        [[noreturn]] void throw_rr_conflict(size_t state_idx, size_t sym_idx,
                                            size_t rule_info_a, size_t rule_info_b) const
        {
            std::string msg = "R/R conflict at state ";
            msg += std::to_string(state_idx);
            msg += " on '";
            msg += symbol_name_of(sym_idx);
            msg += "': rule '";
            msg += format_rule(rule_info_a);
            msg += "' vs rule '";
            msg += format_rule(rule_info_b);
            msg += "'";
            throw std::runtime_error(msg);
        }

        const term_subset& make_right_side_slice_first(const rule_info& ri, size_t start)
        {
            size_t right_side_slice_idx = max_rule_element_count * ri.r_idx + start;
            auto& res = right_side_slice_first[right_side_slice_idx];

            if (right_side_slice_first_analyzed.test(right_side_slice_idx))
                return res;
            right_side_slice_first_analyzed.set(right_side_slice_idx);

            for (size_t i = start; i < ri.r_elements; ++i)
            {
                const symbol& s = gi.right_sides[ri.r_idx][i];
                if (s.term)
                {
                    res.set(s.idx);
                    break;
                }
                res.add(make_nterm_first(s.idx));
                if (!make_nterm_empty(s.idx))
                    break;
            }
            return res;
        }

        const term_subset& make_nterm_first(size16_t nt)
        {
            if (nterm_first_analyzed.test(nt))
                return nterm_first[nt];
            nterm_first_analyzed.set(nt);

            const utils::slice& s = gi.nterm_rule_slices[nt];
            for (size_t i = 0u; i < s.n; ++i)
            {
                const rule_info& ri = gi.rule_infos[s.start + i];
                nterm_first[nt].add(make_right_side_slice_first(ri, 0));
            }
            return nterm_first[nt];
        }

        bool make_right_side_slice_empty(const rule_info& ri, size_t start)
        {
            auto idx = ri.r_idx * situation_size + start;
            if (right_side_slice_empty_analyzed.test(idx))
                return right_side_slice_empty.test(idx);

            right_side_slice_empty_analyzed.set(idx);
            for (size_t i = start; i < ri.r_elements; ++i)
            {
                const symbol& s = gi.right_sides[ri.r_idx][i];
                if (s.term)
                    return false;
                if (!make_nterm_empty(s.idx))
                    return false;
            }
            right_side_slice_empty.set(idx);
            return true;
        }

        bool make_right_side_empty(const rule_info& ri)
        {
            return make_right_side_slice_empty(ri, 0);
        }

        bool make_nterm_empty(size16_t nt)
        {
            if (nterm_empty_analyzed.test(nt))
                return nterm_empty.test(nt);
            nterm_empty_analyzed.set(nt);

            const utils::slice& s = gi.nterm_rule_slices[nt];
            for (size_t i = 0u; i < s.n; ++i)
            {
                if (make_right_side_empty(gi.rule_infos[s.start + i]))
                {
                    return (nterm_empty.set(nt), true);
                }
            }
            return (nterm_empty.reset(nt), false);
        }

        const grammar_info& gi;
        const char* const* term_names_ref;
        const char* const* nterm_names_ref;
        std::vector<stdex::dyn_bitset> simple_states;
        std::vector<parse_table_entry>& parse_table;

        std::vector<state> states;

        size16_t state_count = 0;
        stdex::dyn_bitset closures_analyzed;
        std::vector<situation_vector> closures;

        stdex::dyn_bitset right_side_slice_empty_analyzed;
        stdex::dyn_bitset right_side_slice_empty;
        std::vector<stdex::dyn_bitset> right_side_slice_first;
        stdex::dyn_bitset right_side_slice_first_analyzed;
        stdex::dyn_bitset nterm_empty;
        std::vector<stdex::dyn_bitset> nterm_first;
        stdex::dyn_bitset nterm_empty_analyzed;
        stdex::dyn_bitset nterm_first_analyzed;
    };

    constexpr static size16_t get_parse_table_idx(bool term, size16_t idx)
    {
        return term ? nterm_count + idx : idx;
    }

    constexpr void analyze_eof()
    {
        term_names[eof_idx] = detail::eof::get_name();
        term_ids[eof_idx] = detail::eof::get_name();
        gi.term_precedences[eof_idx] = 0;
        gi.term_associativities[eof_idx] = associativity::no_assoc;
    }

    constexpr void analyze_error_recovery_token()
    {
        term_names[error_recovery_token_idx] = error_recovery_token::get_name();
        term_ids[error_recovery_token_idx] = error_recovery_token::get_name();
        gi.term_precedences[error_recovery_token_idx] = 0;
        gi.term_associativities[error_recovery_token_idx] = associativity::no_assoc;
    }

    template<size16_t TermIdx, typename Term>
    constexpr void analyze_term(const Term& t)
    {
        gi.term_precedences[TermIdx] = t.get_precedence();
        gi.term_associativities[TermIdx] = t.get_associativity();
        term_names[TermIdx] = t.get_name();
        term_ids[TermIdx] = t.get_id();
        term_ftors[TermIdx] = string_view_to_term_value<TermIdx>;
    }

    template<typename ValueType>
    constexpr void analyze_nterm(const nterm<ValueType>& nt, size16_t idx)
    {
        nterm_names[idx] = nt.get_name();
    }

    template<typename ValueType>
    constexpr void analyze_nterm(detail::fake_root<ValueType>)
    {
        nterm_names[fake_root_idx] = detail::fake_root<ValueType>::get_name();
    }

    template<typename Term>
    constexpr auto make_symbol(const Term& t) const
    {
        return symbol{ true, size16_t(utils::find_str(term_ids, t.get_id())) };
    }

    template<typename ValueType>
    constexpr auto make_symbol(const nterm<ValueType>& nt) const
    {
        return symbol{ false, size16_t(utils::find_str(nterm_names, nt.get_name())) };
    }

    template<size_t... I>
    constexpr void analyze_terms(std::index_sequence<I...>)
    {
        (void(analyze_term<I>(std::get<I>(term_tuple))), ...);
    }

    template<size_t... I>
    constexpr void analyze_nterms(std::index_sequence<I...>)
    {
        (void(analyze_nterm(std::get<I>(nterm_tuple), I)), ...);
    }

    template<size_t... I>
    constexpr void analyze_rules(std::index_sequence<I...>, const root_nterm_type& root)
    {
        (void(analyze_rule<I>(std::get<I>(rule_tuple), std::make_index_sequence<Rules::n>{})), ...);
        analyze_rule<root_rule_idx>(detail::fake_root<value_type_t<root_nterm_type>>{}(root), std::index_sequence<0>{});
        stdex::sort(gi.rule_infos, [](const auto& ri1, const auto& ri2) { return ri1.l_idx < ri2.l_idx; });
        make_nterm_rule_slices();
    }

    constexpr void make_nterm_rule_slices()
    {
        size16_t nt = 0;
        for (size16_t i = 0u; i < rule_count; ++i)
        {
            if (nt != gi.rule_infos[i].l_idx)
            {
                nt = gi.rule_infos[i].l_idx;
                gi.nterm_rule_slices[nt].start = i;
                gi.nterm_rule_slices[nt].n = 1;
            }
            else
                gi.nterm_rule_slices[nt].n++;
        }
    }

    constexpr size16_t calculate_rule_last_term(size16_t rule_idx, size16_t rule_size) const
    {
        for (int i = int(rule_size - 1); i >= 0; --i)
        {
            const auto& s = gi.right_sides[rule_idx][i];
            if (s.term)
                return s.idx;
        }
        return uninitialized16;
    }

    constexpr int calculate_rule_precedence(int precedence, size16_t rule_idx) const
    {
        if (precedence != 0)
            return precedence;
        size16_t last_term_idx = gi.rule_last_terms[rule_idx];
        if (last_term_idx != uninitialized16)
            return gi.term_precedences[last_term_idx];
        return 0;
    }

    constexpr associativity calculate_rule_associativity(size16_t rule_idx) const
    {
        size16_t last_term_idx = gi.rule_last_terms[rule_idx];
        if (last_term_idx != uninitialized16)
            return gi.term_associativities[last_term_idx];
        return associativity::no_assoc;
    }

    template<size_t Nr, bool RequiresContext, typename F, typename L, typename... R, size_t... I>
    constexpr void analyze_rule(const detail::rule<RequiresContext, F, L, R...>& r, std::index_sequence<I...>)
    {
        size16_t l_idx = size16_t(utils::find_str(nterm_names, r.get_l().get_name()));
        (void(gi.right_sides[Nr][I] = make_symbol(std::get<I>(r.get_r()))), ...);
        constexpr size16_t rule_elements_count = size16_t(sizeof...(R));
        gi.rule_infos[Nr] = { l_idx, size16_t(Nr), rule_elements_count };
        gi.rule_last_terms[Nr] = calculate_rule_last_term(Nr, rule_elements_count);
        gi.rule_precedences[Nr] = calculate_rule_precedence(r.get_precedence(), Nr);
        gi.rule_associativities[Nr] = calculate_rule_associativity(Nr);
    }

    constexpr const char* get_symbol_name(const symbol& s) const
    {
        return s.term ? term_names[s.idx] : nterm_names[s.idx];
    }

    template<typename Stream>
    void write_rule_diag_str(Stream& s, const rule_info& ri) const
    {
        s << nterm_names[ri.l_idx] << " <- ";
        if (ri.r_elements > 0)
            s << get_symbol_name(gi.right_sides[ri.r_idx][0]);
        if constexpr (max_rule_element_count > 1)
        {
            for (size_t i = 1u; i < ri.r_elements; ++i)
            {
                s << " " << get_symbol_name(gi.right_sides[ri.r_idx][i]);
            }
        }
    }

    template<size16_t TermIdx>
    constexpr static value_variant_type string_view_to_term_value(const term_tuple_type& term_tuple, const std::string_view& sv, source_point sp)
    {
        const auto &t = std::get<TermIdx>(term_tuple);
        using term_value_type = value_type_t<std::tuple_element_t<TermIdx, term_tuple_type>>;
        return value_variant_type(term_value_type(t.get_ftor()(sv), sp));
    }

    template<typename ParseState>
    constexpr void shift_recovery_token(ParseState& ps, size16_t new_cursor_value) const
    {
        if (ps.options.verbose)
            ps.error_stream << ps.current_sp << " PARSE: Shift to " << new_cursor_value << ", term: " << term_names[error_recovery_token_idx] << "\n";
        ps.cursor_stack.push_back(new_cursor_value);

        ps.value_stack.emplace_back(term_value(no_type{}, ps.current_sp));
    }

    template<typename ParseState>
    constexpr void shift(ParseState& ps, const std::string_view& sv, size16_t term_idx, size16_t new_cursor_value) const
    {
        if (ps.options.verbose)
            ps.error_stream << ps.current_sp << " PARSE: Shift to " << new_cursor_value << ", term: " << sv << "\n";
        ps.cursor_stack.push_back(new_cursor_value);

        const auto& ftor = term_ftors[term_idx];
        ps.value_stack.emplace_back(ftor(term_tuple, sv, ps.current_sp));
    }

    template<typename Context, typename ParseState>
    void reduce(Context&& ctx, const parse_table_view& table, ParseState& ps, size16_t rule_info_idx) const
    {
        const rule_info& ri = gi.rule_infos[rule_info_idx];
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Reduced using rule " << ri.r_idx << "  ";
            write_rule_diag_str(ps.error_stream, ri);
            ps.error_stream << "\n";
        }

        ps.cursor_stack.erase(ps.cursor_stack.end() - ri.r_elements, ps.cursor_stack.end());
        size16_t new_cursor_value = table.action(ps.cursor_stack.back(), ri.l_idx).arg;

        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Go to " << new_cursor_value << "\n";
        }

        ps.cursor_stack.push_back(new_cursor_value);
        value_variant_type* start = ps.value_stack.data() + ps.value_stack.size() - ri.r_elements;
        value_variant_type lvalue(ps.reductors.invoke(std::forward<Context>(ctx), ri.r_idx, start));
        ps.value_stack.erase(ps.value_stack.end() - ri.r_elements, ps.value_stack.end());
        ps.value_stack.emplace_back(std::move(lvalue));
    }

    template<typename Context, typename ParseState>
    void rr_conflict(Context&& ctx, const parse_table_view& table, ParseState& ps, size16_t rule_idx) const
    {
        if (ps.options.verbose)
            ps.error_stream << ps.current_sp << " PARSE: R/R conflict encountered \n";
        reduce(std::forward<Context>(ctx), table, ps, rule_idx);
    }

    template<typename ParseState>
    constexpr bool pop_stacks(ParseState& ps) const
    {
        ps.cursor_stack.pop_back();
        if (ps.value_stack.size() != 0)
            ps.value_stack.pop_back();

        if (ps.cursor_stack.size() == 0)
        {
            if (ps.options.verbose)
            {
                ps.error_stream << ps.current_sp << " PARSE: Could not recover from error \n";
            }
            return false;
        }

        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Recovering to state " << ps.cursor_stack.back() << "\n";
        }

        return true;
    }

    template<typename ParseState>
    constexpr void syntax_error(ParseState& ps) const
    {
        ps.error_stream << ps.current_sp << " PARSE: Syntax error: " <<
            "Unexpected '" << term_names[ps.current_term_idx] << "'" << "\n";
    }

    template<typename ParseState>
    constexpr root_value_type& success(ParseState& ps) const
    {
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Success \n";
        }
        return std::get<root_value_type>(ps.value_stack.front());
    }

    template<typename ParseState>
    constexpr void consume_term(ParseState& ps) const
    {
        ps.current_sp.update(ps.current_it, ps.current_end_it);
        ps.current_it = ps.current_end_it;
    }

    template<typename ParseState>
    constexpr void enter_recovery_mode(ParseState& ps) const
    {
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Entering recovery mode \n";
        }
        ps.enter_recovery_mode();
    }

    template<typename ParseState>
    constexpr void leave_recovery_mode(ParseState& ps) const
    {
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Leaving recovery mode \n";
        }
        ps.leave_recovery_mode();
    }

    template<typename ParseState>
    constexpr void enter_consume_mode(ParseState& ps) const
    {
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Entering consume mode \n";
        }
        ps.enter_consume_mode();
    }

    template<typename ParseState>
    constexpr void leave_consume_mode(ParseState& ps) const
    {
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Leaving consume mode \n";
        }
        ps.leave_consume_mode();
    }

    template<typename ParseState>
    constexpr bool consume_term_recovering(ParseState& ps) const
    {
        if (ps.current_term_idx == eof_idx)
            return false;
        if (ps.options.verbose)
        {
            ps.error_stream << ps.current_sp << " PARSE: Recovery, consuming term " << term_names[ps.current_term_idx] << " \n";
        }
        consume_term(ps);
        return true;
    }

    template<typename Buffer, typename ParseState>
    size16_t get_current_term(const Buffer& buffer, ParseState& ps) const
    {
        if (ps.in_recovery_mode())
            return error_recovery_token_idx;

        if (ps.current_it != ps.current_end_it)
            return ps.current_term_idx;

        if (ps.options.skip_whitespace)
        {
            auto after_ws = skip_whitespace(ps);
            ps.current_sp.update(ps.current_it, after_ws);
            ps.current_it = after_ws;
        }

        if (ps.current_it == ps.buffer_end)
        {
            ps.current_term_idx = eof_idx;
            trace_recognized_term(ps);
            return eof_idx;
        }

        recognized_term res;
        match_options opts;
        opts.set_verbose(ps.options.verbose);

        if constexpr (generate_lexer)
        {
            res = generated_match(buffer, ps.current_it, ps.buffer_end);
            if (opts.verbose && res.term_idx != uninitialized16)
                ps.error_stream << ps.current_sp << " LEXER MATCH: Recognized "
                                << res.term_idx << "\n";
        }
        else
        {
            lexer_type custom_lexer;
            res = custom_lexer.match(opts, ps.current_sp, ps.current_it, ps.buffer_end, ps.error_stream);
        }

        ps.current_term_idx = res.term_idx;
        ps.current_end_it = ps.current_it + res.len;

        if (ps.current_term_idx == uninitialized16)
        {
            unexpected_char(ps);
            return uninitialized16;
        }
        else
        {
            trace_recognized_term(ps);
        }

        return ps.current_term_idx;
    }

    template<typename ParseState>
    constexpr auto skip_whitespace(ParseState& ps) const
    {
        constexpr char space_chars_newline[] = { 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x00 };
        constexpr char space_chars_no_newline[] = { 0x09, 0x0b, 0x0c, 0x0d, 0x20, 0x00 };

        const char* space_chars = ps.options.skip_newline
                                    ? space_chars_newline
                                    : space_chars_no_newline;

        auto start = ps.current_it;
        while (true)
        {
            if (start == ps.buffer_end)
                break;
            if (utils::find_char(*start, space_chars) == uninitialized)
                break;
            ++start;
        }
        return start;
    }

    template<typename ParseState>
    constexpr void unexpected_char(ParseState& ps) const
    {
        ps.error_stream << ps.current_sp << " PARSE: Unexpected character: " << *ps.current_it << "\n";
    }

    template<typename ParseState>
    constexpr void trace_recognized_term(ParseState& ps) const
    {
        if (ps.options.verbose)
            ps.error_stream << ps.current_sp << " PARSE: Recognized " << term_names[ps.current_term_idx] << " \n";
    }

    // ---- std::regex-based generated lexer ----
    template<typename Buffer, typename Iterator>
    recognized_term generated_match(const Buffer& buffer, Iterator start, Iterator end) const
    {
        std::string_view sv = buffer.get_view(start, end);

        size_t best_len = 0;
        size16_t best_term = uninitialized16;
        match_all_terms(std::make_index_sequence<sizeof...(Terms)>{}, sv, best_len, best_term);
        return recognized_term(best_term, best_len);
    }

    template<size_t... I>
    void match_all_terms(std::index_sequence<I...>, std::string_view sv,
                         size_t& best_len, size16_t& best_term) const
    {
        (void(match_one_term<I>(std::get<I>(term_tuple), sv, best_len, best_term)), ...);
    }

    template<size_t I, typename Term>
    void match_one_term(const Term& t, std::string_view sv,
                        size_t& best_len, size16_t& best_term) const
    {
        size_t len = t.match(sv);
        if (len > best_len)
        {
            best_len = len;
            best_term = size16_t(I);
        }
    }

    struct no_parser{};

    str_table<term_count> term_names = {};
    str_table<term_count> term_ids = {};
    str_table<nterm_count> nterm_names = {};
    grammar_info gi = {};

    term_tuple_type term_tuple;
    nterm_tuple_type nterm_tuple;
    rule_tuple_type rule_tuple;

    using string_view_to_term_value_t = value_variant_type(*)(const term_tuple_type&, const std::string_view&, source_point);
    string_view_to_term_value_t term_ftors[term_count] = {};

    // Лексер на std::regex больше не требует генерируемого DFA.
};

template<typename Root, typename Terms, typename NTerms, typename Rules>
parser(Root, Terms, NTerms, Rules&&) -> parser<Root, Terms, NTerms, Rules, use_generated_lexer, default_limits>;

template<typename Root, typename Terms, typename NTerms, typename Rules, typename LexerUsage>
parser(Root, Terms, NTerms, Rules&&, LexerUsage) -> parser<Root, Terms, NTerms, Rules, LexerUsage, default_limits>;

template<typename Root, typename Terms, typename NTerms, typename Rules, typename LexerUsage, typename Limits>
parser(Root, Terms, NTerms, Rules&&, LexerUsage, Limits) -> parser<Root, Terms, NTerms, Rules, LexerUsage, Limits>;

template<typename... Terms>
constexpr auto terms(const Terms&... terms)
{
    return std::make_tuple(detail::make_term(terms)...);
}

template<typename... NTerms>
constexpr auto nterms(NTerms... nterms)
{
    return std::make_tuple(nterms...);
}

template<typename... Rules>
constexpr auto rules(Rules&&... rules)
{
    return std::make_tuple(std::move(rules)...);
}

struct skip
{
    template<typename T>
    constexpr skip(T&&) {}
};


template<auto& Pattern>
class regex_term : public term
{
public:
    using internal_value_type = std::string_view;

    static const bool is_trivial = false;
    static const size_t pattern_size = std::size(Pattern);

    regex_term(associativity a = associativity::no_assoc) :
        regex_term(nullptr, 0, a)
    {}

    regex_term(int precedence = 0, associativity a = associativity::no_assoc) :
        regex_term(nullptr, precedence, a)
    {}

    regex_term(const char *custom_name, int precedence = 0, associativity a = associativity::no_assoc) :
        term(precedence, a),
        custom_name(custom_name),
        re(Pattern)
    {
        id[0] = 'r';
        id[1] = '_';
        std::copy(Pattern, Pattern + pattern_size, id + 2);
    }

    const char* get_name() const { return custom_name ? custom_name : id; }
    const char* get_id() const { return id; }

    const auto& get_ftor() const { return utils::pass_sv; }

    // Возвращает длину совпадения от начала sv (0 — не совпало).
    size_t match(std::string_view sv) const
    {
        std::cmatch m;
        if (std::regex_search(sv.data(), sv.data() + sv.size(), m, re,
                              std::regex_constants::match_continuous))
            return m.length();
        return 0;
    }

private:
    char id[pattern_size + 2] = {};
    const char* custom_name = nullptr;
    std::regex re;
};

} // namespace ctpg

#endif
