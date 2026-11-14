add_library(savetinker_core STATIC
    src/authoring.cpp
    src/backup.cpp
    src/byte_buffer.cpp
    src/checksum.cpp
    src/code_inspect.cpp
    src/document.cpp
    src/editor_session.cpp
    src/editor_store.cpp
    src/editor_template.cpp
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
target_include_directories(savetinker_core PUBLIC include PRIVATE src)
target_link_libraries(savetinker_core PUBLIC nlohmann_json PRIVATE lua miniz qrcodegen)
target_compile_options(savetinker_core PRIVATE -Wall -Wextra)
target_compile_definitions(savetinker_core PUBLIC SAVETINKER_VERSION="${PROJECT_VERSION}")
