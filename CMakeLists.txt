cmake_minimum_required(VERSION 3.20)

project(
  cpp_market_replay_engine
  VERSION 0.1.0
  DESCRIPTION "Deterministic C++20 market replay engine"
  LANGUAGES CXX
)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer when supported" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(market_replay
  src/version.cpp
)

add_library(market_replay::market_replay ALIAS market_replay)

target_include_directories(market_replay
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(market_replay PUBLIC cxx_std_20)

function(replay_apply_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
    )
  endif()
endfunction()

function(replay_apply_sanitizers target_name)
  if(ENABLE_SANITIZERS AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

replay_apply_warnings(market_replay)
replay_apply_sanitizers(market_replay)

add_executable(replay_cli
  apps/replay_cli.cpp
)

target_link_libraries(replay_cli PRIVATE market_replay::market_replay)
replay_apply_warnings(replay_cli)
replay_apply_sanitizers(replay_cli)

if(BUILD_TESTING)
  add_executable(smoke_tests
    tests/unit/smoke_test.cpp
  )

  target_link_libraries(smoke_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(smoke_tests)
  replay_apply_sanitizers(smoke_tests)

  add_test(NAME smoke_tests COMMAND smoke_tests)
endif()
