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

# Unity/src is absent in this tree; guard so the cf_tests target still
# configures (the pulse_proc/ay targets then cannot be built).
if(EXISTS ${PICO_FOLDER}/Unity/src/unity.c)
add_library(Unity STATIC ${PICO_FOLDER}/Unity/src/unity.c)
target_include_directories(Unity PUBLIC ${PICO_FOLDER}/Unity/src)
endif()

file(GLOB PULSE_PROC_SRC ${PICO_PROJECT_FOLDER}/src/PulseProc*.cpp)

set(HOST_TEST_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/stub
  ${PICO_PROJECT_FOLDER}/src
  ${PICO_FOLDER}/pico-emu-utils/src
)

if(EXISTS ${PICO_FOLDER}/Unity/src/unity.c)
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
endif()

# SuperSpec CF write-path desk simulation (test_cf.cpp): needs stub pico.h /
# ff.h / SdCardFatFsSpi.h headers (cfstub/), NOT the SDK ones, and no Unity
# (self-contained PASS/FAIL harness; Unity/src is not present in this tree).
add_executable(cf_tests
  ${PICO_PROJECT_FOLDER}/src/ZxSpectrumCf.cpp
  test_cf.cpp
)
target_include_directories(cf_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/cfstub
  ${PICO_PROJECT_FOLDER}/src
)
add_test(NAME cf_suite COMMAND cf_tests)
set_tests_properties(cf_suite PROPERTIES TIMEOUT 10)
