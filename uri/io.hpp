#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_IO_HPP
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_IO_HPP

#include <string>
#include <algorithm> // for copy
#include <cstring> // for memcpy

namespace bbts {
namespace detail {

template<class T> struct type {};

template<class T, class InIt>
inline T read_impl(InIt& start, type<T>) {
    T ret = 0;
    for (int i = 0; i < (int) sizeof(T); ++i) {
        ret <<= 8;
        ret |= static_cast<uint8_t>(*start);
        ++start;
    }
    return ret;
}

template<class InIt>
uint8_t read_impl(InIt& start, type<uint8_t>) {
    return static_cast<uint8_t>(*start++);
}

template<class InIt>
int8_t read_impl(InIt& start, type<int8_t>) {
    return static_cast<int8_t>(*start++);
}

template<class T, class OutIt>
inline void write_impl(T val, OutIt& start) {
    for (int i = (int) sizeof(T) - 1; i >= 0; --i) {
        *start = static_cast<unsigned char>((val >> (i * 8)) & 0xff);
        ++start;
    }
}

}

template<class InIt>
std::string read_string(InIt& start, InIt& end) {
    std::string str;
    while (start != end && *start != '\0') {
        str.push_back(*start++);
    }
    if (start != end) {
        ++start;
    }
    return str;
}

template<class InIt>
int64_t read_int64(InIt& start) {
    return detail::read_impl(start, detail::type<int64_t>());
}

template<class InIt>
uint64_t read_uint64(InIt& start) {
    return detail::read_impl(start, detail::type<uint64_t>());
}

template<class InIt>
uint32_t read_uint32(InIt& start) {
    return detail::read_impl(start, detail::type<uint32_t>());
}

template<class InIt>
int32_t read_int32(InIt& start) {
    return detail::read_impl(start, detail::type<int32_t>());
}

template<class InIt>
int16_t read_int16(InIt& start) {
    return detail::read_impl(start, detail::type<int16_t>());
}

template<class InIt>
uint16_t read_uint16(InIt& start) {
    return detail::read_impl(start, detail::type<uint16_t>());
}

template<class InIt>
int8_t read_int8(InIt& start) {
    return detail::read_impl(start, detail::type<int8_t>());
}

template<class InIt>
uint8_t read_uint8(InIt& start) {
    return detail::read_impl(start, detail::type<uint8_t>());
}

template<class OutIt>
void write_uint64(uint64_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_int64(int64_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_uint32(uint32_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_int32(int32_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_uint16(uint16_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_int16(int16_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_uint8(uint8_t val, OutIt& start) {
    detail::write_impl(val, start);
}

template<class OutIt>
void write_int8(int8_t val, OutIt& start) {
    detail::write_impl(val, start);
}

inline void write_string(std::string const& str, char*& start) {
    std::memcpy((void*) start, str.c_str(), str.size());
    start += str.size();
}

template<class OutIt>
void write_string(std::string const& str, OutIt& start) {
    std::copy(str.begin(), str.end(), start);
}

}

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_IO_HPP
