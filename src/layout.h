#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cand.h"
#include "fmt_context.h"
#include "tag.h"

// Forward declarations needed by LayoutItem.
class Layout;
using LayoutPtr = std::shared_ptr<Layout>;
using LayoutVec = std::vector<LayoutPtr>;
extern const LayoutPtr null_node;

class LayoutItem;
using LIPtr = std::shared_ptr<LayoutItem>;
using ComputeFn = LIPtr (Layout::*)(const FmtContext&) const;

// Top-level entry point: format a list of top-level nodes.
std::string Format(const LayoutVec& nodes);

// Format a single node in a given context, returning one or more candidates.
Candidates format_node(const Layout& node, const FmtContext& ctx);

// Format an expression node, dispatching by tag.
Candidates format_expr(const Layout& node, const FmtContext& ctx);

// Emit a line prefix for a given indent level and starting column: tabs
// for indent levels, then spaces to reach the target column.  This is the
// *only* place tabs appear.
std::string line_prefix(int indent, int col);

// Beam search partial result.
struct Partial {
	Formatting fmt;
	int col;      // current column (end of last line)
	int indent;   // current indent level
	int lines;
	int overflow;
	bool must_break;  // preceding token forces next Sp to break
	int align_col;    // alignment column set by ArgList for SoftCont
};

using Partials = std::vector<Partial>;

// Layout combinator

// Layout item kinds.
enum LIKind {
	Lit,	// literal Formatting text, emitted as-is
	FmtExpr,	// format a child as an expression (multi-candidate)
	Sp,	// soft space: beam explores both " " and newline+indent

	// Pre-beam resolution kinds - resolved to Lit/FmtExpr before
	// the beam search runs.
	Tok,	// child token by index, resolved to Lit
	ExprIdx,	// child expression by index, resolved to FmtExpr
	LastTok,	// last child token, resolved to Lit
	ArgIdx,	// numbered Arg() string, resolved to Lit

	// Composite kinds - resolved or handled specially by the beam.
	ArgList,	// parenthesized arg list: flat/fill/vertical
	FillList,	// keyword + args as fill (e.g. "print a, b, c")
	FlatSplit,	// flat-or-split binary/ternary expression

	DeclCands,	// declaration candidates (multi-candidate)

	// Indent/break control.
	IndentUp,	// increment indent level
	IndentDown,	// decrement indent level (pre-comments at inner indent)
	HardBreak,	// unconditional newline + indent

	// Continuation: space-or-break for trailing suffixes like
	// return types or attributes after an arg list.
	SoftCont,

	StmtBody,	// format children as an indented statement list
	BodyText,	// Whitesmith-style brace block body

	// Boolean chain fill: "a || b || c" with greedy line packing.
	OpFill,
};

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
	static FmtStep L(const LayoutPtr& n)
		{ return {SLit, Formatting(n), nullptr, -1}; }

	// Sub-expression: formatted in context, re-formatted after split.
	static FmtStep E(const LayoutPtr& n)
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
	LayoutPtr node;
	int child_idx;

private:
	FmtStep(Kind k, Formatting f, LayoutPtr n, int idx)
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

// Base class for layout items.  Resolution items (Tok, ExprIdx,
// computed kinds, etc.) use the base class directly.  Beam items
// (Lit, Sp, ArgList, etc.) are subclasses that override LayoutStep.
class LayoutItem
	{
public:
	LIKind kind;
	virtual ~LayoutItem() = default;

	// Beam search step: process this item against the current beam.
	// Default asserts - resolution items must be resolved before
	// the beam runs.
	virtual Partials LayoutStep(Partials& beam,
		const FmtContext& ctx, int trail) const;

	// Literal text.
	LayoutItem(const std::string& s)
		: kind(Lit), fmt(s), must_break(false) {}
	LayoutItem(const char* s)
		: kind(Lit), fmt(s), must_break(false) {}
	LayoutItem(const Formatting& f)
		: kind(Lit), fmt(f), must_break(false) {}
	LayoutItem(Formatting&& f)
		: kind(Lit), fmt(std::move(f)), must_break(false) {}

	// Layout to format (produces candidates).
	LayoutItem(const LayoutPtr& n)
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
	LayoutItem(LIKind k, const LayoutPtr& n, Formatting suffix)
		: kind(k), fmt(std::move(suffix)), node(n),
		  must_break(false) {}

	// Resolved arglist with flags: node + suffix + flags.
	LayoutItem(LIKind k, const LayoutPtr& n, Formatting suffix, int fl)
		: kind(k), fmt(std::move(suffix)), node(n),
		  sb_flags(fl), must_break(false) {}

	// Flat-or-split: steps + split points, resolved in the beam.
	LayoutItem(FmtSteps s, std::vector<SplitAt> sp, bool ff = false)
		: kind(FlatSplit), steps(std::move(s)),
		  splits(std::move(sp)), force_flat(ff),
		  must_break(false) {}

	// Resolved DeclCands: holds pre-computed Candidates.
	LayoutItem(LIKind k, Candidates cs)
		: kind(k), cands(std::move(cs)), must_break(false) {}

	// Computed layout item: kind + compute function pointer.
	LayoutItem(LIKind k, ComputeFn fn)
		: kind(k), compute_fn(fn), must_break(false) {}

	// ArgList with computed suffix.
	LayoutItem(LIKind k, unsigned child_index, ComputeFn suffix)
		: kind(k), child_idx(static_cast<int>(child_index)),
		  suffix_fn(suffix), must_break(false) {}

	// ArgList with computed prefix and suffix.
	LayoutItem(LIKind k, unsigned child_index,
	           ComputeFn prefix, ComputeFn suffix)
		: kind(k), child_idx(static_cast<int>(child_index)),
		  suffix_fn(suffix), prefix_fn(prefix),
		  must_break(false) {}

	// Resolved arglist with prefix, suffix, and optional flags.
	LayoutItem(LIKind k, const LayoutPtr& n,
	           Formatting prefix, Formatting suffix)
		: kind(k), fmt(std::move(suffix)), li_prefix(std::move(prefix)),
		  node(n), must_break(false) {}
	LayoutItem(LIKind k, const LayoutPtr& n,
	           Formatting prefix, Formatting suffix, int fl)
		: kind(k), fmt(std::move(suffix)), li_prefix(std::move(prefix)),
		  node(n), sb_flags(fl), must_break(false) {}

	// Resolved SoftCont/DeclCands: kind + content.
	LayoutItem(LIKind k, Formatting f)
		: kind(k), fmt(std::move(f)), must_break(false) {}

	// Resolved OpFill: operator string + operand nodes.
	LayoutItem(LIKind k, std::string op, LayoutVec ops)
		: kind(k), fmt(std::move(op)),
		  operands(std::move(ops)), must_break(false) {}

	const Formatting& Fmt() const { return fmt; }
	const LayoutPtr& LI_Node() const { return node; }
	int ChildIdx() const { return child_idx; }
	int SubChildIdx() const { return sub_child_idx; }
	int Flags() const { return sb_flags; }
	bool MustBreak() const { return must_break; }
	const FmtSteps& Steps() const { return steps; }
	const std::vector<SplitAt>& Splits() const { return splits; }
	bool ForceFlatSubs() const { return force_flat; }
	ComputeFn GetComputeFn() const { return compute_fn; }
	ComputeFn SuffixFn() const { return suffix_fn; }
	ComputeFn PrefixFn() const { return prefix_fn; }
	const LayoutVec& Operands() const { return operands; }
	const Candidates& Cands() const { return cands; }
	const Formatting& Prefix() const { return li_prefix; }

	void SetMustBreak(bool mb) { must_break = mb; }

protected:
	Formatting fmt;
	Formatting li_prefix;
	LayoutPtr node;
	LayoutVec operands;
	Candidates cands;
	FmtSteps steps;
	std::vector<SplitAt> splits;
	int child_idx = -1;
	int sub_child_idx = -1;
	int sb_flags = 0;
	bool force_flat = false;
	ComputeFn compute_fn = nullptr;
	ComputeFn suffix_fn = nullptr;
	ComputeFn prefix_fn = nullptr;
	bool must_break;	// force next Sp to break (trailing comment)
	};

using LayoutItems = std::vector<LIPtr>;

// ---- Beam item subclasses ------------------------------------------------
// Each overrides LayoutStep to implement its beam search behavior.
// Implementations are in layout.cc.

class LILit : public LayoutItem {
public:
	LILit(const Formatting& f) : LayoutItem(f) {}
	LILit(Formatting&& f) : LayoutItem(std::move(f)) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIExpr : public LayoutItem {
public:
	LIExpr(const LayoutPtr& n) : LayoutItem(n) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LISp : public LayoutItem {
public:
	LISp() : LayoutItem(Sp) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIHardBreak : public LayoutItem {
public:
	LIHardBreak() : LayoutItem(HardBreak) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIIndentUp : public LayoutItem {
public:
	LIIndentUp() : LayoutItem(IndentUp) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIIndentDown : public LayoutItem {
public:
	LIIndentDown() : LayoutItem(IndentDown) {}
	LIIndentDown(const LayoutPtr& n)
		: LayoutItem(IndentDown, n, Formatting()) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

// "R" = Resolved: these items hold fully resolved data (child nodes,
// formatted suffixes, etc.) produced by BuildLayout's resolution phase.
// Their unresolved counterparts live in the layout table as base
// LayoutItem instances and are replaced with these during resolution.

class LIArgListR : public LayoutItem {
public:
	LIArgListR(const LayoutPtr& n, Formatting suffix)
		: LayoutItem(ArgList, n, std::move(suffix)) {}
	LIArgListR(const LayoutPtr& n, Formatting suffix, int fl)
		: LayoutItem(ArgList, n, std::move(suffix), fl) {}
	LIArgListR(const LayoutPtr& n, Formatting prefix, Formatting suffix)
		: LayoutItem(ArgList, n, std::move(prefix),
			std::move(suffix)) {}
	LIArgListR(const LayoutPtr& n, Formatting prefix,
	           Formatting suffix, int fl)
		: LayoutItem(ArgList, n, std::move(prefix),
			std::move(suffix), fl) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIOpFillR : public LayoutItem {
public:
	LIOpFillR(std::string op, LayoutVec ops)
		: LayoutItem(OpFill, std::move(op), std::move(ops)) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIFlatSplitR : public LayoutItem {
public:
	LIFlatSplitR(FmtSteps s, std::vector<SplitAt> sp,
	             bool ff = false)
		: LayoutItem(std::move(s), std::move(sp), ff) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LIDeclCandsR : public LayoutItem {
public:
	LIDeclCandsR(Candidates cs)
		: LayoutItem(DeclCands, std::move(cs)) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

class LISoftContR : public LayoutItem {
public:
	LISoftContR(Formatting f) : LayoutItem(SoftCont, std::move(f)) {}
	Partials LayoutStep(Partials& beam, const FmtContext& ctx,
		int trail) const override;
};

// ---- Factory functions ---------------------------------------------------

// Token literal: wraps the node in a lazy Formatting piece and
// forces the next soft_sp to break if it has a trailing comment.
LIPtr tok(const LayoutPtr& n);

// Token by child index: resolved by BuildLayout into a Lit via tok().
// Use 0U for child 0 to avoid null-pointer ambiguity.
inline LIPtr tok(unsigned child_index)
	{ return std::make_shared<LayoutItem>(child_index); }

// Sub-child token: tok(parent_idx, child_idx) resolves to
// tok(Child(parent_idx)->Child(child_idx)).
inline LIPtr tok(unsigned parent_index, unsigned sub_index)
	{ return std::make_shared<LayoutItem>(parent_index, sub_index); }

// Literal text.
inline LIPtr lit(const char* s)
	{ return std::make_shared<LILit>(Formatting(s)); }
inline LIPtr lit(const std::string& s)
	{ return std::make_shared<LILit>(Formatting(s)); }
inline LIPtr lit(const Formatting& f) { return std::make_shared<LILit>(f); }
inline LIPtr lit(Formatting&& f)
	{ return std::make_shared<LILit>(std::move(f)); }

// Soft space: beam explores both " " and newline+indent.
inline LIPtr sp() { return std::make_shared<LISp>(); }

// Hard break: unconditional newline + indent.
inline LIPtr hard_brk() { return std::make_shared<LIHardBreak>(); }

// Indent control.
inline LIPtr indent_up() { return std::make_shared<LIIndentUp>(); }
inline LIPtr indent_down() { return std::make_shared<LayoutItem>(IndentDown); }

// Expression child by index: resolved by BuildLayout into a Fmt
// item via Child(n).  Parallel to integer Tok shorthand but the
// child is formatted as an expression (producing candidates).
inline LIPtr expr(unsigned child_index)
	{ return std::make_shared<LayoutItem>(ExprIdx, child_index); }

// Last child as token: resolved by BuildLayout into
// tok(Children().back()).
inline LIPtr last()
	{ return std::make_shared<LayoutItem>(LastTok); }

// Layout argument by index: resolved by BuildLayout into
// Formatting(Arg(n)) as a literal.
inline LIPtr arg(unsigned arg_index)
	{ return std::make_shared<LayoutItem>(ArgIdx, arg_index); }

// Computed layout item: resolved by BuildLayout via stored ComputeFn.
inline LIPtr computed(ComputeFn fn)
	{ return std::make_shared<LayoutItem>(Lit, fn); }

// DeclCands returns Candidates (not LIPtr) and is resolved
// by its own case in ResolveItem, not via ComputeFn.
inline LIPtr decl_cands()
	{ return std::make_shared<LayoutItem>(DeclCands); }

// Bracketed argument list: child at child_index is expected to have
// open/close brackets as first/last children.  Resolved by BuildLayout
// and handled in the beam via flat_or_fill.  Optional suffix is
// appended after the close bracket (e.g. return type).
inline LIPtr arglist(unsigned child_index)
	{ return std::make_shared<LayoutItem>(ArgList, child_index); }
inline LIPtr arglist(unsigned child_index, Formatting suffix)
	{ return std::make_shared<LayoutItem>(ArgList, child_index,
		std::move(suffix)); }
inline LIPtr arglist(unsigned child_index, ComputeFn suffix)
	{ return std::make_shared<LayoutItem>(ArgList, child_index, suffix); }
inline LIPtr arglist(unsigned child_index, int flags)
	{ return std::make_shared<LayoutItem>(ArgList, child_index, flags); }

// Bracketed argument list with a computed prefix, passed to
// flat_or_fill as the prefix argument (e.g. "function" before
// the param list in a lambda).
inline LIPtr arglist_prefix(unsigned child_index,
                            ComputeFn prefix, ComputeFn suffix = nullptr)
	{
	return std::make_shared<LayoutItem>(ArgList, child_index,
		prefix, suffix);
	}

// Bare fill list: flat_or_fill on collected args with the first
// child (keyword) as prefix.  No open/close brackets.
inline LIPtr fill_list() { return std::make_shared<LayoutItem>(FillList); }

// Statement body: formats children as a statement list at the
// current indent level, prepending "\n".  Default selects non-token
// children; SB_AllChildren selects all.  Use SBFlag for options.
inline LIPtr stmt_body(int flags = 0)
	{ return std::make_shared<LayoutItem>(StmtBody, flags); }
inline LIPtr stmt_body(unsigned child_index, int flags = 0)
	{ return std::make_shared<LayoutItem>(StmtBody, child_index, flags); }

// Body text: formats a BODY child via FormatBodyText (Whitesmith
// block if braced, indented single statement otherwise).
inline LIPtr body_text(unsigned child_index)
	{ return std::make_shared<LayoutItem>(BodyText, child_index); }

// Soft continuation: content is placed inline (space + content)
// or on a continuation line (break + indent + content).  Both
// options enter the beam; the best is selected by pruning.
// Empty content is a no-op.
inline LIPtr soft_cont(ComputeFn op)
	{ return std::make_shared<LayoutItem>(SoftCont, 0U, op); }

// Operator fill: format content children separated by arg(0),
// try flat, then greedy-fill with wrap at operator.
inline LIPtr op_fill() { return std::make_shared<LayoutItem>(OpFill); }

// Flat-or-split with deferred child references: FmtStep::EI(n)
// and FmtStep::TI(n) are resolved during BuildLayout.
inline LIPtr flat_split(FmtSteps s, std::vector<SplitAt> sp,
                        bool ff = false)
	{
	return std::make_shared<LayoutItem>(std::move(s), std::move(sp), ff);
	}

// Build layout candidates from a sequence of components using
// beam search.  At each Fmt node, all of its candidates are tried;
// at each soft_sp, both "space" and "break + indent" are tried.
// The beam is pruned to the best candidates at each step.
Candidates build_layout(LayoutItems items, const FmtContext& ctx);

// A node in the representation tree.  Each node has:
//   - tag:      semantic type (e.g. Tag::BinaryOp)
//   - args:     zero or more string arguments
//   - children: zero or more child nodes (from { } block)
//
// Leaf nodes like  IDENTIFIER "x"  have args, no children.
// Container nodes like  CALL { ... }  have children (and
// possibly args).
// Bare markers like  SEMI  or  BLANK  have neither.
//
// Nodes with a layout specification are formatted declaratively
// via BuildLayout + beam search.  Other nodes (tokens, markers,
// preproc) use the default fallback.

class Layout {
public:
	Layout(Tag tag) : tag(tag) {}
	Layout(Tag tag, const LayoutItems& li) : tag(tag), layout(li) {}

	Candidates Format(const FmtContext& ctx) const;

	// Layout combinator: resolves LayoutItems (tokens, computed
	// kinds, etc.) before delegating to the beam-search engine.
	Candidates BuildLayout(LayoutItems items,
	                       const FmtContext& ctx) const;

	// Resolve a single layout item during BuildLayout.
	void ResolveItem(LayoutItems& items, size_t i,
	                 const FmtContext& ctx) const;

	Tag GetTag() const { return tag; }
	bool IsLambda() const { return is_lambda(tag); }
	bool IsMarker() const { return is_marker(tag); }
	bool IsToken() const { return is_token(tag); }
	bool IsType() const { return is_type_tag(tag); }

	const std::vector<std::string>& Args() const { return args; }

	const LayoutVec& Children() const { return children; }
	LayoutVec& Children() { return children; }

	// Direct positional child access.
	const LayoutPtr& Child(size_t i) const { return children[i]; }

	// Positional child access with tag verification.
	const LayoutPtr& Child(size_t i, Tag t) const;

	// Find a child node by tag, or null LayoutPtr if absent.
	const LayoutPtr& FindOptChild(Tag tag) const;

	// Find a required child node by tag.  Aborts if not found.
	const LayoutPtr& FindChild(Tag tag) const;

	// Find a child by tag, starting after the given child.
	const LayoutPtr& FindChild(Tag tag, const LayoutPtr& after) const;

	// Collect non-token, non-comment children.
	LayoutVec ContentChildren() const;

	// Same but there must be at least n or throw an exception.
	LayoutVec ContentChildren(const char* name, int n) const;

	const std::string& TrailingComment() const { return trailing_comment; }
	void SetTrailingComment(std::string c)
		{ trailing_comment = " " + std::move(c); }

	const std::vector<std::string>& PreComments() const
		{ return pre_comments; }
	void AddPreComment(std::string c)
		{ pre_comments.push_back(std::move(c)); }
	bool MustBreakBefore() const { return ! pre_comments.empty(); }

	// Marker nodes (BLANK, etc.) that appeared between the
	// pre-comments and this node - preserved for round-trip.
	const LayoutVec& PreMarkers() const { return pre_markers; }
	void AddPreMarker(LayoutPtr m)
		{ pre_markers.push_back(std::move(m)); }

	void AddArg(std::string a) { args.push_back(std::move(a)); }
	void AddChild(LayoutPtr child)
		{ children.push_back(std::move(child)); }

	// Convenience: i-th arg, or empty string if absent.
	const std::string& Arg(size_t i = 0) const;

	// Complete text for this node: for tokens, the syntax
	// string + trailing comment; for atoms, the arg + trailing
	// comment; for composite nodes, just the trailing comment.
	std::string Text() const;

	// Width of this node's text in columns.
	int Width() const { return static_cast<int>(Text().size()); }

	// True if a line break must follow this node (has a
	// trailing comment).
	bool MustBreakAfter() const { return ! trailing_comment.empty(); }

	bool HasBlock() const { return has_block; }
	void SetHasBlock() { has_block = true; }

	bool HasChildren() const { return ! children.empty(); }

	// Emit pre-comments and pre-markers as indented lines.
	FmtPtr EmitPreComments(const std::string& pad) const;

	// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
	const LayoutPtr& FindTypeChild() const;

	// Compute functions for declarative BuildLayout resolution.
	LIPtr ComputeRetType(const FmtContext& ctx) const;
	LIPtr ComputeParamType(const FmtContext& ctx) const;
	LIPtr ComputeOfType(const FmtContext& ctx) const;
	LIPtr ComputeEnumBody(const FmtContext& ctx) const;
	LIPtr ComputeRedefEnumBody(const FmtContext& ctx) const;
	LIPtr ComputeRecordBody(const FmtContext& ctx) const;
	LIPtr ComputeElseFollowOn(const FmtContext& ctx) const;
	LIPtr ComputeFuncRet(const FmtContext& ctx) const;
	LIPtr ComputeFuncAttrs(const FmtContext& ctx) const;
	LIPtr ComputeFuncBody(const FmtContext& ctx) const;
	LIPtr ComputeLambdaPrefix(const FmtContext& ctx) const;
	LIPtr ComputeLambdaRet(const FmtContext& ctx) const;
	LIPtr ComputeLambdaBody(const FmtContext& ctx) const;
	LIPtr ComputeSwitchExpr(const FmtContext& ctx) const;
	LIPtr ComputeSwitchCases(const FmtContext& ctx) const;
	Candidates ComputeDecl(const FmtContext& ctx) const;

	// Format an ATTR-LIST node as a single string.
	Formatting FormatAttrList(const FmtContext& ctx) const;

	// Format an ATTR-LIST node as individual attr strings.
	std::vector<Formatting> FormatAttrStrings(const FmtContext& ctx) const;

	// Format a BODY node: Whitesmith block or indented single stmt.
	FmtPtr FormatBodyText(const FmtContext& ctx) const;

	// Format a Whitesmith-style braced block.
	Formatting FormatWhitesmithBlock(const FmtContext& ctx) const;

	// Preprocessor directive formatting and depth control.
	FmtPtr FormatText() const;
	bool OpensDepth() const;
	bool ClosesDepth() const;
	bool AtColumnZero() const;

	// Debug: print tree to stdout.
	void Dump(int indent = 0) const;

private:
	Tag tag;
	LayoutItems layout;
	std::vector<std::string> args;
	LayoutVec children;
	std::string trailing_comment;
	std::vector<std::string> pre_comments;
	LayoutVec pre_markers;
	bool has_block = false;
};

// Factory: creates a Layout with optional layout items based on tag.
LayoutPtr MakeNode(Tag tag);
