CXX ?= c++
PKG_CONFIG ?= pkg-config
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags swiftzfs-cpp)
LDLIBS += $(shell $(PKG_CONFIG) --libs swiftzfs-cpp)

EXAMPLES = list-pools list-datasets list-snapshots
BINARIES = $(addprefix .build/,$(EXAMPLES))

.PHONY: all
all: $(BINARIES)

.build:
	mkdir -p $@

.build/%: %.cpp | .build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDLIBS)
