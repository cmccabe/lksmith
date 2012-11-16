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
# We use pthread_key_create with a non-NULL destructor parameter, so we
# cannot allow our shared library to be unloaded.  If we did allow this,
# we might get users into a situation where the destructor functions were
# invoked, but they had been unloaded.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -z nodelete")

set(CMAKE_C_FLAGS_RELEASE "-O3 -Wuninitialized")
set(CMAKE_C_FLAGS_DEBUG "-g -O2 -fstack-protector -rdynamic")
set(CMAKE_CURRENT_BINARY_DIR, builds)
SET(CMAKE_INSTALL_PREFIX /usr/local)

# If our compiler and linker both support the __thread attribute, we can use
# it as a more efficient alternative to POSIX thread-local storage in some
# scenarios.  This should always work on Linux.
INCLUDE(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("int main(void) { static __thread int i = 0; return 0; }" HAVE_IMPROVED_TLS)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

# Set up include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

macro(add_utest utest)
    add_test(${utest} ${CMAKE_CURRENT_BINARY_DIR}/${utest} ${utest})
endmacro(add_utest)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_library(lksmith
        lksmith.c
        linux_platform.c
    )
else()
    add_library(lksmith
        lksmith.c
        posix_platform.c
    )
endif()

target_link_libraries(lksmith pthread)

add_executable(thread_unit test.c thread_unit.c)
target_link_libraries(thread_unit lksmith)
add_utest(thread_unit)

add_executable(simple_unit test.c simple_unit.c)
target_link_libraries(simple_unit lksmith)
add_utest(simple_unit)
