CXXFLAGS=-g -std=c++14
LDFLAGS=-lgtest -pthread
CC=g++
TARGET=future_test

all: $(TARGET)

future_test: future_test.o main.o
future_test.o: future_test.cpp future.hpp

main.o: main.cpp

clean:
	rm *.o $(TARGET)

.PHONY: clean
