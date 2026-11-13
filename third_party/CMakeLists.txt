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
