SOURCES  := main.cpp play.cpp serial.cpp
HEADERS  := play.h serial.h util.h format.h

OBJECTS  := $(patsubst %.c,int/%.o,$(patsubst %.cpp,int/%.o,$(SOURCES)))

CC := /usr/bin/gcc
CXX := /usr/bin/g++
LD := /usr/bin/ld

LDFLAGS  := -lasound -lpthread
CXXFLAGS := -g3 -O0 -x c++ -std=c++17

.PHONY: all
all: monitor

monitor: $(OBJECTS)
	g++ -o $@ $^ $(LDFLAGS)

.PHONY: run debug clean
run: monitor
	./monitor

debug: monitor
	gdb ./monitor

clean:
	-rm -rf play.out

int/%.o: %.c
	[ -d "./int/" ] || mkdir int/
	${CC} -c ${CFLAGS} -o $@ $^

int/%.o: %.cpp
	[ -d "./int/" ] || mkdir int/
	${CXX} -c ${CXXFLAGS} -o $@ $^

