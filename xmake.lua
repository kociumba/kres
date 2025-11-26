add_rules("mode.debug", "mode.release")

set_languages("c99", "c++23")

add_requires("xxhash")
add_requires("catch2")

target("kres")
    set_kind("static")
    add_files("kres/**.c", "kres/**.cpp")
    add_includedirs("include", {interface = true})
    add_packages("xxhash", {public = true})

target("catch2_test")
    set_kind("binary")
    add_files("tests/*.cpp")
    add_packages("catch2")
    add_deps("kres")
    add_links("kres")
    add_defines("CMAKE_BINARY_DIR=\"$(builddir)\"")