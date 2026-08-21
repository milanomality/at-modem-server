cmake_minimum_required(VERSION 3.13)
project(at-modem-server LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

option(ENABLE_SANITIZERS "Собрать с ASan/UBSan" OFF)

add_library(atmodem STATIC
    src/pattern.cpp
    src/dictionary.cpp
    src/log.cpp
    src/serial_port.cpp
    src/modem_server.cpp
)
target_include_directories(atmodem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(atmodem PUBLIC -Wall -Wextra -Wpedantic -Wshadow)
    if(ENABLE_SANITIZERS)
        target_compile_options(atmodem PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(atmodem PUBLIC -fsanitize=address,undefined)
    endif()
endif()

add_executable(at-modem-server src/main.cpp)
target_link_libraries(at-modem-server PRIVATE atmodem)

# ------------------------------------------------------------------- тесты
enable_testing()

add_executable(unit_tests
    tests/mini_test.cpp
    tests/test_pattern.cpp
    tests/test_dictionary.cpp
    tests/test_server.cpp
)
target_link_libraries(unit_tests PRIVATE atmodem)

add_test(NAME unit COMMAND unit_tests)

add_test(NAME integration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_test.sh
            $<TARGET_FILE:at-modem-server>
            ${CMAKE_CURRENT_SOURCE_DIR}/config/modem.dict
)

install(TARGETS at-modem-server RUNTIME DESTINATION bin)
install(FILES config/modem.dict DESTINATION share/at-modem-server)
