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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fno-strict-aliasing -rdynamic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_REENTRANT -D_FILE_OFFSET_BITS=64")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
# We use pthread_key_create with a non-NULL destructor parameter, so we
# cannot allow our shared library to be unloaded.  If we did allow this,
# we might get users into a situation where the destructor functions were
# invoked, but they had been unloaded.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -z nodelete")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O3 -Wuninitialized")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g -O0 -fstack-protector")
set(CMAKE_CURRENT_BINARY_DIR, builds)
if (NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX /usr/local)
endif()

# If our compiler and linker both support the __thread attribute, we can use
# it as a more efficient alternative to POSIX thread-local storage in some
# scenarios.  This should always work on Linux.
INCLUDE(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("int main(void) { static __thread int i = 0; return 0; }" HAVE_IMPROVED_TLS)

SET(USE_LIBUNWIND false)
if(USE_LIBUNWIND)
    find_package(Libunwind REQUIRED)
endif(USE_LIBUNWIND)

CHECK_C_SOURCE_COMPILES("#include <execinfo.h>
int main(void) { return backtrace(0, 0); }" HAVE_LIBC_BACKTRACE)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

# Set up include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    ${LIBUNWIND_INCLUDE_DIR}
)

macro(add_utest utest)
    add_test(${utest} ${CMAKE_CURRENT_BINARY_DIR}/${utest} ${utest})
endmacro(add_utest)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(PLATFORM_FILES linux_platform.c)
else()
    set(PLATFORM_FILES posix_platform.c)
endif()

if (USE_LIBUNWIND)
    set(PLATFORM_FILES ${PLATFORM_FILES} libunwind_backtrace.c)
elseif (HAVE_LIBC_BACKTRACE)
    set(PLATFORM_FILES ${PLATFORM_FILES} libc_backtrace.c)
else()
    MESSAGE(FATAL_ERROR "can't find any backtrace scheme to use.  Perhaps install libunwind.")
endif()

add_library(lksmith SHARED
    ${PLATFORM_FILES}
    error.c
    lksmith.c
    handler.c
    util.c
)

target_link_libraries(lksmith pthread)
INSTALL(TARGETS lksmith LIBRARY DESTINATION lib)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(lksmith dl)
endif()
if(USE_LIBUNWIND)
    target_link_libraries(lksmith ${LIBUNWIND_LIBRARIES})
endif(USE_LIBUNWIND)

add_executable(thread_unit test.c thread_unit.c test.c mem.c)
target_link_libraries(thread_unit lksmith)
add_utest(thread_unit)

add_executable(simple_unit test.c simple_unit.c test.c mem.c)
target_link_libraries(simple_unit lksmith)
add_utest(simple_unit)

add_executable(error_unit test.c error_unit.c test.c mem.c)
target_link_libraries(error_unit lksmith)
add_utest(error_unit)

add_executable(ignore_unit test.c ignore_unit.c test.c mem.c)
target_link_libraries(ignore_unit lksmith)
add_utest(ignore_unit)
