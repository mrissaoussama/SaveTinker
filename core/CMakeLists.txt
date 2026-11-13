add_library(savetinker_core STATIC
    src/backup.cpp
    src/byte_buffer.cpp
    src/checksum.cpp
    src/document.cpp
    src/lua_save_api.cpp
    src/lua_util_api.cpp
    src/manifest.cpp
    src/platform.cpp
    src/repository.cpp
    src/script_host.cpp
    src/version.cpp
)
target_include_directories(savetinker_core PUBLIC include PRIVATE src)
target_link_libraries(savetinker_core PUBLIC nlohmann_json PRIVATE lua)
target_compile_options(savetinker_core PRIVATE -Wall -Wextra)
target_compile_definitions(savetinker_core PUBLIC SAVETINKER_VERSION="${PROJECT_VERSION}")
