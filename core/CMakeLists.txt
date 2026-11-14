file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.lua ${CMAKE_CURRENT_SOURCE_DIR}/lua/modules/*.lua)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lua_sources.hpp
    COMMAND ${CMAKE_COMMAND} -DLUA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/lua -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/lua_sources.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_lua.cmake
    DEPENDS ${LUA_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/embed_lua.cmake
)

add_library(savetinker_core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/lua_sources.hpp
    src/authoring.cpp
    src/backup.cpp
    src/byte_buffer.cpp
    src/checksum.cpp
    src/code_inspect.cpp
    src/compression.cpp
    src/crypto.cpp
    src/document.cpp
    src/editor_session.cpp
    src/editor_store.cpp
    src/editor_template.cpp
    src/file_saves.cpp
    src/formats.cpp
    src/game_data.cpp
    src/i18n.cpp
    src/lua_save_api.cpp
    src/lua_util_api.cpp
    src/manifest.cpp
    src/network.cpp
    src/platform.cpp
    src/qr.cpp
    src/repository.cpp
    src/script_host.cpp
    src/settings.cpp
    src/smdh.cpp
    src/version.cpp
)
target_include_directories(savetinker_core PUBLIC include PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(savetinker_core PUBLIC nlohmann_json PRIVATE lua miniz qrcodegen zstd)
target_compile_options(savetinker_core PRIVATE -Wall -Wextra)
target_compile_definitions(savetinker_core PUBLIC SAVETINKER_VERSION="${PROJECT_VERSION}")
