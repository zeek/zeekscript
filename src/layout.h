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
              ArgList, FillList, FlatSplit, Computed, IndentUp,
              IndentDown, HardBreak, StmtBody };

// Shared context for Computed layout items within a single
// BuildLayout call.  Earlier compute functions can populate
// fields that later ones read.
struct ComputeCtx {
	Formatting fmt;
};

class LayoutItem;
using ComputeFn = LayoutItem (Node::*)(ComputeCtx&, const FmtContext&) const;

// A piece in a flat-or-split sequence.
class FmtStep {
public:
	enum Kind { SLit, SExpr, SSp, SExprIdx, STokIdx };

	// Fixed text.
	static FmtStep L(const char* s)
		{ return {SLit, Formatting(s), nullptr, -1}; }
	static FmtStep L(const std::string& s)
		{ return {SLit, Formatting(s), nullptr, -1}; }
	static FmtStep L(const Formatting& f)
		{ return {SLit, f, nullptr, -1}; }
	static FmtStep L(const NodePtr& n)
		{ return {SLit, Formatting(n), nullptr, -1}; }

	// Sub-expression: formatted in context, re-formatted after split.
	static FmtStep E(const NodePtr& n)
		{ return {SExpr, Formatting(), n, -1}; }

	// Expression child by index: resolved during BuildLayout.
	static FmtStep EI(unsigned idx)
		{ return {SExprIdx, Formatting(), nullptr, static_cast<int>(idx)}; }

	// Token child by index: resolved during BuildLayout.
	static FmtStep TI(unsigned idx)
		{ return {STokIdx, Formatting(), nullptr, static_cast<int>(idx)}; }

	// Soft space: included in flat, dropped after a split point.
	static FmtStep S(const std::string& s = " ")
		{ return {SSp, Formatting(s), nullptr, -1}; }

	Kind kind;
	Formatting text;
	NodePtr node;
	int child_idx;

private:
	FmtStep(Kind k, Formatting f, NodePtr n, int idx)
		: kind(k), text(std::move(f)), node(std::move(n)),
		  child_idx(idx) {}
};

using FmtSteps = std::vector<FmtStep>;

// Where and how to break when flat overflows.
struct SplitAt {
	int after;	// break after this piece index

	enum Style {
		Indented,	// ctx.Indented()
		SameCol,	// ctx.AtCol(ctx.Col())
		IndentedOrSame,	// Indented if at indent col, else SameCol
		AlignWith,	// ctx.AtCol(flat position of piece align_piece)
	};

	Style style;
	int align_piece = -1;
	bool skip_if_multiline = false;
};

// Flags for ArgList layout items.
enum ALFlag {
	AL_TrailingCommaVertical = 1,  // trailing comma -> vertical
	AL_VerticalUpgrade       = 2,  // fill-wraps-all -> vertical
	AL_FlatOrVertical        = 4,  // vertical instead of fill on overflow
	AL_AllCommentsVertical   = 8,  // all items have comments -> vertical
	AL_TrailingCommaFill    = 16, // trailing comma preserved in fill layout
};

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

	// Kind only, with optional flags (used by soft_sp, stmt_body, etc.).
	LayoutItem(LIKind k, int fl = 0)
		: kind(k), sb_flags(fl), must_break(false) {}

	// Kind + child index + suffix (for arglist with suffix).
	LayoutItem(LIKind k, unsigned child_index, Formatting suffix)
		: kind(k), fmt(std::move(suffix)),
		  child_idx(static_cast<int>(child_index)),
		  must_break(false) {}

	// Kind + child index + flags (for stmt_body with child).
	LayoutItem(LIKind k, unsigned child_index, int fl)
		: kind(k), child_idx(static_cast<int>(child_index)),
		  sb_flags(fl), must_break(false) {}

	// Resolved arglist: node + suffix.
	LayoutItem(LIKind k, const NodePtr& n, Formatting suffix)
		: kind(k), fmt(std::move(suffix)), node(n),
		  must_break(false) {}

	// Resolved arglist with flags: node + suffix + flags.
	LayoutItem(LIKind k, const NodePtr& n, Formatting suffix, int fl)
		: kind(k), fmt(std::move(suffix)), node(n),
		  sb_flags(fl), must_break(false) {}

	// Flat-or-split: steps + split points, resolved in the beam.
	LayoutItem(FmtSteps s, std::vector<SplitAt> sp, bool ff = false)
		: kind(FlatSplit), steps(std::move(s)),
		  splits(std::move(sp)), force_flat(ff),
		  must_break(false) {}

	// Computed: calls a member function during BuildLayout resolution.
	LayoutItem(LIKind k, ComputeFn fn)
		: kind(k), compute_fn(fn), must_break(false) {}

	// ArgList with computed suffix.
	LayoutItem(LIKind k, unsigned child_index, ComputeFn fn)
		: kind(k), child_idx(static_cast<int>(child_index)),
		  compute_fn(fn), must_break(false) {}

	const Formatting& Fmt() const { return fmt; }
	const NodePtr& LI_Node() const { return node; }
	int ChildIdx() const { return child_idx; }
	int SubChildIdx() const { return sub_child_idx; }
	int Flags() const { return sb_flags; }
	bool MustBreak() const { return must_break; }
	const FmtSteps& Steps() const { return steps; }
	const std::vector<SplitAt>& Splits() const { return splits; }
	bool ForceFlatSubs() const { return force_flat; }
	ComputeFn CompFn() const { return compute_fn; }

	void SetMustBreak(bool mb) { must_break = mb; }

private:
	Formatting fmt;
	NodePtr node;
	FmtSteps steps;
	std::vector<SplitAt> splits;
	int child_idx = -1;
	int sub_child_idx = -1;
	int sb_flags = 0;
	bool force_flat = false;
	ComputeFn compute_fn = nullptr;
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
inline LayoutItem expr(unsigned child_index) { return {ExprIdx, child_index}; }

// Last child as token: resolved by BuildLayout into
// tok(Children().back()).
inline LayoutItem last() { return {LastTok}; }

// Node argument by index: resolved by BuildLayout into
// Formatting(Arg(n)) as a literal.
inline LayoutItem arg(unsigned arg_index) { return {ArgIdx, arg_index}; }

// Bracketed argument list: child at child_index is expected to have
// open/close brackets as first/last children.  Resolved by BuildLayout
// and handled in the beam via flat_or_fill.  Optional suffix is
// appended after the close bracket (e.g. return type).
inline LayoutItem arglist(unsigned child_index)
	{ return {ArgList, child_index}; }
inline LayoutItem arglist(unsigned child_index, Formatting suffix)
	{ return {ArgList, child_index, std::move(suffix)}; }
inline LayoutItem arglist(unsigned child_index, ComputeFn suffix_fn)
	{ return {ArgList, child_index, suffix_fn}; }
inline LayoutItem arglist(unsigned child_index, int flags)
	{ return {ArgList, child_index, flags}; }

// Bare fill list: flat_or_fill on collected args with the first
// child (keyword) as prefix.  No open/close brackets.
inline LayoutItem fill_list() { return {FillList}; }

// Statement body: formats children as a statement list at the
// current indent level, prepending "\n".  Default selects non-token
// children; SB_AllChildren selects all.  Use SBFlag for options.
inline LayoutItem stmt_body(int flags = 0) { return {StmtBody, flags}; }
inline LayoutItem stmt_body(unsigned child_index, int flags = 0)
	{ return {StmtBody, child_index, flags}; }

// Computed value: calls a member function on the node during
// BuildLayout resolution, replacing itself with the result.
inline LayoutItem compute(ComputeFn fn) { return {Computed, fn}; }

// Flat-or-split with deferred child references: FmtStep::EI(n)
// and FmtStep::TI(n) are resolved during BuildLayout.
inline LayoutItem flat_split(FmtSteps s, std::vector<SplitAt> sp,
                             bool ff = false)
	{ return {std::move(s), std::move(sp), ff}; }

// Build layout candidates from a sequence of components using
// beam search.  At each Fmt node, all of its candidates are tried;
// at each soft_sp, both "space" and "break + indent" are tried.
// The beam is pruned to the best candidates at each step.
using LayoutItems = std::vector<LayoutItem>;
Candidates build_layout(LayoutItems items, const FmtContext& ctx);
