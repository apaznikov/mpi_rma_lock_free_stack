cmake_minimum_required(VERSION 3.10)
get_filename_component(project_dir ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${project_dir})

find_package(MPI REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

add_library(${PROJECT_NAME} ${SOURCES})
add_library(sub::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC sub::stack_interface sub::custom_mpi_extensions
        PRIVATE ${MPI_C_LIBRARIES}
        PUBLIC spdlog
)
target_include_directories(
        ${PROJECT_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC include
        PUBLIC spdlog
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib/)