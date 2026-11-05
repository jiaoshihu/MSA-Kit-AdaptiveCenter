cmake_minimum_required(VERSION 3.18)
project(MSAKit VERSION 0.3.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "MSA-Kit 0.3 supports Linux (POSIX I/O and atomic file publication).")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MSAKIT_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
include(CTest)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# All header-only dependencies are pinned and shipped with the source tree.
# Configuration does not download dependencies or invoke external aligners.
add_library(msakit_build_options INTERFACE)
target_compile_options(msakit_build_options INTERFACE
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>")
if(MSAKIT_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang.")
    endif()
    # Even an empty ASan-instrumented PIE intermittently failed at startup on
    # the validation host. Use a fixed executable layout only in this diagnostic
    # build; keep all sanitizer checks and the normal Release layout unchanged.
    target_compile_options(msakit_build_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    target_link_options(msakit_build_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie)
endif()

add_library(msakit_support STATIC
    src/cli.cpp src/input.cpp src/output.cpp src/validation.cpp)
target_include_directories(msakit_support PUBLIC include PRIVATE src)
target_include_directories(msakit_support SYSTEM PRIVATE
    extern/CLI11/include extern/spdlog/include extern/kseq)
target_compile_definitions(msakit_support PRIVATE MSAKIT_VERSION="${PROJECT_VERSION}")
target_link_libraries(msakit_support PRIVATE msakit_build_options ZLIB::ZLIB Threads::Threads)

# Replace this source to supply a different algorithm; the surrounding library
# and executable keep the same input, output, validation and command-line rules.
add_library(msakit_core STATIC src/align.cpp)
target_include_directories(msakit_core PUBLIC include PRIVATE src)
target_link_libraries(msakit_core PRIVATE msakit_build_options)

add_executable(msakit src/main.cpp)
target_include_directories(msakit PRIVATE src)
target_link_libraries(msakit PRIVATE msakit_support msakit_core msakit_build_options)
install(TARGETS msakit RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_executable(msakit_unit_tests
        tests/doctest_main.cpp tests/test_core.cpp tests/test_io.cpp tests/test_kseq.cpp)
    target_include_directories(msakit_unit_tests PRIVATE src)
    target_include_directories(msakit_unit_tests SYSTEM PRIVATE extern/doctest/include extern/kseq)
    target_link_libraries(msakit_unit_tests PRIVATE
        msakit_support msakit_core msakit_build_options ZLIB::ZLIB)
    add_test(NAME unit.contract COMMAND msakit_unit_tests --test-suite-exclude=reference)
    add_test(NAME unit.reference COMMAND msakit_unit_tests --test-suite=reference)

    # Faulty cores only exist in this test executable, never as production flags.
    add_executable(msakit_test_driver tests/fake_core_main.cpp)
    target_include_directories(msakit_test_driver PRIVATE src)
    target_link_libraries(msakit_test_driver PRIVATE msakit_support msakit_build_options)
    foreach(suite IN ITEMS contract reference)
        add_test(NAME integration.${suite} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py"
            --binary $<TARGET_FILE:msakit>
            --test-driver $<TARGET_FILE:msakit_test_driver> --suite ${suite})
    endforeach()
    set_tests_properties(unit.contract integration.contract PROPERTIES LABELS contract)
    set_tests_properties(unit.reference integration.reference PROPERTIES LABELS reference)
    set_tests_properties(unit.contract unit.reference integration.contract integration.reference
        PROPERTIES TIMEOUT 120)
    if(MSAKIT_ENABLE_SANITIZERS)
        set_tests_properties(unit.contract unit.reference integration.contract integration.reference
            PROPERTIES ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()
endif()
