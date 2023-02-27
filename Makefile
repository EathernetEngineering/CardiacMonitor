SOURCES  := main.cpp play.cpp serial.cpp graphics.c
HEADERS  := play.h serial.h util.h format.h graphics.h

OBJECTS  := $(patsubst %.c,int/%.o,$(patsubst %.cpp,int/%.o,$(SOURCES)))

CC := /usr/bin/gcc
CXX := /usr/bin/g++
LD := /usr/bin/ld

LDFLAGS  := -L/opt/vc/lib -lasound -lrt -lpthread -lm -lbcm_host -lbrcmEGL -lbrcmGLESv2
CFLAGS := -g3 -O0 -x c -I/opt/vc/include
CXXFLAGS := -g3 -O0 -x c++ -std=c++17 -I/opt/vc/include

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
	-rm -rf play.out int/*

int/%.o: %.c $(HEADERS)
	[ -d "./int/" ] || mkdir int/
	${CC} -c ${CFLAGS} -o $@ $<

int/%.o: %.cpp $(HEADERS)
	[ -d "./int/" ] || mkdir int/
	${CXX} -c ${CXXFLAGS} -o $@ $<

