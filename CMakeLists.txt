cmake_minimum_required(VERSION 3.24)

project(mycord
    DESCRIPTION "A simple chat application"
    LANGUAGES C
)

option(TUI "Build with TUI support" OFF)

add_executable(mycord client.c)

if(TUI)
    target_sources(mycord PRIVATE tui.c)
    target_compile_definitions(mycord PRIVATE TUI)
endif()

target_link_libraries(mycord PRIVATE pthread)
target_include_directories(mycord PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
