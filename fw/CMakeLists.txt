cmake_minimum_required(VERSION 3.16)

# Set the target before project declaration
set(IDF_TARGET "esp32c3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esprobe_dap)
