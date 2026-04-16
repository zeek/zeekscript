CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O3
TARGET = clz-format

SRCS = main.cc parser.cc tag.cc formatting.cc cand.cc \
       layout.cc layout_util.cc flat_split.cc fmt_util.cc fmt_options.cc top.cc
OBJS = $(SRCS:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Header dependencies
main.o: cand.h fmt_context.h layout.h formatting.h parser.h tag.h
parser.o: cand.h formatting.h layout.h parser.h tag.h
tag.o: tag.h
formatting.o: fmt_context.h formatting.h layout.h
cand.o: cand.h fmt_context.h formatting.h
layout.o: cand.h flat_split.h fmt_context.h fmt_options.h fmt_util.h layout.h formatting.h tag.h
layout_util.o: cand.h fmt_context.h fmt_options.h fmt_util.h layout.h formatting.h tag.h
flat_split.o: cand.h flat_split.h fmt_context.h fmt_util.h layout.h formatting.h tag.h
fmt_options.o: fmt_options.h
fmt_util.o: cand.h fmt_context.h fmt_options.h fmt_util.h layout.h formatting.h tag.h
top.o: cand.h fmt_context.h fmt_util.h layout.h formatting.h tag.h

SHELL = /bin/bash
PYTHON ?= python3.11
EMITTER ?= ../zeekscript/emit_ast.py

test: $(TARGET)
	@pass=0; fail=0; \
	for f in ../tests/formatting/test*.zeek; do \
		name=$$(basename "$$f" .zeek); \
		expected="../tests/formatting/$${name}.fmt"; \
		if diff -q <($(PYTHON) $(EMITTER) "$$f" | ./$(TARGET) -) "$$expected" \
		    >/dev/null 2>&1; then \
			pass=$$((pass + 1)); \
		else \
			echo "FAIL: $$name"; \
			fail=$$((fail + 1)); \
		fi; \
	done; \
	echo "$$pass pass, $$fail fail"

new: $(TARGET)
	@for f in ../tests/formatting/test*.zeek; do \
		name=$$(basename "$$f" .zeek); \
		$(PYTHON) $(EMITTER) "$$f" | ./$(TARGET) - > "../tests/formatting/$${name}.new"; \
	done; \
	echo "wrote .new files for $$(ls ../tests/formatting/test*.new | wc -l | tr -d ' ') tests"

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean test new
