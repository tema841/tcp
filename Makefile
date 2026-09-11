# Windows / MinGW Makefile
# Требуется mingw32-make и g++ из MinGW-w64.
#
# Сборка:
#   mingw32-make
#
# Запуск тестов:
#   mingw32-make tests-run
#
# Очистка:
#   mingw32-make clean

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
LDFLAGS := -lws2_32

BUILD := build

COMMON_OBJ := \
	$(BUILD)/socket.o \
	$(BUILD)/transfer.o \
	$(BUILD)/signals.o \
	$(BUILD)/fs_lib.o

.PHONY: all clean tests tests-run dirs

all: dirs client.exe server.exe tests.exe

dirs:
	if not exist "$(BUILD)" mkdir "$(BUILD)"

$(BUILD)/socket.o: socket.cpp socket.hpp
	$(CXX) $(CXXFLAGS) -c socket.cpp -o $@

$(BUILD)/transfer.o: transfer.cpp transfer.hpp socket.hpp signals.hpp fs_lib.hpp
	$(CXX) $(CXXFLAGS) -c transfer.cpp -o $@

$(BUILD)/signals.o: signals.cpp signals.hpp
	$(CXX) $(CXXFLAGS) -c signals.cpp -o $@

$(BUILD)/fs_lib.o: fs_lib.cpp fs_lib.hpp
	$(CXX) $(CXXFLAGS) -c fs_lib.cpp -o $@

$(BUILD)/server.o: server.cpp socket.hpp transfer.hpp signals.hpp fs_lib.hpp
	$(CXX) $(CXXFLAGS) -c server.cpp -o $@

server.exe: $(BUILD)/server.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/client.o: client.cpp socket.hpp transfer.hpp signals.hpp fs_lib.hpp progress.hpp
	$(CXX) $(CXXFLAGS) -c client.cpp -o $@

client.exe: $(BUILD)/client.o $(COMMON_OBJ) progress_lib.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD)/tests.o: tests.cpp socket.hpp signals.hpp
	$(CXX) $(CXXFLAGS) -c tests.cpp -o $@

tests.exe: $(BUILD)/tests.o $(BUILD)/socket.o $(BUILD)/signals.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

tests:
	$(MAKE) tests.exe

tests-run: all
	.\tests.exe

clean:
	if exist "$(BUILD)" rmdir /s /q "$(BUILD)"
	if exist "client.exe" del /q "client.exe"
	if exist "server.exe" del /q "server.exe"
	if exist "tests.exe" del /q "tests.exe"
