cmake_minimum_required(VERSION 3.10.0)
project(vk_test VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)

find_package(Vulkan REQUIRED) 

add_executable(vk_test main.cpp)

target_link_libraries(vk_test glfw Vulkan::Vulkan)