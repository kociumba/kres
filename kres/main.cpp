#include "main.h"

#include <__msvc_filebuf.hpp>

namespace kres {

bool validate_archive(const byte_vec& data) {
    header h;
    if (parse_header(data, &h) != KRES_OK) {
        return false;
    }

    version_t v = version_decode(h.version);
    version_t current = version_decode(KRES_VERSION);

    return h.magic == KRES_MAGIC && v.major == current.major;
}

// part of the new api, allows for validating from disk
bool validate_archive(const string& filename) {
    archive ar;
    if (!preload_archive(&ar, filename)) return false;

    version_t v = version_decode(ar.header.version);
    version_t current = version_decode(KRES_VERSION);

    return ar.header.magic == KRES_MAGIC && v.major == current.major;
}

bool validate_entry(const entry& entry) {
    uint32_t computed_crc = crc32(entry.data.data(), entry.data.size());
    return computed_crc == entry.crc32;
}

kres_err serialize_archive(const archive& arch, byte_vec* out) {
    byte_writer writer;
    writer.buffer = out;

    writer.write_u32(arch.header.magic);
    writer.write_u32(arch.header.version);
    writer.write_u32(arch.header.flags);
    writer.write_u64(arch.header.entry_count);

    for (const auto& [entry_id, offset] : arch.header.offset_table) {
        writer.write_u64(entry_id);
        writer.write_u64(offset);
    }

    writer.write_u64(arch.header.user_section_size);
    if (arch.header.user_section_size > 0) {
        writer.write_bytes(arch.header.user_section);
    }

    for (const auto& entry : arch.entries) {
        writer.write_u32(entry.filename_len);
        writer.write_string(entry.filename);
        writer.write_u32(entry.crc32);
        writer.write_u64(entry.size);
        writer.write_bytes(entry.data);
    }

    return KRES_OK;
}

kres_err parse_header(const byte_vec& data, header* h) {
    byte_reader reader;
    reader.buffer = &data;
    reader.pos = 0;

    kres_err err;

    err = reader.read_u32(&h->magic);
    if (err != KRES_OK) return err;

    if (h->magic != KRES_MAGIC) {
        return KRES_ERROR_INVALID_ARCHIVE;
    }
    err = reader.read_u32(&h->version);
    if (err != KRES_OK) return err;
    err = reader.read_u32(&h->flags);
    if (err != KRES_OK) return err;
    err = reader.read_u64(&h->entry_count);
    if (err != KRES_OK) return err;

    h->offset_table.reserve(h->entry_count);
    for (uint64_t i = 0; i < h->entry_count; i++) {
        id entry_id;
        uint64_t offset;
        err = reader.read_u64(&entry_id);
        if (err != KRES_OK) return err;
        err = reader.read_u64(&offset);
        if (err != KRES_OK) return err;
        h->offset_table[entry_id] = offset;
    }
    err = reader.read_u64(&h->user_section_size);
    if (err != KRES_OK) return err;

    if (h->user_section_size > 0) {
        err = reader.read_bytes(h->user_section_size, &h->user_section);
        if (err != KRES_OK) return err;
    }

    return KRES_OK;
}

kres_err extract_entry_by_id(const byte_vec& data, const header& h, id entry_id, entry* out) {
    auto it = h.offset_table.find(entry_id);
    if (it == h.offset_table.end()) {
        return KRES_ERROR_ENTRY_NOT_FOUND;
    }

    uint64_t offset = it->second;

    uint32_t filename_len;
    const char* filename_ptr = get_filename_ptr(data, offset, &filename_len);

    out->filename_len = filename_len;
    out->filename.assign(filename_ptr, filename_len);

    byte_reader reader;
    reader.buffer = &data;
    reader.pos = offset + 4 + filename_len + 1;

    kres_err err;

    err = reader.read_u32(&out->crc32);
    if (err != KRES_OK) return err;

    err = reader.read_u64(&out->size);
    if (err != KRES_OK) return err;
    err = reader.read_bytes(out->size, &out->data);
    if (err != KRES_OK) return err;

    return KRES_OK;
}

kres_err build_archive(const vec<entry>& entries, archive* out, const byte_vec* user_data) {
    out->entries = entries;
    out->header.entry_count = entries.size();

    if (user_data) {
        out->header.user_section = *user_data;
        out->header.user_section_size = user_data->size();
    } else {
        out->header.user_section_size = 0;
    }

    size_t header_size = 4 + 4 + 4 + 8;                // magic, version, flags, entry_count
    header_size += entries.size() * 16;                // offset table (id + offset per entry)
    header_size += 8 + out->header.user_section_size;  // user section size + data

    uint64_t current_offset = header_size;

    out->header.offset_table.reserve(entries.size());
    out->header.filename_table.reserve(entries.size());

    for (const auto& entry : entries) {
        id entry_id = generate_id(entry.filename);
        out->header.offset_table[entry_id] = current_offset;
        out->header.filename_table[entry_id] = entry.filename;

        current_offset += 4 + entry.filename.length() + 1 + 4 + 8 + entry.size;
    }

    return serialize_archive(*out, &out->raw_data);
}

kres_err extract_entry_by_name(const byte_vec& data,
                               const header& h,
                               const string& filename,
                               entry* out) {
    id entry_id = generate_id(filename);
    return extract_entry_by_id(data, h, entry_id, out);
}

kres_err extract_filename(const byte_vec& data,
                          const header& h,
                          id entry_id,
                          string* filename_out,
                          uint32_t* len_out) {
    auto it = h.offset_table.find(entry_id);
    if (it == h.offset_table.end()) {
        return KRES_ERROR_ENTRY_NOT_FOUND;
    }

    uint64_t offset = it->second;
    const char* filename_ptr = get_filename_ptr(data, offset, len_out);
    filename_out->assign(filename_ptr, *len_out);

    return KRES_OK;
}

//-------------------------------------//
//          NEW IMPROVED API           //
//-------------------------------------//

archive init_archive() { return {}; }

kres_err make_header(archive* ar) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    header tmp_header;
    tmp_header.flags = ar->header.flags;
    tmp_header.version = ar->header.version;
    tmp_header.user_section_size = ar->header.user_section_size;
    tmp_header.user_section = ar->header.user_section;
    tmp_header.entry_count = ar->entries.size();

    size_t header_size = 4 + 4 + 4 + 8;
    header_size += ar->entries.size() * 16;
    header_size += 8 + ar->header.user_section_size;

    uint64_t current_offset = header_size;

    ar->header.offset_table.reserve(ar->entries.size());
    ar->header.filename_table.reserve(ar->entries.size());

    for (auto& entry : ar->entries) {
        id e_id = generate_id(entry.filename);

        ar->header.offset_table[e_id] = current_offset;
        ar->header.filename_table[e_id] = entry.filename;

        current_offset += 4 + entry.filename.length() + 1 + 4 + 8 + entry.size;
    }

    ar->header = tmp_header;
    return KRES_OK;
}

kres_err append_entry(archive* ar, const entry& e) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    id e_id = generate_id(e.filename);
    if (ar->header.offset_table.contains(e_id)) {
        if (ar->entries[e_id].data == e.data) return KRES_ERROR_DUPLICATE_ENTRY;
        // TODO: trigger some kind of redundancy duplicate id resolution, or could just error out
        // for now i guess ?
        return KRES_ERROR_DUPLICATE_ENTRY;  // for now just error out
    } else {
        ar->entries.push_back(e);
    }

    return make_header(ar);
}

kres_err append_entry(archive* ar, const string& filename, bool recurse) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    entry e;
    using namespace std::filesystem;

    if (is_regular_file(filename)) {
        e.size = file_size(filename);
        e.data.reserve(e.size);

        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) return KRES_ERROR_FAILED_IO;
        file.read(reinterpret_cast<char*>(e.data.data()), e.size);

        return append_entry(ar, e);
    } else if (is_directory(filename)) {
        for (const auto& file : directory_iterator(filename)) {
            if (file.is_directory() && !recurse) continue;
            append_entry(ar, file.path().string(), recurse);
        }
    }

    return KRES_INVALID_STATE;
}

kres_err set_user_data(archive* ar, const byte_vec& ud) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    ar->header.user_section_size = ud.size();
    ar->header.user_section = ud;

    return make_header(ar);
}

kres_err preload_archive(archive* ar, const string& filename) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    header h;
    file_reader r;
    using namespace std::filesystem;
    if (!is_regular_file(filename) || !exists(filename)) return KRES_ERROR_INVALID_ARCHIVE_FILE;

    auto err = r.open(filename.c_str());
    if (!err) return err;

    err = r.read_u32(&h.magic);
    if (!err) return err;

    if (h.magic != KRES_MAGIC) return KRES_ERROR_INVALID_ARCHIVE;

    err = r.read_u32(&h.version);
    if (!err) return err;
    err = r.read_u32(&h.flags);
    if (!err) return err;
    err = r.read_u64(&h.entry_count);
    if (!err) return err;

    h.offset_table.reserve(h.entry_count);
    for (uint64_t i = 0; i < h.entry_count; i++) {
        id e_id;
        uint64_t offset;
        err = r.read_u64(&e_id);
        if (!err) return err;
        err = r.read_u64(&offset);
        if (!err) return err;
        h.offset_table[e_id] = offset;
    }

    err = r.read_u64(&h.user_section_size);
    if (!err) return err;

    if (h.user_section_size > 0) {
        err = r.read_bytes(h.user_section_size, &h.user_section);
        if (!err) return err;
    }

    return KRES_OK;
}

}  // namespace kres