CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -O0 -g
TARGET = processor_simulator

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp simulator.cpp simulator.h ../tac/tac.cpp ../tac/tac.h ../architecture/architecture.cpp ../architecture/architecture.h
	$(CXX) $(CXXFLAGS) -I../tac -I../architecture main.cpp simulator.cpp ../tac/tac.cpp ../architecture/architecture.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
