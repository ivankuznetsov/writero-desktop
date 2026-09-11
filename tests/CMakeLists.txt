find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

function(writero_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE writero_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/runtime"
    )
endfunction()

writero_add_test(tst_app_info core/tst_app_info.cpp)
