#include <kres.h>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iostream>

using namespace kres;

TEST_CASE("Create archive", "[archive]") {
    vec<entry> entries;

    entry e1;
    e1.filename = "test.txt";
    e1.filename_len = static_cast<uint32_t>(e1.filename.length());
    e1.data = {std::byte('h'), std::byte('e'), std::byte('l'), std::byte('l'), std::byte('o')};
    e1.size = e1.data.size();
    e1.crc32 = crc32(e1.data.data(), e1.size);
    entries.push_back(e1);

    entry e2;
    e2.filename = "foo.bar";
    e2.filename_len = static_cast<uint32_t>(e2.filename.length());
    e2.data = {std::byte('f'), std::byte('o'), std::byte('o')};
    e2.size = e2.data.size();
    e2.crc32 = crc32(e2.data.data(), e2.size);
    entries.push_back(e2);

    archive arch;
    REQUIRE(build_archive(entries, &arch) == KRES_OK);

    std::string file_path = std::string(CMAKE_BINARY_DIR) + "/test.kres";
    std::ofstream file(file_path, std::ios::binary);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(arch.raw_data.data()), arch.raw_data.size());
    file.close();
}

TEST_CASE("Read archive", "[archive]") {
    std::string file_path = std::string(CMAKE_BINARY_DIR) + "/test.kres";
    std::ifstream in(file_path, std::ios::binary);
    REQUIRE(in.is_open());

    in.seekg(0, std::ios::end);
    size_t file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    byte_vec data(file_size);
    in.read(reinterpret_cast<char*>(data.data()), file_size);
    in.close();

    header h;
    REQUIRE(parse_header(data, &h) == KRES_OK);

    std::cout << "Found filenames in archive:" << std::endl;
    for (const auto& [entry_id, offset] : h.offset_table) {
        string filename;
        uint32_t len;
        REQUIRE(extract_filename(data, h, entry_id, &filename, &len) == KRES_OK);
        std::cout << "- " << filename << std::endl;
    }
}