add_executable(savetinker_tests
    main.cpp
    test_authoring.cpp
    test_backup.cpp
    test_byte_buffer.cpp
    test_checksum.cpp
    test_code_inspect.cpp
    test_document.cpp
    test_editor_session.cpp
    test_editor_store.cpp
    test_editor_template.cpp
    test_example_editor.cpp
    test_formats.cpp
    test_game_data.cpp
    test_game_info.cpp
    test_i18n.cpp
    test_manifest.cpp
    test_network.cpp
    test_pc_platform.cpp
    test_repository.cpp
    test_script_host.cpp
    test_smdh.cpp
    test_version.cpp
)
target_link_libraries(savetinker_tests PRIVATE savetinker_platform doctest)
target_compile_options(savetinker_tests PRIVATE -Wall -Wextra)
target_compile_definitions(savetinker_tests PRIVATE SAVETINKER_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    SAVETINKER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME savetinker_tests COMMAND savetinker_tests)

add_test(NAME cli_validate_sample COMMAND savetinker-cli validate ${PROJECT_SOURCE_DIR}/examples/editors/sample-game)
add_test(NAME cli_run_sample COMMAND savetinker-cli run ${PROJECT_SOURCE_DIR}/examples/editors/sample-game
    ${PROJECT_SOURCE_DIR}/examples/console/saves/0100000000001000/account-00000000000000010000000000000001
    --set Player/Gold=4321)
set_tests_properties(cli_run_sample PROPERTIES PASS_REGULAR_EXPRESSION "Gold = 4321")
