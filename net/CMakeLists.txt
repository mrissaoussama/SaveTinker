find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

add_library(savetinker_net STATIC curl_http_client.cpp)
target_include_directories(savetinker_net PUBLIC .)
target_link_libraries(savetinker_net PUBLIC savetinker_core PRIVATE PkgConfig::CURL)
target_compile_options(savetinker_net PRIVATE -Wall -Wextra)
