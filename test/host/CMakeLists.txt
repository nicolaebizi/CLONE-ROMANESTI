# Host (PC) unit tests for the portable parts of the emulator.
#
#   cmake -S test/host -B build/host
#   make -C build/host
#   (cd build/host && ctest --output-on-failure --timeout 10)
#
cmake_minimum_required(VERSION 3.12)

project(pico-zxspectrum-host-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall -g -fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)

get_filename_component(PICO_FOLDER ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
get_filename_component(PICO_PROJECT_FOLDER ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)

include(CTest)

add_library(Unity STATIC ${PICO_FOLDER}/Unity/src/unity.c)
target_include_directories(Unity PUBLIC ${PICO_FOLDER}/Unity/src)

file(GLOB PULSE_PROC_SRC ${PICO_PROJECT_FOLDER}/src/PulseProc*.cpp)

set(HOST_TEST_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/stub
  ${PICO_PROJECT_FOLDER}/src
  ${PICO_FOLDER}/pico-emu-utils/src
)

foreach(target
  pulse_proc
  ay
)
  add_executable(${target}_tests
    ${PULSE_PROC_SRC}
    ${PICO_FOLDER}/pico-emu-utils/src/InputStream.cpp
    test_${target}.cpp
  )
  target_include_directories(${target}_tests PRIVATE ${HOST_TEST_INCLUDES})
  target_link_libraries(${target}_tests Unity)
  add_test(NAME ${target}_suite COMMAND ${target}_tests)
  set_tests_properties(${target}_suite PROPERTIES TIMEOUT 10)
endforeach()
