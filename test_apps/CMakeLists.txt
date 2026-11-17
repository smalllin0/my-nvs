cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")
set(COMPONENTS main my-nvs unity nvs_flash)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_nvs_test)