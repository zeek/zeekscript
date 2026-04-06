#pragma once

#include <stdexcept>
#include <string>

// Thrown when the formatter encounters a malformed node tree.
class FormatError : public std::runtime_error {
public:
	FormatError(const std::string& msg) : std::runtime_error(msg) {}
};

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
	FmtContext(int indent, int col, int width, int trail = 0)
		: indent(indent), col(col), width(width), trail(trail) {}

	int Indent() const { return indent; }
	int Col() const { return col; }
	int Width() const { return width; }
	int Trail() const { return trail; }

	// Column where a fresh line starts at current indent.
	int IndentCol() const { return indent * INDENT_WIDTH; }

	// Derive a sub-context after emitting 'used' columns
	// on the current line.  Trail carries forward (same line).
	FmtContext After(int used) const
		{ return {indent, col + used, width - used, trail}; }

	// Maximum column (right edge) for this context.
	int MaxCol() const { return col + width; }

	// Derive a sub-context for a new line at one deeper
	// indent level.  Trail carries forward since trailing
	// content will appear after the last line.
	FmtContext Indented() const
		{
		int new_indent = indent + 1;
		int new_col = new_indent * INDENT_WIDTH;
		return {new_indent, new_col, MaxCol() - new_col, trail};
		}

	// Derive a sub-context for a new line at the same
	// indent level but a specific absolute column.
	FmtContext AtCol(int c) const
		{ return {indent, c, MaxCol() - c, trail}; }

	// Derive a context with additional trailing reservation.
	FmtContext Reserve(int n) const
		{ return {indent, col, width, trail + n}; }

private:
	int indent;
	int col;
	int width;
	int trail;	// columns reserved after last line (comment, etc.)
};
