#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "node.h"

// Thrown when the formatter encounters a malformed node tree.
class FormatError : public std::runtime_error {
public:
	FormatError(const std::string& msg)
		: std::runtime_error(msg) {}
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
		: indent(indent), col(col), width(width),
		  trail(trail) {}

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

// A single formatting candidate: the actual text plus metrics that let
// a parent choose among alternatives.
class Candidate {
public:
	// Single-line candidate with overflow computed from context.
	// Accounts for trailing reservation (e.g. comment after stmt).
	Candidate(const std::string& t, const FmtContext& ctx)
		: text(t), width(static_cast<int>(t.size())),
		  lines(1), spread(0)
		{
		int avail = ctx.Width() - ctx.Trail();
		int excess = width - avail;
		overflow = excess > 0 ? excess : 0;
		}

	Candidate(Formatting t, const FmtContext& ctx)
		: text(std::move(t)),
		  width(text.Size()),
		  lines(1), spread(0)
		{
		int avail = ctx.Width() - ctx.Trail();
		int excess = width - avail;
		overflow = excess > 0 ? excess : 0;
		}

	// Multi-line candidate.  first_col is the absolute column where the
	// first line starts (needed for balance/spread computation).
	Candidate(const std::string& t, int w, int l, int ovf,
	          int first_col = 0)
		: text(t), width(w), lines(l), overflow(ovf),
		  spread(l > 1 ? ComputeSpread(text.Str(), first_col) : 0) {}

	Candidate(std::string&& t, int w, int l, int ovf,
	          int first_col = 0)
		: text(std::move(t)), width(w), lines(l), overflow(ovf),
		  spread(l > 1 ? ComputeSpread(text.Str(), first_col) : 0) {}

	Candidate(Formatting t, int w, int l, int ovf,
	          int first_col = 0)
		: text(std::move(t)), width(w), lines(l), overflow(ovf),
		  spread(l > 1 ? ComputeSpread(text.Str(), first_col) : 0) {}

	Candidate(std::string t)
		: text(std::move(t)),
		  width(text.Size()),
		  lines(1), overflow(0), spread(0) { }

	const std::string& Text() const { return text.Str(); }
	int Width() const { return width; }
	int Lines() const { return lines; }
	int Ovf() const { return overflow; }
	int Spread() const { return spread; }

	// Build a new single-line candidate by appending a literal string.
	// Overflow is not set; use In() to finalize.
	Candidate Cat(const std::string& s) const
		{ return Candidate(Text() + s); }

	// Build a new single-line candidate by appending another candidate.
	// Overflow is not set; use In() to finalize.
	Candidate Cat(const Candidate& o) const
		{ return Candidate(Text() + o.Text()); }

	// Return a copy with overflow computed against a context.
	Candidate In(const FmtContext& ctx) const
		{ return Candidate(Text(), ctx); }

	// Is this a clean single-line result?
	bool Fits() const { return lines == 1 && overflow <= 0; }

	// Comparison: fewer overflows wins, then fewer lines, then
	// more balanced (smaller spread).
	bool BetterThan(const Candidate& o) const;

private:
	// Compute spread (max line width - min line width) from text.
	// first_col is the absolute column where the first line starts.
	static int ComputeSpread(const std::string& text, int first_col);

	Formatting text;
	int width;	// width of last (or only) line
	int lines;	// number of lines (1 = single line)
	int overflow;	// columns past the allowed width
	int spread;	// max line width - min line width (0 = balanced)
};

// Pick the best candidate from a non-empty vector.
const Candidate& best(const Candidates& cs);

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
// let callers mix strings, node pointers, and SP markers freely:
//   build_layout({prefix, soft_sp, node, soft_sp, suffix}, ctx)
class LayoutItem
	{
public:
	enum class Kind { Lit, Fmt, Sp } kind;

	// Literal string.
	LayoutItem(const std::string& s)
		: kind(Kind::Lit), text(s), node(nullptr), must_break(false) {}
	LayoutItem(const char* s)
		: kind(Kind::Lit), text(s), node(nullptr), must_break(false) {}

	// Node to format (produces candidates).
	LayoutItem(const Node* n)
		: kind(Kind::Fmt), node(n), must_break(false) {}
	LayoutItem(const NodePtr& n)
		: kind(Kind::Fmt), node(n.get()), must_break(false) {}

	// Soft space (private; use soft_sp constant).
	LayoutItem(Kind k) : kind(k), node(nullptr), must_break(false) {}

	const std::string& Text() const { return text; }
	const Node* LI_Node() const { return node; }
	bool MustBreak() const { return must_break; }

	void SetMustBreak(bool _must_break) { must_break = _must_break; }

private:
	std::string text;
	const Node* node;
	bool must_break;	// force next Sp to break (trailing comment)
	};

extern const LayoutItem soft_sp;

// Token literal: emits node->Text() and forces the next soft_sp
// to break if the token has a trailing comment.
LayoutItem tok(const Node* n);
LayoutItem tok(const NodePtr& n);

// Build layout candidates from a sequence of components using
// beam search.  At each Fmt node, all of its candidates are tried;
// at each soft_sp, both "space" and "break + indent" are tried.
// The beam is pruned to the best candidates at each step.
using LayoutItems = std::initializer_list<LayoutItem>;
Candidates build_layout(LayoutItems items_init, const FmtContext& ctx);
