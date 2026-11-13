add_executable(SaveTinker main.cpp)
target_link_libraries(SaveTinker PRIVATE savetinker_platform)
target_compile_options(SaveTinker PRIVATE -Wall -Wextra)

nx_generate_nacp(SaveTinker.nacp
    NAME "SaveTinker"
    AUTHOR "SaveTinker"
    VERSION "${PROJECT_VERSION}"
)

nx_create_nro(SaveTinker
    NACP SaveTinker.nacp
)
