set(SAVETINKER_UPDATE_REPO "" CACHE STRING "GitHub owner/repo that publishes SaveTinker releases")
option(SAVETINKER_AUTOPILOT "Replay <data>/autopilot.txt and save screenshots (for testing)" OFF)

file(GLOB LANG_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lang/*.json)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lang_data.hpp
    COMMAND ${CMAKE_COMMAND} -DLANG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/lang -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/lang_data.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_lang.cmake
    DEPENDS ${LANG_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/embed_lang.cmake
)

set(APP_SOURCES
    app.cpp
    i18n.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/lang_data.hpp
    save_flow.cpp
    screens_editor.cpp
    screens_game.cpp
    screens_creator.cpp
    screens_hex.cpp
    screens_main.cpp
    screens_repos.cpp
    textures.cpp
    ui.cpp
)
set(SDL_SOURCES
    main.cpp
    gpu_gl.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl2.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp
)
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_sdl2.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp
    PROPERTIES COMPILE_OPTIONS -w)

if(NINTENDO_3DS)
    ctr_add_shader_library(imgui_shader shaders/imgui.v.pica)
    dkp_add_embedded_binary_library(imgui_shader_binary imgui_shader)
    add_executable(SaveTinker ${APP_SOURCES} main_3ds.cpp system_3ds.cpp imgui_impl_citro3d.cpp)
    target_link_libraries(SaveTinker PRIVATE imgui_shader_binary citro3d ctru)
else()
    find_package(PkgConfig REQUIRED)
    if(NINTENDO_SWITCH2)
        # The Switch SDL2 package names libnx and Mesa in its pkg-config file; on Switch 2 the same library
        # links against libounce and OunceVK's EGL instead.
        find_library(SDL2_LIBRARY SDL2 REQUIRED)
        find_path(SDL2_INCLUDE SDL.h PATH_SUFFIXES SDL2 REQUIRED)
        pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
        add_library(sdl2_switch2 INTERFACE)
        target_include_directories(sdl2_switch2 INTERFACE ${SDL2_INCLUDE})
        target_link_libraries(sdl2_switch2 INTERFACE ${SDL2_LIBRARY} PkgConfig::EGL)
        add_library(PkgConfig::SDL2 ALIAS sdl2_switch2)
    else()
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    endif()

    if(NINTENDO_SWITCH)
        pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
        add_executable(SaveTinker ${APP_SOURCES} ${SDL_SOURCES} system_switch.cpp)
        target_compile_definitions(SaveTinker PRIVATE IMGUI_IMPL_OPENGL_ES3)
        target_link_libraries(SaveTinker PRIVATE PkgConfig::SDL2 PkgConfig::GLES)
    else()
        add_executable(SaveTinker ${APP_SOURCES} ${SDL_SOURCES} system_pc.cpp)
        target_compile_definitions(SaveTinker PRIVATE SAVETINKER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
        target_link_libraries(SaveTinker PRIVATE PkgConfig::SDL2)
        if(WIN32)
            target_link_libraries(SaveTinker PRIVATE opengl32)
        else()
            find_package(OpenGL REQUIRED)
            target_link_libraries(SaveTinker PRIVATE OpenGL::GL)
        endif()
    endif()
endif()

target_link_libraries(SaveTinker PRIVATE savetinker_platform savetinker_net imgui stb)
target_include_directories(SaveTinker PRIVATE . ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(SaveTinker PRIVATE -Wall -Wextra)
if(SAVETINKER_AUTOPILOT)
    target_sources(SaveTinker PRIVATE autopilot.cpp)
    target_compile_definitions(SaveTinker PRIVATE SAVETINKER_AUTOPILOT)
endif()
if(SAVETINKER_UPDATE_REPO)
    target_compile_definitions(SaveTinker PRIVATE SAVETINKER_UPDATE_REPO="${SAVETINKER_UPDATE_REPO}")
endif()

if(NINTENDO_3DS)
    ctr_generate_smdh(SaveTinker.smdh
        NAME "SaveTinker"
        DESCRIPTION "Save editor"
        AUTHOR "SaveTinker"
    )
    ctr_create_3dsx(SaveTinker SMDH SaveTinker.smdh)
elseif(NINTENDO_SWITCH)
    nx_generate_nacp(SaveTinker.nacp
        NAME "SaveTinker"
        AUTHOR "SaveTinker"
        VERSION "${PROJECT_VERSION}"
    )
    nx_create_nro(SaveTinker NACP SaveTinker.nacp)
endif()
