add_executable(savetinker-cli main.cpp)
target_link_libraries(savetinker-cli PRIVATE savetinker_platform)
target_compile_options(savetinker-cli PRIVATE -Wall -Wextra)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # std::print's terminal support lives in stdc++exp on MinGW.
    target_link_libraries(savetinker-cli PRIVATE stdc++exp)
    target_link_options(savetinker-cli PRIVATE -static)
endif()
