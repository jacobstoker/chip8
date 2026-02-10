cmake_minimum_required(VERSION 3.10)
project(chip8 C)

find_package(raylib REQUIRED)

add_executable(chip8 src/chip8.c)
target_link_libraries(chip8 PRIVATE
    raylib
)

target_compile_options(chip8 PRIVATE -Wall -Wextra -Wconversion -Wdouble-promotion
                                       -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion
                                       # -fsanitize=address,undefined 
                                       -g)

# target_link_options(chip8 PRIVATE -fsanitize=address,undefined)

# if(NOT TARGET Wayland::Client)
#     target_link_libraries(chip8 m pthread dl)
# endif()
