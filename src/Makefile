CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -g
TARGET = zeek-format

SRCS = main.cc node.cc parser.cc tag.cc formatter.cc condition_block.cc
OBJS = $(SRCS:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Header dependencies
main.o: node.h parser.h tag.h formatter.h
node.o: node.h tag.h condition_block.h formatter.h
parser.o: parser.h node.h tag.h
tag.o: tag.h
formatter.o: formatter.h node.h tag.h condition_block.h
condition_block.o: condition_block.h formatter.h node.h tag.h

SHELL = /bin/bash

test: $(TARGET)
	@pass=0; fail=0; \
	for f in ../tests/formatting/test*.rep; do \
		name=$$(basename "$$f" .rep); \
		expected="../tests/formatting/$${name}.fmt.zeek"; \
		if diff -q <(./$(TARGET) "$$f") "$$expected" \
		    >/dev/null 2>&1; then \
			pass=$$((pass + 1)); \
		else \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "$$pass pass, $$fail fail"

roundtrip: $(TARGET)
	@pass=0; fail=0; \
	for f in ../tests/formatting/test*.rep; do \
		if diff -q <(./$(TARGET) --dump "$$f") "$$f" \
		    >/dev/null 2>&1; then \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$f"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "$$pass pass, $$fail fail"; \
	[ $$fail -eq 0 ]

new: $(TARGET)
	@for f in ../tests/formatting/test*.rep; do \
		name=$$(basename "$$f" .rep); \
		./$(TARGET) "$$f" > "../tests/formatting/$${name}.new"; \
	done; \
	echo "wrote .new files for $$(ls ../tests/formatting/test*.new | wc -l | tr -d ' ') tests"

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean test roundtrip new
