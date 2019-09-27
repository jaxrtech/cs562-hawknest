cmake_minimum_required(VERSION 3.13)
project(hawknest)

add_executable(hawknest "")
set_property(TARGET hawknest PROPERTY C_STANDARD 11)
add_subdirectory(./emu)
target_link_libraries(hawknest SDL2 readline)