// ctpg/table_view/mmap_factory.hpp
#pragma once

#include <ctpg/parse-table.hpp>

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

// The user must define:
//   CTPG_TABLE_FILE  — path to the .bin file (string literal)
//
// Example:
//   #define CTPG_TABLE_FILE "build/grammar.tbl"
//   #include <ctpg/table_view/mmap_factory.hpp>

#ifndef CTPG_TABLE_FILE
#  error "CTPG_TABLE_FILE must be defined before including mmap_factory.hpp"
#endif

namespace ctpg
{

namespace detail
{

class mmap_holder
{
public:
    explicit mmap_holder(const char* path)
    {
#if defined(_WIN32)
        HANDLE file = CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error(std::string("ctpg: cannot open file: ") + path);

        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file, &file_size))
        {
            CloseHandle(file);
            throw std::runtime_error(std::string("ctpg: cannot get file size: ") + path);
        }

        HANDLE mapping = CreateFileMappingA(
            file,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr);
        CloseHandle(file);
        if (mapping == nullptr)
            throw std::runtime_error(std::string("ctpg: CreateFileMapping failed: ") + path);

        void* ptr = MapViewOfFile(
            mapping,
            FILE_MAP_READ,
            0,
            0,
            0);
        CloseHandle(mapping);
        if (ptr == nullptr)
            throw std::runtime_error(std::string("ctpg: MapViewOfFile failed: ") + path);

        data_ = static_cast<const std::uint8_t*>(ptr);
        size_ = static_cast<std::size_t>(file_size.QuadPart);
#else
        int fd = ::open(path, O_RDONLY);
        if (fd < 0)
            throw std::runtime_error(std::string("ctpg: cannot open file: ") + path);

        struct stat st;
        if (::fstat(fd, &st) != 0)
        {
            ::close(fd);
            throw std::runtime_error(std::string("ctpg: fstat failed: ") + path);
        }

        void* ptr = ::mmap(
            nullptr,
            static_cast<std::size_t>(st.st_size),
            PROT_READ,
            MAP_PRIVATE,
            fd,
            0);
        ::close(fd);
        if (ptr == MAP_FAILED)
            throw std::runtime_error(std::string("ctpg: mmap failed: ") + path);

        data_ = static_cast<const std::uint8_t*>(ptr);
        size_ = static_cast<std::size_t>(st.st_size);
#endif
    }

    ~mmap_holder()
    {
        if (data_ == nullptr)
            return;
#if defined(_WIN32)
        UnmapViewOfFile(data_);
#else
        ::munmap(const_cast<std::uint8_t*>(data_), size_);
#endif
    }

    mmap_holder(const mmap_holder&) = delete;
    mmap_holder& operator=(const mmap_holder&) = delete;

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

inline mmap_holder& get_mmap_holder()
{
    static mmap_holder holder(CTPG_TABLE_FILE);
    return holder;
}

} // namespace detail

inline parse_table_view make_parse_table_view()
{
    auto& holder = detail::get_mmap_holder();
    return parse_table_view{ holder.data(), holder.size() };
}

} // namespace ctpg
