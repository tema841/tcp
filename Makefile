CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS ?=
ifeq ($(OS),Windows_NT)
  LDLIBS += -lws2_32
endif
CORE = socket.cpp transfer.cpp sha256.cpp metrics.cpp
all: server client tests
server: server.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)
client: client.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)
tests: tests.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)
clean:
	rm -f server client tests
