#ifndef KRES_MAIN_H
#define KRES_MAIN_H

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <xxhash.h>
#include "hash/crc32.h"

#include "types.h"
#include "utility.h"

#define VERSION_ENCODE(maj, min, pat) \
    (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | (uint32_t)(pat))

namespace kres {

constexpr uint32_t KRES_VERSION = VERSION_ENCODE(0, 0, 1);
constexpr uint32_t KRES_MAGIC =
    0x4B524553;  // ascii for "KRES" (reversed in archives, due to endianness)

struct version_t {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

inline version_t version_decode(uint32_t version) {
    version_t v;
    v.major = (uint8_t)(version >> 16);
    v.minor = (uint8_t)(version >> 8);
    v.patch = (uint8_t)version;
    return v;
}

struct entry {
    uint32_t filename_len;
    string filename;  // needs to be null terminated
    uint32_t crc32;
    uint64_t size;
    byte_vec data;
};

// the ids and offsets are in this pattern to make access easier here, in memory we store them as:
// id, offset, id, offset... | 8bytes, 8bytes, 8bytes, 8bytes...
struct header {
    uint32_t magic = KRES_MAGIC;      // identify valid kres archives
    uint32_t version = KRES_VERSION;  // to detect changes in api
    uint32_t flags = 0;               // currently unused, reserved for later
    uint64_t entry_count = 0;
    map<id, uint64_t> offset_table;  // in file stored side by side with the offsets
    uint64_t user_section_size = 0;
    byte_vec user_section;  // user section contains arbitrary data the user might want to embed

    // utility fields not stored in the format
    map<id, string> filename_table;  // will not be populated if the archive is not fully parsed
};

// defines the structure of a kres archive, serializes/deserialized with specific functions to and
// from byte_vec
struct archive {
    byte_vec raw_data;

    // format fields
    header header;

    // end of header, data section
    vec<entry> entries;
};

[[deprecated]] bool validate_archive(
    const byte_vec& data);  // validates the magic number, the header and that the major version is
                            // the same as the current one in KRES_VERSION
bool validate_archive(const string& filename);
bool validate_entry(const entry& entry);  // uses the crc32 sum to validate singular entries

inline id generate_id(const string& filename) {
    return XXH3_64bits(filename.c_str(), filename.length());
}

inline const char* get_filename_ptr(const byte_vec& data, uint64_t offset, uint32_t* len_out) {
    *len_out = static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);

    return reinterpret_cast<const char*>(data.data() + offset + 4);
}

kres_err serialize_archive(const archive& arch, byte_vec* out);
[[deprecated("use preload_archive instead")]] kres_err parse_header(const byte_vec& data,
                                                                    header* h);
[[deprecated]] kres_err extract_entry_by_id(const byte_vec& data,
                                            const header& h,
                                            id entry_id,
                                            entry* out);
[[deprecated]] kres_err build_archive(const vec<entry>& entries,
                                      archive* out,
                                      const byte_vec* user_data = nullptr);
[[deprecated]] kres_err extract_entry_by_name(const byte_vec& data,
                                              const header& h,
                                              const string& filename,
                                              entry* out);
[[deprecated]] kres_err extract_filename(const byte_vec& data,
                                         const header& h,
                                         id entry_id,
                                         string* filename_out,
                                         uint32_t* len_out);

//-------------------------------------//
//          NEW IMPROVED API           //
//-------------------------------------//

// useless in c++, will most likely be used for c bindings
archive init_archive();

// regenerate the header with new offsets, should be called after each operation that might shift
// data
kres_err make_header(archive* ar);
kres_err append_entry(archive* ar, const entry& e);
kres_err append_entry(archive* ar, const string& filename, bool recurse = false);
kres_err set_user_data(archive* ar, const byte_vec& ud);

// does the same as parse_header, but uses a better reader, which does not load the whole archive
// into memory
kres_err preload_archive(archive* ar, const string& filename);

}  // namespace kres

#endif  // KRES_MAIN_H
