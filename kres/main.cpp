#include "main.h"

#define check(expr) \
    if (err = expr; err != KRES_OK) return err

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
    if (auto err = preload_archive(&ar, filename); err != KRES_OK) return false;

    version_t v = version_decode(ar.header.version);
    version_t current = version_decode(KRES_VERSION);

    return ar.header.magic == KRES_MAGIC && v.major == current.major;
}

// TODO: needs an off ram impl
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
        ar->header.insert_order.emplace_back(e_id);
        ar->entries.push_back(e);
        ar->header.entry_count++;
        ar->header.offset_table[e_id] = 0;  // placeholder
    }

    return KRES_OK;
}

kres_err append_entry(archive* ar, const string& filename, const string& ar_path, bool recurse) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    entry e;
    using namespace std::filesystem;
    path abs_path = absolute(filename);

    if (!exists(abs_path)) return KRES_ERROR_INVALID_INPUT_FILE;

    if (is_regular_file(abs_path)) {
        e.size = file_size(abs_path);

        e.abs_path = abs_path.string();
        auto in_ar_path = ar_path.empty() ? abs_path.filename().string() : ar_path;
        in_ar_path = sanitize_ar_path(in_ar_path);

        e.filename = in_ar_path;
        e.filename_len = in_ar_path.length();

        return append_entry(ar, e);
    } else if (is_directory(abs_path)) {
        string ar_prefix = ar_path;
        if (!ar_prefix.empty() && ar_prefix.back() != '/') ar_prefix += '/';

        bool has_entries = false;
        for (const auto& sub : directory_iterator(abs_path)) {
            path sub_path = sub.path();
            string sub_rel = sub_path.filename().string();
            string new_ar_path = ar_prefix + sub_rel;

            kres_err err = append_entry(ar, sub_path.string(), new_ar_path, recurse);
            if (err == KRES_OK)
                has_entries = true;
            else if (err != KRES_INVALID_STATE)
                return err;
        }

        return KRES_OK;
    }

    return KRES_INVALID_STATE;
}

kres_err set_user_data(archive* ar, const byte_vec& ud) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    ar->header.user_section_size = ud.size();
    ar->header.user_section = ud;

    return KRES_OK;
}

kres_err preload_archive(archive* ar, const string& filename) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    header h;
    file_reader r;
    using namespace std::filesystem;

    if (!is_regular_file(filename) || !exists(filename)) return KRES_ERROR_INVALID_ARCHIVE_FILE;

    kres_err err;
    check(r.open(filename.c_str()));
    check(r.read_u32(&h.magic));

    if (h.magic != KRES_MAGIC) return KRES_ERROR_INVALID_ARCHIVE;

    check(r.read_u32(&h.version));
    check(r.read_u32(&h.flags));
    check(r.read_u64(&h.entry_count));

    h.offset_table.reserve(h.entry_count);
    for (uint64_t i = 0; i < h.entry_count; i++) {
        id e_id;
        uint64_t offset;
        check(r.read_u64(&e_id));
        check(r.read_u64(&offset));
        h.offset_table[e_id] = offset;
    }

    check(r.read_u64(&h.user_section_size));

    if (h.user_section_size > 0) {
        check(r.read_bytes(h.user_section_size, &h.user_section));
    }

    return KRES_OK;
}

kres_err render_archive(archive* ar, const string& filename) {
    if (!ar) return KRES_ERROR_INVALID_ARCHIVE;

    using namespace std::filesystem;
    if (!is_regular_file(filename)) return KRES_ERROR_INVALID_ARCHIVE_FILE;

    kres_err err;
    file_writer ar_w;
    check(ar_w.open(filename.c_str()));

    uint64_t header_offset = 4 + 4 + 4 + 8;
    uint64_t offset_table = header_offset;
    header_offset += ar->entries.size() * 16;
    header_offset += 8 + ar->header.user_section_size;

    uint64_t ar_offset = header_offset;

    check(ar_w.write_u32(ar->header.magic));
    check(ar_w.write_u32(ar->header.version));
    check(ar_w.write_u32(ar->header.flags));
    check(ar_w.write_u64(ar->header.entry_count));

    for (auto& e_id : ar->header.insert_order) {
        check(ar_w.write_u64(e_id));
        check(ar_w.write_u64(0));
    }

    check(ar_w.write_u64(ar->header.user_section_size));
    check(ar_w.write_bytes(ar->header.user_section));

    for (auto& e : ar->entries) {
        file_reader r;
        check(r.open(e.abs_path.c_str()));
        check(ar_w.write_u32(e.filename_len));
        check(ar_w.write_string(e.filename));

        check(ar_w.write_u32(0));

        check(ar_w.write_u64(e.size));
        check(ar_w.write_from_reader(&r, e.size, &e.crc32));

        check(ar_w.seek(ar_offset + 4 + e.filename_len));
        check(ar_w.write_u32(e.crc32));

        id e_id = generate_id(e.filename);
        ar->header.offset_table[e_id] = ar_offset;
        ar_offset += 4 + e.filename.length() + 1 + 4 + 8 + e.size;  // move to next entry
        check(ar_w.seek(ar_offset));
    }

    check(ar_w.seek(offset_table));

    for (auto& e_id : ar->header.insert_order) {
        check(ar_w.write_u64(e_id));
        check(ar_w.write_u64(ar->header.offset_table[e_id]));
    }

    return KRES_OK;
}

}  // namespace kres