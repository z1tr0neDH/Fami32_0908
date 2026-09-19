# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "FAMI32 N16R8 target")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS -Wno-error APPEND)
project(Fami32)

execute_process(COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/tools/check_hardware.py"
                RESULT_VARIABLE FAMI32_HARDWARE_CHECK)
if(NOT FAMI32_HARDWARE_CHECK EQUAL 0)
    message(FATAL_ERROR "FAMI32 hardware source check failed")
endif()

if(NOT CONFIG_IDF_TARGET_ESP32S3 OR NOT CONFIG_ESPTOOLPY_FLASHSIZE_16MB
   OR NOT CONFIG_SPIRAM_MODE_OCT OR NOT CONFIG_SPIRAM_SPEED_80M
   OR NOT CONFIG_ESPTOOLPY_FLASHFREQ_80M OR NOT CONFIG_ESPTOOLPY_FLASHMODE_DIO
   OR NOT CONFIG_PARTITION_TABLE_CUSTOM OR NOT CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH)
    message(FATAL_ERROR "FAMI32 requires the N16R8 board configuration in sdkconfig.defaults")
endif()

set(PROJECT_COMPONENTS_DIR "${CMAKE_SOURCE_DIR}/components")
set(PROJECT_MAIN_DIR "${CMAKE_SOURCE_DIR}/main")

idf_build_get_property(component_targets __BUILD_COMPONENT_TARGETS)
foreach(component_target ${component_targets})
    get_property(component_dir TARGET ${component_target} PROPERTY COMPONENT_DIR)
    get_property(component_lib TARGET ${component_target} PROPERTY COMPONENT_LIB)

    if(NOT TARGET ${component_lib})
        continue()
    endif()

    string(FIND "${component_dir}" "${PROJECT_COMPONENTS_DIR}/" components_pos)
    if(component_dir STREQUAL PROJECT_MAIN_DIR OR components_pos EQUAL 0)
        get_target_property(component_type ${component_lib} TYPE)
        if(component_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${component_lib} INTERFACE -Ofast)
        else()
            target_compile_options(${component_lib} PRIVATE -Ofast)
        endif()
    endif()
endforeach()
