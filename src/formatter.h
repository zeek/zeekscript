#pragma once

#include <string>
#include <vector>

#include "node.h"

static constexpr int INDENT_WIDTH = 8;  // columns per indent level

// The context passed down during formatting.  Everything is in absolute
// columns - tabs don't exist at this level.
//
// indent: base indentation level (0, 1, 2, ...)
// col:	absolute column where content starts on the current line
//      (>= indent * INDENT_WIDTH when there's an offset beyond base indent)
// width: columns available for content on this line (from col to the
//        right edge)

class FmtContext {
public:
	FmtContext(int indent, int col, int width)
		: indent(indent), col(col), width(width) {}

	int Indent() const { return indent; }
	int Col() const { return col; }
	int Width() const { return width; }

	// Column where a fresh line starts at current indent.
	int IndentCol() const { return indent * INDENT_WIDTH; }

	// Derive a sub-context after emitting 'used' columns
	// on the current line.
	FmtContext After(int used) const
		{ return {indent, col + used, width - used}; }

	// Maximum column (right edge) for this context.
	int MaxCol() const { return col + width; }

	// Derive a sub-context for a new line at one deeper
	// indent level, full width from that indent.
	FmtContext Indented() const
		{
		int max_col = MaxCol();
		int new_indent = indent + 1;
		int new_col = new_indent * INDENT_WIDTH;
		return {new_indent, new_col, max_col - new_col};
		}

	// Derive a sub-context for a new line at the same
	// indent level but a specific absolute column.
	FmtContext AtCol(int c) const
		{ return {indent, c, MaxCol() - c}; }

private:
	int indent;
	int col;
	int width;
};

// A single formatting candidate: the actual text plus metrics that let
// a parent choose among alternatives.
class Candidate {
public:
	// Single-line candidate.
	Candidate(const std::string& t, int w)
		: text(t), width(w), lines(1), overflow(0) {}

	// Multi-line candidate.
	Candidate(const std::string& t, int w, int l, int ovf)
		: text(t), width(w), lines(l), overflow(ovf) {}

	const std::string& Text() const { return text; }
	int Width() const { return width; }
	int Lines() const { return lines; }
	int Ovf() const { return overflow; }

	// Is this a clean single-line result?
	bool Fits() const { return lines == 1 && overflow <= 0; }

	// Comparison: fewer overflows wins, then fewer lines, then narrower.
	bool BetterThan(const Candidate& o) const;

private:
	std::string text;
	int width;	// width of last (or only) line
	int lines;	// number of lines (1 = single line)
	int overflow;	// columns past the allowed width
};

using Candidates = std::vector<Candidate>;

// Pick the best candidate from a non-empty vector.
const Candidate& Best(const Candidates& cs);

// Top-level entry point: format a list of top-level nodes.
std::string Format(const Node::NodeVec& nodes);

// Format a single node in a given context, returning one or more candidates.
Candidates FormatNode(const Node& node, const FmtContext& ctx);

// Emit a line prefix for a given indent level and starting column: tabs
// for indent levels, then spaces to reach the target column.  This is the
// *only* place tabs appear.
std::string LinePrefix(int indent, int col);
