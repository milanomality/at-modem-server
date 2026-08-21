# Запасная сборка для тех, у кого нет CMake. Основной путь — CMakeLists.txt.
#
#   make            собрать сервер
#   make test       собрать и прогнать юнит- и интеграционные тесты
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
BUILD    := build-make

CORE  := src/pattern.cpp src/dictionary.cpp src/log.cpp \
         src/serial_port.cpp src/modem_server.cpp
TESTS := tests/mini_test.cpp tests/test_pattern.cpp \
         tests/test_dictionary.cpp tests/test_server.cpp

all: $(BUILD)/at-modem-server

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/at-modem-server: $(CORE) src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CORE) src/main.cpp -o $@

$(BUILD)/unit_tests: $(CORE) $(TESTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CORE) $(TESTS) -o $@

test: $(BUILD)/unit_tests $(BUILD)/at-modem-server
	$(BUILD)/unit_tests
	./tests/integration_test.sh $(BUILD)/at-modem-server config/modem.dict

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
