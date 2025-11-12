#ifndef KRES_UTILITY_H
#define KRES_UTILITY_H

#include "types.h"

namespace kres {

inline uint32_t host_to_le32(uint32_t val) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(val);
#else
    return val;
#endif
}

inline uint64_t host_to_le64(uint64_t val) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap64(val);
#else
    return val;
#endif
}

inline uint32_t le32_to_host(uint32_t val) { return host_to_le32(val); }

inline uint64_t le64_to_host(uint64_t val) { return host_to_le64(val); }

struct byte_writer {
    byte_vec* buffer;

    void write_u32(uint32_t val) {
        val = host_to_le32(val);
        buffer->push_back(static_cast<std::byte>(val & 0xFF));
        buffer->push_back(static_cast<std::byte>((val >> 8) & 0xFF));
        buffer->push_back(static_cast<std::byte>((val >> 16) & 0xFF));
        buffer->push_back(static_cast<std::byte>((val >> 24) & 0xFF));
    }

    void write_u64(uint64_t val) {
        val = host_to_le64(val);
        for (int i = 0; i < 8; i++) {
            buffer->push_back(static_cast<std::byte>((val >> (i * 8)) & 0xFF));
        }
    }

    void write_bytes(const byte_vec& data) {
        buffer->insert(buffer->end(), data.begin(), data.end());
    }

    void write_string(const string& str) {
        for (char c : str) {
            buffer->push_back(static_cast<std::byte>(c));
        }
        buffer->push_back(std::byte{0});
    }
};

struct byte_reader {
    const byte_vec* buffer;
    size_t pos;

    kres_err read_u32(uint32_t* out) {
        if (pos + 4 > buffer->size()) return KRES_ERROR_BUFFER_OVERFLOW;
        *out = static_cast<uint32_t>((*buffer)[pos]) |
               (static_cast<uint32_t>((*buffer)[pos + 1]) << 8) |
               (static_cast<uint32_t>((*buffer)[pos + 2]) << 16) |
               (static_cast<uint32_t>((*buffer)[pos + 3]) << 24);
        *out = le32_to_host(*out);
        pos += 4;
        return KRES_OK;
    }

    kres_err read_u64(uint64_t* out) {
        if (pos + 8 > buffer->size()) return KRES_ERROR_BUFFER_OVERFLOW;
        *out = 0;
        for (int i = 0; i < 8; i++) {
            *out |= static_cast<uint64_t>((*buffer)[pos + i]) << (i * 8);
        }
        *out = le64_to_host(*out);
        pos += 8;
        return KRES_OK;
    }

    kres_err read_string(string* out) {
        out->clear();
        while (pos < buffer->size() && (*buffer)[pos] != std::byte{0}) {
            *out += static_cast<char>((*buffer)[pos++]);
        }
        if (pos < buffer->size()) pos++;
        return KRES_OK;
    }

    kres_err read_bytes(size_t count, byte_vec* out) {
        if (pos + count > buffer->size()) return KRES_ERROR_BUFFER_OVERFLOW;
        out->assign(buffer->begin() + pos, buffer->begin() + pos + count);
        pos += count;
        return KRES_OK;
    }

    size_t tell() const { return pos; }
    void seek(size_t new_pos) { pos = new_pos; }
};

struct file_reader {
    std::ifstream file;

    file_reader() {}

    kres_err open(const char* path) {
        file.open(path, std::ios::binary);
        if (!file.is_open()) return KRES_ERROR_INVALID_INPUT_FILE;
        return KRES_OK;
    }

    void close() {
        if (file.is_open()) {
            file.close();
        }
    }

    ~file_reader() { close(); }

    kres_err read_u32(uint32_t* out) {
        uint8_t buf[4];
        file.read(reinterpret_cast<char*>(buf), 4);
        if (file.gcount() != 4) {
            return file.eof() ? KRES_ERROR_EOF : KRES_ERROR_FAILED_IO;
        }
        *out = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
               (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
        *out = le32_to_host(*out);
        return KRES_OK;
    }

    kres_err read_u64(uint64_t* out) {
        uint8_t buf[8];
        file.read(reinterpret_cast<char*>(buf), 8);
        if (file.gcount() != 8) {
            return file.eof() ? KRES_ERROR_EOF : KRES_ERROR_FAILED_IO;
        }
        *out = 0;
        for (int i = 0; i < 8; i++) {
            *out |= static_cast<uint64_t>(buf[i]) << (i * 8);
        }
        *out = le64_to_host(*out);
        return KRES_OK;
    }

    kres_err read_string(string* out) {
        out->clear();
        char c;
        while (file.get(c) && c != 0) {
            *out += c;
        }
        if (file.bad()) return KRES_ERROR_FAILED_IO;
        return KRES_OK;
    }

    kres_err read_bytes(size_t count, byte_vec* out) {
        out->resize(count);
        file.read(reinterpret_cast<char*>(out->data()), count);
        size_t read = file.gcount();
        if (read != count) {
            out->resize(read);
            return file.eof() ? KRES_ERROR_EOF : KRES_ERROR_FAILED_IO;
        }
        return KRES_OK;
    }

    kres_err tell(size_t* out) {
        auto pos = file.tellg();
        if (pos < 0) return KRES_ERROR_FAILED_IO;
        *out = static_cast<size_t>(pos);
        return KRES_OK;
    }

    kres_err seek(size_t new_pos) {
        file.seekg(static_cast<std::streamoff>(new_pos), std::ios::beg);
        if (file.fail()) {
            file.clear();
            return KRES_ERROR_FAILED_IO;
        }
        return KRES_OK;
    }

    kres_err skip(size_t bytes) {
        file.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
        if (file.fail()) {
            file.clear();
            return KRES_ERROR_FAILED_IO;
        }
        return KRES_OK;
    }
};

}  // namespace kres

#endif  // KRES_UTILITY_H
