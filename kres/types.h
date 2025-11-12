#ifndef KRES_TYPES_H
#define KRES_TYPES_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kres {

enum kres_err {
    KRES_INVALID_STATE = -1,
    KRES_OK = 0,
    KRES_ERROR_BUFFER_OVERFLOW,
    KRES_ERROR_INVALID_ARCHIVE,
    KRES_ERROR_INVALID_ARCHIVE_FILE,
    KRES_ERROR_MISMATCHED_VERSION,
    KRES_ERROR_ENTRY_NOT_FOUND,
    KRES_ERROR_DUPLICATE_ENTRY,
    KRES_ERROR_ENTRY_CORRUPTED,
    KRES_ERROR_FAILED_IO,
    KRES_ERROR_EOF,
    KRES_ERROR_INVALID_INPUT_FILE,
};

template <typename T>
using vec = std::vector<T>;
template <typename K, typename V>
using map = std::unordered_map<K, V>;
using id = uint64_t;  // id is an xxhashH3 of the filename, some collisions are bound to happen but
// they are minimized by the 64 bit width and filepath truncation to the root
// of the archive
template <typename F, typename S>
using pair = std::pair<F, S>;
using string = std::string;
using byte_vec = vec<std::byte>;

}  // namespace kres

#endif  // KRES_TYPES_H
