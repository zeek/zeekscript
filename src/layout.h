#pragma once

#include <string>
#include <vector>

#include "cand.h"
#include "fmt_context.h"

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

// Layout item kinds.
enum LIKind { Lit, FmtExpr, Sp, Tok, ExprIdx, LastTok, ArgIdx,
              ArgList, IndentUp, IndentDown, HardBreak, StmtBody };

// Flags for StmtBody layout items.
enum SBFlag {
	SB_AllChildren  = 1,  // use all children (default: non-token)
	SB_SkipBlanks   = 2,  // skip leading blank lines
	SB_StripNewline = 4,  // strip trailing newline from result
	SB_RelocComment = 8,  // relocate close-brace trailing comment
};

// A component in a layout specification.  Implicit constructors
// let callers mix child indices, node pointers, strings, and
// SP markers freely:
//   BuildLayout({0U, soft_sp, Child(2), soft_sp, 3}, ctx)
class LayoutItem
	{
public:
	LIKind kind;

	// Literal text.
	LayoutItem(const std::string& s)
		: kind(Lit), fmt(s), must_break(false) {}
	LayoutItem(const char* s)
		: kind(Lit), fmt(s), must_break(false) {}
	LayoutItem(const Formatting& f)
		: kind(Lit), fmt(f), must_break(false) {}
	LayoutItem(Formatting&& f)
		: kind(Lit), fmt(std::move(f)), must_break(false) {}

	// Node to format (produces candidates).
	LayoutItem(const NodePtr& n)
		: kind(FmtExpr), node(n), must_break(false) {}

	// Child token: resolved by BuildLayout into a Lit via tok().
	// Use 0U for child 0 to avoid null-pointer ambiguity with
	// the const-char* constructor.
	LayoutItem(unsigned child_index) : kind(Tok),
		child_idx(static_cast<int>(child_index)), must_break(false) {}

	// Sub-child token: {parent_idx, child_idx} resolves to
	// tok(Child(parent_idx)->Child(child_idx)).
	LayoutItem(unsigned parent_index, unsigned sub_index)
		: kind(Tok),
		  child_idx(static_cast<int>(parent_index)),
		  sub_child_idx(static_cast<int>(sub_index)),
		  must_break(false) {}

	// Kind + child index (used by expr() helper).
	LayoutItem(LIKind k, unsigned child_index)
		: kind(k), child_idx(static_cast<int>(child_index)),
		  must_break(false) {}

	// Soft space (private; use soft_sp constant).
	LayoutItem(LIKind k) : kind(k), must_break(false) {}

	// Kind + child index + suffix (for arglist with suffix).
	LayoutItem(LIKind k, unsigned child_index, Formatting suffix)
		: kind(k), fmt(std::move(suffix)),
		  child_idx(static_cast<int>(child_index)),
		  must_break(false) {}

	// Resolved arglist: node + suffix.
	LayoutItem(LIKind k, const NodePtr& n, Formatting suffix)
		: kind(k), fmt(std::move(suffix)), node(n),
		  must_break(false) {}

	const Formatting& Fmt() const { return fmt; }
	const NodePtr& LI_Node() const { return node; }
	int ChildIdx() const { return child_idx; }
	int SubChildIdx() const { return sub_child_idx; }
	int Flags() const { return sb_flags; }
	bool MustBreak() const { return must_break; }

	void SetMustBreak(bool mb) { must_break = mb; }

private:
	friend LayoutItem stmt_body(int flags);
	friend LayoutItem stmt_body(unsigned child_index, int flags);

	Formatting fmt;
	NodePtr node;
	int child_idx = -1;
	int sub_child_idx = -1;
	int sb_flags = 0;
	bool must_break;	// force next Sp to break (trailing comment)
	};

extern const LayoutItem soft_sp;
extern const LayoutItem hard_break;
extern const LayoutItem indent_up;
extern const LayoutItem indent_down;

// Token literal: wraps the node in a lazy Formatting piece and
// forces the next soft_sp to break if it has a trailing comment.
LayoutItem tok(const NodePtr& n);

// Expression child by index: resolved by BuildLayout into a Fmt
// item via Child(n).  Parallel to integer Tok shorthand but the
// child is formatted as an expression (producing candidates).
LayoutItem expr(unsigned child_index);

// Last child as token: resolved by BuildLayout into
// tok(Children().back()).
LayoutItem last();

// Node argument by index: resolved by BuildLayout into
// Formatting(Arg(n)) as a literal.
LayoutItem arg(unsigned arg_index);

// Bracketed argument list: child at child_index is expected to have
// open/close brackets as first/last children.  Resolved by BuildLayout
// and handled in the beam via flat_or_fill.  Optional suffix is
// appended after the close bracket (e.g. return type).
LayoutItem arglist(unsigned child_index);
LayoutItem arglist(unsigned child_index, Formatting suffix);

// Statement body: formats children as a statement list at the
// current indent level, prepending "\n".  Default selects non-token
// children; SB_AllChildren selects all.  Use SBFlag for options.
LayoutItem stmt_body(int flags = 0);
LayoutItem stmt_body(unsigned child_index, int flags = 0);

// Build layout candidates from a sequence of components using
// beam search.  At each Fmt node, all of its candidates are tried;
// at each soft_sp, both "space" and "break + indent" are tried.
// The beam is pruned to the best candidates at each step.
using LayoutItems = std::vector<LayoutItem>;
Candidates build_layout(LayoutItems items, const FmtContext& ctx);
