cmake_minimum_required(VERSION 3.20)
project(hed C)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build options
option(USE_TREESITTER "Enable Tree-sitter syntax highlighting" ON)
option(USE_LIBVTERM "Enable libvterm terminal emulation" ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Include directories
include_directories(
    src
    src/ui
    src/buf
    src/lib
    src/utils
    src/commands
)

# Collect all source files
file(GLOB_RECURSE SOURCES "src/*.c")

# Create executable
add_executable(hed ${SOURCES})

# Optional Tree-sitter support
if(USE_TREESITTER)
    target_compile_definitions(hed PRIVATE USE_TREESITTER)
    target_link_libraries(hed PRIVATE tree-sitter dl)
endif()

# Optional libvterm support
if(USE_LIBVTERM)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM REQUIRED vterm)

    target_compile_definitions(hed PRIVATE USE_LIBVTERM)
    target_include_directories(hed PRIVATE ${VTERM_INCLUDE_DIRS})
    target_link_libraries(hed PRIVATE ${VTERM_LIBRARIES})
endif()

# Custom targets for convenience
add_custom_target(run
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/hed
    DEPENDS hed
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running hed"
)
