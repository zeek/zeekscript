#pragma once

#include "layout.h"

#include <memory>
#include <string>
#include <vector>

// Recursive-descent parser for the .rep format.
//
// Grammar:
//   file   ::= node*
//   node   ::= TAG arg* ('{' node* '}')?
//   TAG    ::= non-whitespace, non-quote, non-brace chars
//   arg    ::= quoted-string
//   quoted-string ::= '"' (escape | char)* '"'

class Parser {
public:
	// Parse the entire input.  On error, prints to stderr.
	// Returns {nodes, had_error}.
	static std::pair<LayoutVec, bool> Parse(const std::string& input,
	                                        const char* filename = nullptr);

private:
	Parser(const std::string& input, const char* filename)
		: input(input), filename(filename) {}

	LayoutVec ParseFile();
	LayoutPtr ParseNode();
	std::string ParseTag();
	std::string ParseQuotedString();

	void SkipWhitespace();

	bool AtEnd() const { return pos >= input.size(); }
	char Peek() const { return input[pos]; }
	char Advance();

	void Error(const char* msg);

	const std::string& input;
	const char* filename;
	size_t pos = 0;
	int line = 1;
	int col = 1;
	bool had_error = false;
};
