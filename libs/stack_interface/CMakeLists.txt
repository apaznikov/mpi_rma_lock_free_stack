cmake_minimum_required(VERSION 3.10)
get_filename_component(project_dir ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${project_dir})

set(CMAKE_CXX_STANDARD 17)

add_library(
        ${PROJECT_NAME}
        INTERFACE
        include/IStack.h
)
add_library(sub::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
        ${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${PROJECT_NAME}>
        .
)