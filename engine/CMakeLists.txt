cmake_minimum_required(VERSION 3.24)
project(wizd CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include(FetchContent)
FetchContent_Declare(glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG v8.4.0
)
FetchContent_MakeAvailable(glaze)

find_package(OpenSSL REQUIRED) # SHA1 for the WebSocket handshake only

add_library(wizcore src/wiz.cpp src/server.cpp)
target_include_directories(wizcore PUBLIC src)
target_link_libraries(wizcore PUBLIC glaze::glaze OpenSSL::Crypto)
target_compile_features(wizcore PUBLIC cxx_std_23)

find_package(Threads REQUIRED)

add_executable(wizd src/main.cpp)
target_link_libraries(wizd PRIVATE wizcore Threads::Threads)

enable_testing()
add_executable(wizd_tests test/test_wiz.cpp)
target_link_libraries(wizd_tests PRIVATE wizcore)
# assert() must never be compiled out here, even in a Release build.
target_compile_options(wizd_tests PRIVATE -UNDEBUG)
add_test(NAME wizd_tests COMMAND wizd_tests)
