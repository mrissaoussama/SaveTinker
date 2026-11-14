file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c)
# Scripts never get io, os, package or the stock library loader.
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(liolib|loslib|loadlib|linit)\\.c$")
# Built as C++ so lua_error unwinds with exceptions instead of longjmp.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua)
target_compile_options(lua PRIVATE -w -include climits)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann)

add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

add_library(miniz STATIC miniz/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniz)
target_compile_options(miniz PRIVATE -w)

add_library(qrcodegen STATIC qrcodegen/qrcodegen.cpp)
target_include_directories(qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/qrcodegen)
target_compile_options(qrcodegen PRIVATE -w)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
target_include_directories(imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_compile_options(imgui PRIVATE -w)
if(NINTENDO_SWITCH OR NINTENDO_3DS)
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
endif()

# zstd 1.5.7, single threaded.
add_library(zstd STATIC
    zstd/common/entropy_common.c
    zstd/common/error_private.c
    zstd/common/fse_decompress.c
    zstd/common/xxhash.c
    zstd/common/zstd_common.c
    zstd/decompress/huf_decompress.c
    zstd/decompress/zstd_ddict.c
    zstd/decompress/zstd_decompress.c
    zstd/decompress/zstd_decompress_block.c
    zstd/compress/fse_compress.c
    zstd/compress/hist.c
    zstd/compress/huf_compress.c
    zstd/compress/zstd_compress.c
    zstd/compress/zstd_compress_literals.c
    zstd/compress/zstd_compress_sequences.c
    zstd/compress/zstd_compress_superblock.c
    zstd/compress/zstd_double_fast.c
    zstd/compress/zstd_fast.c
    zstd/compress/zstd_lazy.c
    zstd/compress/zstd_ldm.c
    zstd/compress/zstd_opt.c
    zstd/compress/zstd_preSplit.c
)
target_include_directories(zstd SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/zstd)
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_LEGACY_SUPPORT=0)
target_compile_options(zstd PRIVATE -w)
