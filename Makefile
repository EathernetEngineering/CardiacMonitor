SOURCES := main.cpp audio.c serial.cpp graphics.c graph.c
HEADERS := audio.h serial.h util.h audioFormat.h common.h graphics.h graph.h

OBJECTS  := $(patsubst %.c,int/%.o,$(patsubst %.cpp,int/%.o,$(SOURCES)))

CC := /usr/bin/gcc
CXX := /usr/bin/g++
LD := /usr/bin/ld

LDFLAGS  := -L/opt/vc/lib -lasound -lrt -lpthread -lm -lbcm_host -lbrcmEGL -lbrcmGLESv2
CFLAGS := -g3 -O3 -x c -Wall -I/opt/vc/include
CXXFLAGS := -g3 -O3 -x c++ -std=c++17 -Wall -I/opt/vc/include

.PHONY: all
all: monitor

monitor: $(OBJECTS)
	${CXX} -o $@ $^ $(LDFLAGS)

.PHONY: run debug clean
run: monitor
	./monitor

debug: monitor
	gdb ./monitor

clean:
	-rm -rf monitor int/*

int/%.o: %.c $(HEADERS)
	[ -d "./int/" ] || mkdir int/
	${CC} -c ${CFLAGS} -o $@ $<

int/%.o: %.cpp $(HEADERS)
	[ -d "./int/" ] || mkdir int/
	${CXX} -c ${CXXFLAGS} -o $@ $<

