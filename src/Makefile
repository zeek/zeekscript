CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -g
TARGET = zeek-format

SRCS = main.cc node.cc parser.cc tag.cc formatting.cc cand.cc core.cc \
       layout.cc flat_split.cc fmt_util.cc expr.cc type.cc decl.cc \
       stmt.cc top.cc condition_block.cc
OBJS = $(SRCS:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Header dependencies
main.o: cand.h fmt_context.h layout.h formatting.h node.h parser.h tag.h
node.o: cand.h fmt_context.h layout.h formatting.h node.h stmt.h tag.h
parser.o: cand.h formatting.h node.h parser.h tag.h
tag.o: tag.h
formatting.o: fmt_context.h formatting.h node.h
cand.o: cand.h fmt_context.h formatting.h node.h
core.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
layout.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
flat_split.o: cand.h flat_split.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
fmt_util.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h stmt.h tag.h
expr.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
type.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
decl.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h stmt.h tag.h
stmt.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h stmt.h tag.h
top.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h
condition_block.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h node.h tag.h

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
