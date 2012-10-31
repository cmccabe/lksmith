cmake_minimum_required(VERSION 2.6.4 FATAL_ERROR)

# The name of our project is "Locksmith". CMakeLists files in this project can
# refer to the root source directory of the project as ${LOCKSMITH_SOURCE_DIR}
# and to the root binary directory of the project as ${LOCKSMITH_BINARY_DIR}.
project(locksmith)

enable_testing()

# Define "make check" as an alias for "make test."
add_custom_target(check COMMAND ctest)

SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The Locksmith lock-checking library")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
SET(CPACK_PACKAGE_VERSION_MAJOR "1")
SET(CPACK_PACKAGE_VERSION_MINOR "0")
SET(CPACK_PACKAGE_CONTACT "cmccabe@alumni.cmu.edu")
SET(CPACK_GENERATOR "TGZ")
INCLUDE(CPack)

set(CMAKE_BUILD_TYPE, Release) # can also be Debug
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_REENTRANT -D_FILE_OFFSET_BITS=64")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
# We use pthread_key_create with a non-NULL destructor parameter, so don't
# allow unloading our shared libraries.  If we did allow this, we might get
# users into a situation where the destructor functions were invoked, but they
# had been unloaded.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -z nodelete")

set(CMAKE_C_FLAGS_RELEASE "-O3 -Wuninitialized")
set(CMAKE_C_FLAGS_DEBUG "-g -O2 -fstack-protector -Werror -rdynamic")
set(CMAKE_CURRENT_BINARY_DIR, builds)
SET(CMAKE_INSTALL_PREFIX /usr/local)

# Set up include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

macro(add_utest utest)
    add_test(${utest} ${CMAKE_CURRENT_BINARY_DIR}/${utest} ${utest})
endmacro(add_utest)

add_subdirectory(util)
