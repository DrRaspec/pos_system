cmake_minimum_required(VERSION 3.22)
project(pos_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(httplib nlohmann_json)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

add_executable(pos_backend
  src/main.cpp
  src/config.cpp
  src/database.cpp
  src/security.cpp
  src/http_utils.cpp
  src/rate_limiter.cpp
)

target_include_directories(pos_backend PRIVATE include)
target_link_libraries(pos_backend
  PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
    PkgConfig::PQXX
)

target_compile_options(pos_backend PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  $<$<CONFIG:Release>:-O2 -DNDEBUG>
)