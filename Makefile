CXX ?= c++
AR ?= ar
PKG_CONFIG ?= pkg-config
BUILD ?= build
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags Qt6Core libarchive)
CXXFLAGS += -std=c++17 -fPIC -Wall -Wextra -O2
LDLIBS += $(shell $(PKG_CONFIG) --libs Qt6Core libarchive)
all: $(BUILD)/libremarkable.a
$(BUILD)/remarkable.o: remarkable.cpp remarkable.h
	mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD)/libremarkable.a: $(BUILD)/remarkable.o
	$(AR) rcs $@ $^
check: $(BUILD)/libremarkable.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) test.cpp $< $(LDLIBS) -o $(BUILD)/check
	$(BUILD)/check
.PHONY: all check
