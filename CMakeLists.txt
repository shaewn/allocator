cmake_minimum_required(VERSION 3.5.0)

set(VERSION "0.0.1")
set(PROJECT_NAME "alloc")

project(${PROJECT_NAME} VERSION ${VERSION} LANGUAGES C)

set(SOURCES
    "src/main.c"
    "src/alloc.c"
)

add_executable(${PROJECT_NAME} ${SOURCES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

include(CTest)
enable_testing()
