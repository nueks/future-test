CXXFLAGS=-g -std=c++14 -fno-access-control
LDFLAGS=-lgtest -pthread
CC=g++
TARGET=future_test circular_buffer_test

all: $(TARGET)

future_test: future_test.o main.o
future_test.o: future_test.cpp future.hpp

circular_buffer_test: circular_buffer_test.o main.o
circular_buffer_test.o: circular_buffer_test.cpp circular_buffer.hpp

main.o: main.cpp

clean:
	rm *.o $(TARGET)

.PHONY: clean
