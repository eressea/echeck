cmake_minimum_required(VERSION 2.8...3.20)
project (echeck C)

add_executable(echeck main.c echeck.c unicode.c whereami.c)
add_executable(tests tests.c echeck.c unicode.c whereami.c CuTest.c)
target_compile_definitions (tests PRIVATE TESTING=1)
set_property(TARGET echeck PROPERTY C_STANDARD 99)
set_property(TARGET tests PROPERTY C_STANDARD 99)

if (WIN32)
find_library(Intl_LIBRARY NAMES libintl intl
  DOC "libintl libraries (if not in the C library)")
endif (WIN32)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
target_compile_definitions (echeck PRIVATE -D_DEFAULT_SOURCE)
target_compile_definitions (tests PRIVATE -D_DEFAULT_SOURCE)
endif()
find_package(Intl)
include_directories(${Intl_INCLUDE_DIR})

if (Intl_FOUND)
target_compile_definitions (echeck PRIVATE HAVE_GETTEXT=1)
target_compile_definitions (tests PRIVATE HAVE_GETTEXT=1)
target_link_libraries(echeck ${Intl_LIBRARIES})
target_link_libraries(tests ${Intl_LIBRARIES})
else (Intl_FOUND)
message(STATUS "libintl not found")
endif (Intl_FOUND)

include("Localization.cmake")
SET (PACKAGE_NAME echeck)
PROGRAM_LOCALIZATION()

if (WIN32)
add_executable(echeckw WIN32 echeckw.c echeck.c unicode.c whereami.c)
if (Intl_FOUND)
target_compile_definitions (echeckw PRIVATE HAVE_GETTEXT=1)
target_link_libraries(echeckw ${Intl_LIBRARIES})
endif (Intl_FOUND)

endif(WIN32)

set(INSTALL_BIN_DIR "bin" CACHE STRING "Install executable in this directory")
set(INSTALL_DATA_DIR "share/echeck" CACHE STRING "Install data to this directory")
set(INSTALL_LOCALE_DIR "share/locale" CACHE STRING "Install the .mo files to this directory")


install(TARGETS ${PROJECT_NAME} DESTINATION ${INSTALL_BIN_DIR})
install(DIRECTORY e2 e3 DESTINATION ${INSTALL_DATA_DIR})
install(DIRECTORY locale/de  DESTINATION ${INSTALL_LOCALE_DIR})

