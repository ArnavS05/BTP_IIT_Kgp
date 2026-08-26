CXX = g++
CXXFLAGS=-std=c++23 -Wall -Wextra -O0 -g
TARGET = tac_reader

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp tac.cpp tac.h
	$(CXX) $(CXXFLAGS) main.cpp tac.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
