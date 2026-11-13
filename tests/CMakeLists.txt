add_executable(savetinker_tests
    main.cpp
    test_backup.cpp
    test_byte_buffer.cpp
    test_checksum.cpp
    test_document.cpp
    test_example_editor.cpp
    test_manifest.cpp
    test_pc_platform.cpp
    test_repository.cpp
    test_script_host.cpp
    test_version.cpp
)
target_link_libraries(savetinker_tests PRIVATE savetinker_platform doctest)
target_compile_options(savetinker_tests PRIVATE -Wall -Wextra)
target_compile_definitions(savetinker_tests PRIVATE SAVETINKER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
add_test(NAME savetinker_tests COMMAND savetinker_tests)
