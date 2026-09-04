ifeq ($(origin CXX),default)
CXX = clang++
endif
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2 -Iinclude
LDFLAGS  ?= -pthread

SAN_DBG   = -g -fno-omit-frame-pointer -O1
ASAN_FLAGS = $(SAN_DBG) -fsanitize=address,undefined
TSAN_FLAGS = $(SAN_DBG) -fsanitize=thread

BINS = shardgate packet_client keyhash
SRCS = src/main.cpp src/client.cpp tools/keyhash.cpp

all: $(BINS)

shardgate: src/main.cpp $(wildcard include/*.hpp)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(LDFLAGS)

packet_client: src/client.cpp $(wildcard include/*.hpp)
	$(CXX) $(CXXFLAGS) -o $@ src/client.cpp $(LDFLAGS)

keyhash: tools/keyhash.cpp include/sha256.hpp
	$(CXX) $(CXXFLAGS) -o $@ tools/keyhash.cpp

asan: clean-bins
	$(CXX) -std=c++20 -Wall -Wextra -Iinclude $(ASAN_FLAGS) -o shardgate src/main.cpp $(LDFLAGS)
	$(CXX) -std=c++20 -Wall -Wextra -Iinclude $(ASAN_FLAGS) -o packet_client src/client.cpp $(LDFLAGS)

tsan: clean-bins
	$(CXX) -std=c++20 -Wall -Wextra -Iinclude $(TSAN_FLAGS) -o shardgate src/main.cpp $(LDFLAGS)
	$(CXX) -std=c++20 -Wall -Wextra -Iinclude $(TSAN_FLAGS) -o packet_client src/client.cpp $(LDFLAGS)

format:
	clang-format -i $(SRCS) include/*.hpp

format-check:
	clang-format --dry-run --Werror $(SRCS) include/*.hpp

tidy: compile_commands.json
	clang-tidy $(SRCS) -p .

compile_commands.json: Makefile
	@echo '[' > $@; \
	first=1; \
	for f in $(SRCS); do \
	  [ $$first -eq 1 ] || echo ',' >> $@; first=0; \
	  printf '{"directory":"%s","command":"%s %s -c %s","file":"%s"}' \
	    "$$(pwd)" "$(CXX)" "$(CXXFLAGS)" "$$f" "$$f" >> $@; \
	done; \
	echo ']' >> $@

clean-bins:
	rm -f $(BINS)

clean: clean-bins
	rm -rf data compile_commands.json

.PHONY: all asan tsan format format-check tidy clean clean-bins
