cmake_minimum_required(VERSION 3.10)
get_filename_component(project_dir ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${project_dir})

add_subdirectory(custom_mpi_extensions)
add_subdirectory(stack_interface)
add_subdirectory(rma_stack)
