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
  src/event_loop.cpp
  src/execution_simulator.cpp
  src/fill.cpp
  src/latency_execution.cpp
  src/market_feed.cpp
  src/order.cpp
  src/order_book.cpp
  src/simulation_clock.cpp
  src/strategy.cpp
  strategies/queue_imbalance_strategy.cpp
  src/types.cpp
  src/version.cpp
)

add_library(market_replay::market_replay ALIAS market_replay)

target_include_directories(market_replay
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
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

  add_executable(domain_types_tests
    tests/unit/domain_types_test.cpp
  )

  target_link_libraries(domain_types_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(domain_types_tests)
  replay_apply_sanitizers(domain_types_tests)

  add_test(NAME domain_types_tests COMMAND domain_types_tests)

  add_executable(market_feed_tests
    tests/unit/market_feed_test.cpp
  )

  target_link_libraries(market_feed_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(market_feed_tests)
  replay_apply_sanitizers(market_feed_tests)

  add_test(NAME market_feed_tests COMMAND market_feed_tests)
  set_tests_properties(market_feed_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(order_book_tests
    tests/unit/order_book_test.cpp
  )

  target_link_libraries(order_book_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(order_book_tests)
  replay_apply_sanitizers(order_book_tests)

  add_test(NAME order_book_tests COMMAND order_book_tests)
  set_tests_properties(order_book_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(event_loop_tests
    tests/unit/event_loop_test.cpp
  )

  target_link_libraries(event_loop_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(event_loop_tests)
  replay_apply_sanitizers(event_loop_tests)

  add_test(NAME event_loop_tests COMMAND event_loop_tests)
  set_tests_properties(event_loop_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(strategy_tests
    tests/unit/strategy_test.cpp
  )

  target_link_libraries(strategy_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(strategy_tests)
  replay_apply_sanitizers(strategy_tests)

  add_test(NAME strategy_tests COMMAND strategy_tests)
  set_tests_properties(strategy_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(execution_tests
    tests/unit/execution_simulator_test.cpp
  )

  target_link_libraries(execution_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(execution_tests)
  replay_apply_sanitizers(execution_tests)

  add_test(NAME execution_tests COMMAND execution_tests)
  set_tests_properties(execution_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(latency_execution_tests
    tests/unit/latency_execution_test.cpp
  )

  target_link_libraries(latency_execution_tests PRIVATE market_replay::market_replay)
  replay_apply_warnings(latency_execution_tests)
  replay_apply_sanitizers(latency_execution_tests)

  add_test(NAME latency_execution_tests COMMAND latency_execution_tests)
  set_tests_properties(latency_execution_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
