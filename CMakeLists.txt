# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(ImperativeLanguageCompiler CXX)

# Set C++ standard (Binaryen requires C++17+)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")

# Find Binaryen
# Binaryen can be found either as a system package or built from source
# Option 1: If Binaryen is installed system-wide
find_path(BINARYEN_INCLUDE_DIR
    NAMES wasm.h
    PATHS
        /usr/include
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/binaryen/src
    PATH_SUFFIXES binaryen
)

find_library(BINARYEN_LIBRARY
    NAMES binaryen
    PATHS
        /usr/lib
        /usr/local/lib
        ${CMAKE_SOURCE_DIR}/binaryen/lib
)

# Option 2: If Binaryen is in a subdirectory (for development)
if(NOT BINARYEN_INCLUDE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/binaryen")
    set(BINARYEN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/binaryen/src")
    set(BINARYEN_LIBRARY "${CMAKE_SOURCE_DIR}/binaryen/lib/libbinaryen.a")
endif()

if(BINARYEN_INCLUDE_DIR AND BINARYEN_LIBRARY)
    message(STATUS "Found Binaryen: ${BINARYEN_INCLUDE_DIR}")
    include_directories(${BINARYEN_INCLUDE_DIR})
else()
    message(WARNING "Binaryen not found. Please install Binaryen or set BINARYEN_INCLUDE_DIR and BINARYEN_LIBRARY")
    message(STATUS "You can install Binaryen by:")
    message(STATUS "  1. Building from source: git clone https://github.com/WebAssembly/binaryen.git && cd binaryen && cmake . && make")
    message(STATUS "  2. Or set BINARYEN_INCLUDE_DIR and BINARYEN_LIBRARY CMake variables")
endif()

# Find required packages
find_package(PkgConfig)

# Find Flex and Bison
find_program(FLEX_EXECUTABLE flex)
find_program(BISON_EXECUTABLE bison)

if(NOT FLEX_EXECUTABLE)
    message(FATAL_ERROR "Flex not found! Install flex package.")
endif()

if(NOT BISON_EXECUTABLE)
    message(FATAL_ERROR "Bison not found! Install bison package.")
endif()

# Flex and Bison functions
function(ADD_LEXER_TARGET input output)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${FLEX_EXECUTABLE} -o ${output} ${input}
        DEPENDS ${input}
        COMMENT "Generating lexer from ${input}"
    )
endfunction()

function(ADD_PARSER_TARGET input output_cpp output_hpp)
    add_custom_command(
        OUTPUT ${output_cpp} ${output_hpp}
        COMMAND ${BISON_EXECUTABLE} -d -o ${output_cpp} ${input}
        DEPENDS ${input}
        COMMENT "Generating parser from ${input}"
    )
endfunction()

# Generate lexer and parser
ADD_LEXER_TARGET(
    ${CMAKE_CURRENT_SOURCE_DIR}/lexer-with-ast.l
    ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.cpp
)

ADD_PARSER_TARGET(
    ${CMAKE_CURRENT_SOURCE_DIR}/parser-with-ast.y
    ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.hpp
)

# Source files
set(AST_SOURCES
    ast.cpp
)

set(GENERATED_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.cpp
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Create executable
add_executable(parser
    ${AST_SOURCES}
    ${GENERATED_SOURCES}
    wasm_compiler.cpp
)

# Link libraries (Flex library)
find_library(FL_LIBRARY fl)
if(FL_LIBRARY)
    target_link_libraries(parser ${FL_LIBRARY})
else()
    find_library(FL_LIBRARY NAMES fl flex)
    if(FL_LIBRARY)
        target_link_libraries(parser ${FL_LIBRARY})
    else()
        message(WARNING "Flex library not found, linking may fail")
    endif()
endif()

# Link Binaryen library
if(BINARYEN_LIBRARY)
    # Add library directory
    get_filename_component(BINARYEN_LIB_DIR ${BINARYEN_LIBRARY} DIRECTORY)
    link_directories(${BINARYEN_LIB_DIR})
    # Use full path to library and ensure we use the local one, not system one
    target_link_libraries(parser ${BINARYEN_LIBRARY})
    # Binaryen may require pthread
    find_package(Threads REQUIRED)
    target_link_libraries(parser Threads::Threads)
    # Set RPATH so it can find the library at runtime
    set_target_properties(parser PROPERTIES
        BUILD_RPATH "${BINARYEN_LIB_DIR}"
        INSTALL_RPATH "${BINARYEN_LIB_DIR}"
    )
endif()

# ========== NEW: SEMANTIC TEST SUITE ==========
enable_testing()

# Helper function to add semantic tests
function(add_semantic_test test_name input_file should_pass)
    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/parser < ${CMAKE_CURRENT_SOURCE_DIR}/${input_file}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    if(should_pass)
        set_tests_properties(${test_name} PROPERTIES
            PASS_REGULAR_EXPRESSION "=== STARTING SEMANTIC ANALYSIS ==="
        )
    else()
        set_tests_properties(${test_name} PROPERTIES
            PASS_REGULAR_EXPRESSION "ERROR:"
        )
    endif()
endfunction()

# Add semantic tests for declarations before usage
add_semantic_test(test_undeclared_var 
    "semantic_tests/test_undeclared_var.txt" FALSE)

add_semantic_test(test_undeclared_type 
    "semantic_tests/test_undeclared_type.txt" FALSE)

add_semantic_test(test_undeclared_routine 
    "semantic_tests/test_undeclared_routine.txt" FALSE)

add_semantic_test(test_forward_decl_ok 
    "semantic_tests/test_forward_decl_ok.txt" TRUE)

# Add constant folding tests
add_semantic_test(test_const_fold_arithmetic 
    "semantic_tests/test_const_fold_arithmetic.txt" TRUE)

add_semantic_test(test_const_fold_comparison 
    "semantic_tests/test_const_fold_comparison.txt" TRUE)

add_semantic_test(test_const_fold_bool 
    "semantic_tests/test_const_fold_bool.txt" TRUE)

# Custom target to run all semantic tests
add_custom_target(run_semantic_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "test_"
    DEPENDS parser
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running semantic analysis tests"
)

# Add test target (backward compatibility)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_input.txt)
    add_custom_target(test
        COMMAND ./parser < ${CMAKE_CURRENT_SOURCE_DIR}/test_input.txt || true
        DEPENDS parser
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running parser with test input"
    )
endif()

# Add visualization target
find_program(DOT_EXECUTABLE dot)
if(DOT_EXECUTABLE)
    add_custom_target(visualize
        COMMAND ./parser < ${CMAKE_CURRENT_SOURCE_DIR}/test_input.txt > ast_output.dot 2>/dev/null || true
        COMMAND ${DOT_EXECUTABLE} -Tpng ast_output.dot -o ast_tree.png
        DEPENDS parser
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating AST visualization"
    )
endif()

# Installation (optional)
install(TARGETS parser DESTINATION bin)

# Print configuration summary
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Flex executable: ${FLEX_EXECUTABLE}")
message(STATUS "Bison executable: ${BISON_EXECUTABLE}")
message(STATUS "Semantic tests enabled: YES")
