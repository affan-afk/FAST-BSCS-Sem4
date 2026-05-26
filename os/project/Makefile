# Makefile — Restaurant Order Management Simulator
#
# Usage:
#   make          → build the simulator
#   make clean    → remove build artifacts
#   make run      → build and run
#   make debug    → build with AddressSanitizer + ThreadSanitizer

CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -O2 -pthread
TARGET   := restaurant_sim
SRCS     := main.cpp order.cpp queue.cpp threads.cpp
OBJS     := $(SRCS:.cpp=.o)

# Note: on older Linux (glibc < 2.17) add -lrt for POSIX semaphores
LDFLAGS  := -pthread

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete → ./$(TARGET)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

# ThreadSanitizer build: detects data races at runtime
debug:
	$(CXX) -std=c++14 -Wall -Wextra -g -O1 -pthread \
	    -fsanitize=thread \
	    $(SRCS) -o $(TARGET)_tsan
	@echo "TSan build complete → ./$(TARGET)_tsan"

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_tsan
