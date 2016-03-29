CXXFLAGS=-g -std=c++14 -pthread -lgtest
LDFLAGS=-lgtest -lm
LDLIBS=-lm -lgtest

all: main

main: main.cpp future_test.o

future_test.o: future_test.cpp

clean:
	rm *.o main
.PHONY: clean
