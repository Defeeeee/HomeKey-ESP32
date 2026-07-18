FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(SRCS ${app_sources}
                      INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                      REQUIRES arduino-esp32)

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DdscKeypad")
