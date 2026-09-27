CXX ?= c++
AR ?= ar
PKG_CONFIG ?= pkg-config
BUILD ?= build
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags Qt6Core libarchive)
CXXFLAGS += -std=c++17 -fPIC -Wall -Wextra -O2
LDLIBS += $(shell $(PKG_CONFIG) --libs Qt6Core libarchive)
all: $(BUILD)/libremarkablefile.a
$(BUILD)/remarkablefile.o: remarkablefile.cpp remarkablefile.h
	mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD)/libremarkablefile.a: $(BUILD)/remarkablefile.o
	$(AR) rcs $@ $^
check: $(BUILD)/libremarkablefile.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) test.cpp $< $(LDLIBS) -o $(BUILD)/check
	$(BUILD)/check
.PHONY: all check
