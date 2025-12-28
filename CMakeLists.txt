cmake_minimum_required(VERSION 3.16)
project(Aletheia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

option(ALETHEIA_USE_HWY "Enable Highway SIMD" ON)
option(ALETHEIA_USE_WORD_POOL "Enable fixed-block word pool" ON)

find_package(Eigen3 3.4 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
  )
  FetchContent_MakeAvailable(eigen)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

if(ALETHEIA_USE_HWY)
  find_package(hwy QUIET)
  if(NOT hwy_FOUND)
    include(FetchContent)
    set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      hwy
      GIT_REPOSITORY https://github.com/google/highway.git
      GIT_TAG 1.1.0
    )
    FetchContent_MakeAvailable(hwy)
  endif()
endif()

find_package(OpenMP QUIET)

add_executable(aletheia main.cpp Wordle.cpp Connections.cpp)
target_include_directories(aletheia PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aletheia PRIVATE Eigen3::Eigen)
if(TARGET hwy)
  target_link_libraries(aletheia PRIVATE hwy)
  target_compile_definitions(aletheia PRIVATE ALETHEIA_USE_HWY=1)
endif()
if(ALETHEIA_USE_WORD_POOL)
  target_compile_definitions(aletheia PRIVATE ALETHEIA_USE_WORD_POOL=1)
endif()

if(OpenMP_CXX_FOUND)
  target_link_libraries(aletheia PRIVATE OpenMP::OpenMP_CXX)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(aletheia PRIVATE -O3)
elseif(MSVC)
  target_compile_options(aletheia PRIVATE /O2)
endif()

add_executable(wordle_tests tests/wordle_tests.cpp Wordle.cpp)
target_include_directories(wordle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wordle_tests PRIVATE Eigen3::Eigen GTest::gtest_main)
if(TARGET hwy)
  target_link_libraries(wordle_tests PRIVATE hwy)
  target_compile_definitions(wordle_tests PRIVATE ALETHEIA_USE_HWY=1)
endif()
if(ALETHEIA_USE_WORD_POOL)
  target_compile_definitions(wordle_tests PRIVATE ALETHEIA_USE_WORD_POOL=1)
endif()

gtest_discover_tests(wordle_tests)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(wordle_tests PRIVATE -O3)
elseif(MSVC)
  target_compile_options(wordle_tests PRIVATE /O2)
endif()
