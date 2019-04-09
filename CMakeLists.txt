cmake_minimum_required(VERSION 3.12)
project(vk_layer_dali)

set(CMAKE_CXX_STANDARD 14)
set( VULKAN_SDK $ENV{HOME}/opt/vulkan/current/x86_64 )
include_directories( ${VULKAN_SDK}/include )

add_library(VkLayer_dali SHARED library.cpp resources.cpp library.h)
install( TARGETS VkLayer_dali DESTINATION /home/ukpc1/development/dalihub/dali-env/opt/lib )
install( TARGETS VkLayer_dali DESTINATION /tmp )