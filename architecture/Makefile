CXX = g++
CXXFLAGS=-std=c++23 -Wall -Wextra -O0 -g
TARGET = architecture_info

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp architecture.cpp architecture.h
	$(CXX) $(CXXFLAGS) main.cpp architecture.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
