#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "cand.h"

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

// Top-level entry point: format a list of top-level nodes.
std::string Format(const NodeVec& nodes);

// Format a single node in a given context, returning one or more candidates.
Candidates format_node(const Node& node, const FmtContext& ctx);

// Format an expression node, dispatching by tag.
Candidates format_expr(const Node& node, const FmtContext& ctx);

// Emit a line prefix for a given indent level and starting column: tabs
// for indent levels, then spaces to reach the target column.  This is the
// *only* place tabs appear.
std::string line_prefix(int indent, int col);

// Layout combinator

// A component in a layout specification.  Implicit constructors
// let callers mix child indices, node pointers, strings, and
// SP markers freely:
//   BuildLayout({0U, soft_sp, Child(2), soft_sp, 3}, ctx)
class LayoutItem
	{
public:
	enum class Kind { Lit, Fmt, Sp, Tok } kind;

	// Literal text.
	LayoutItem(const std::string& s)
		: kind(Kind::Lit), fmt(s), must_break(false) {}
	LayoutItem(const char* s)
		: kind(Kind::Lit), fmt(s), must_break(false) {}
	LayoutItem(const Formatting& f)
		: kind(Kind::Lit), fmt(f), must_break(false) {}
	LayoutItem(Formatting&& f)
		: kind(Kind::Lit), fmt(std::move(f)), must_break(false) {}

	// Node to format (produces candidates).
	LayoutItem(const NodePtr& n)
		: kind(Kind::Fmt), node(n), must_break(false) {}

	// Child token: resolved by BuildLayout into a Lit via tok().
	// Use 0U for child 0 to avoid null-pointer ambiguity with
	// the const-char* constructor.
	LayoutItem(unsigned child_index) : kind(Kind::Tok),
		child_idx(static_cast<int>(child_index)), must_break(false) {}

	// Sub-child token: {parent_idx, child_idx} resolves to
	// tok(Child(parent_idx)->Child(child_idx)).
	LayoutItem(unsigned parent_index, unsigned sub_index)
		: kind(Kind::Tok),
		  child_idx(static_cast<int>(parent_index)),
		  sub_child_idx(static_cast<int>(sub_index)),
		  must_break(false) {}

	// Soft space (private; use soft_sp constant).
	LayoutItem(Kind k) : kind(k), must_break(false) {}

	const Formatting& Fmt() const { return fmt; }
	const NodePtr& LI_Node() const { return node; }
	int ChildIdx() const { return child_idx; }
	int SubChildIdx() const { return sub_child_idx; }
	bool MustBreak() const { return must_break; }

	void SetMustBreak(bool mb) { must_break = mb; }

private:
	Formatting fmt;
	NodePtr node;
	int child_idx = -1;
	int sub_child_idx = -1;
	bool must_break;	// force next Sp to break (trailing comment)
	};

extern const LayoutItem soft_sp;

// Token literal: wraps the node in a lazy Formatting piece and
// forces the next soft_sp to break if it has a trailing comment.
LayoutItem tok(const NodePtr& n);

// Build layout candidates from a sequence of components using
// beam search.  At each Fmt node, all of its candidates are tried;
// at each soft_sp, both "space" and "break + indent" are tried.
// The beam is pruned to the best candidates at each step.
using LayoutItems = std::vector<LayoutItem>;
Candidates build_layout(LayoutItems items, const FmtContext& ctx);
