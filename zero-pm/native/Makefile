CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fPIC
AR       ?= ar

LIB  = libzpm_native.a
SRCS = zpm_native.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp zpm_native.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
