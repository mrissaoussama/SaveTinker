cmake_minimum_required(VERSION 3.25)

project(SaveTinker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NINTENDO_SWITCH)
    set(SAVETINKER_PLATFORM switch)
else()
    set(SAVETINKER_PLATFORM pc)
endif()

include(CMakeDependentOption)
cmake_dependent_option(SAVETINKER_BUILD_TESTS "Build unit tests" ON "NOT NINTENDO_SWITCH" OFF)

add_subdirectory(third_party)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(net)

if(NINTENDO_SWITCH)
    add_subdirectory(app/switch)
endif()

if(SAVETINKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
