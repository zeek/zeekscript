#pragma once

#include "node.h"

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
	// Parse the entire input, returning top-level nodes.
	// On error, prints to stderr and returns empty.
	static NodeVec Parse(const std::string& input);

private:
	Parser(const std::string& input) : input(input) {}

	NodeVec ParseFile();
	NodePtr ParseNode();
	std::string ParseTag();
	std::string ParseQuotedString();

	void SkipWhitespace();

	bool AtEnd() const { return pos >= input.size(); }
	char Peek() const { return input[pos]; }
	char Advance();

	void Error(const char* msg) const;

	const std::string& input;
	size_t pos = 0;
	int line = 1;
	int col = 1;
};
