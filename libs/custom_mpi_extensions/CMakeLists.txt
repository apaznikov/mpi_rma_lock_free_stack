cmake_minimum_required(VERSION 3.10)
get_filename_component(project_dir ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${project_dir})

file(GLOB SOURCES src/*.cpp src/*.c)

find_package(MPI REQUIRED)

add_library(${PROJECT_NAME} ${SOURCES})
add_library(sub::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE ${MPI_C_LIBRARIES}
)
target_include_directories(
        ${PROJECT_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(
        ${PROJECT_NAME}
        PUBLIC include
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib/)