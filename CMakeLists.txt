# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.1.0")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)
add_compile_options(-Wno-format)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

# Fix missing string.h include in espressif2022__esp_emote_gfx component
# This adds -include string.h to the compiler flags for the specific file
idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(component ${build_components})
    if(component MATCHES "espressif2022__esp_emote_gfx")
        idf_component_get_property(component_lib ${component} COMPONENT_LIB)
        if(TARGET ${component_lib})
            # Add -include string.h to fix missing memset declaration in gfx_render.c
            target_compile_options(${component_lib} PRIVATE 
                "$<$<COMPILE_LANGUAGE:C>:-include>"
                "$<$<COMPILE_LANGUAGE:C>:string.h>"
            )
            break()
        endif()
    endif()
endforeach()

