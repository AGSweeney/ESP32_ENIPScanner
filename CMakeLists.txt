# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Unused components have been moved to components/unused/
# They are not included in the build to avoid unnecessary dependencies
# See components/unused/README.md for details

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# Define FD_SETSIZE for LWIP_MAX_SOCKETS=64
# LWIP_SOCKET_OFFSET = FD_SETSIZE - MAX_SOCKETS, and we need LWIP_SOCKET_OFFSET >= 6
# So FD_SETSIZE >= MAX_SOCKETS + 6 = 64 + 6 = 70
# Also need room for stdout, stderr, stdin (+3), so use 73 for safety
add_compile_definitions(FD_SETSIZE=73)

project(ESP32-ENIPScanner)

# Add .cache folders to the clean process
# These folders are not automatically cleaned by idf.py clean, but can cause build issues
set_directory_properties(PROPERTIES
    ADDITIONAL_CLEAN_FILES
    "${CMAKE_SOURCE_DIR}/components/.cache;${CMAKE_SOURCE_DIR}/.cache"
)