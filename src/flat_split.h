#pragma once

// Declarative flat-or-split formatting: describe a sequence of pieces
// and one or more split points, and the interpreter tries flat first
// then generates split candidates.

#include "layout.h"

#include <vector>

// A piece in a formatting sequence.
class FmtStep {
public:
	enum Kind { Lit, Expr, Sp };

	// Fixed text: string literal, Formatting, or NodePtr token.
	static FmtStep L(const char* s)
		{ return {Lit, Formatting(s), nullptr}; }
	static FmtStep L(const std::string& s)
		{ return {Lit, Formatting(s), nullptr}; }
	static FmtStep L(const Formatting& f)
		{ return {Lit, f, nullptr}; }
	static FmtStep L(const NodePtr& n)
		{ return {Lit, Formatting(n), nullptr}; }

	// Sub-expression: formatted in flat context initially,
	// re-formatted with split context after a break.
	static FmtStep E(const NodePtr& n)
		{ return {Expr, Formatting(), n}; }

	// Soft space: included in flat layout, dropped immediately
	// after a split point (the newline+indent replaces it).
	static FmtStep S(const std::string& s = " ")
		{ return {Sp, Formatting(s), nullptr}; }

	Kind kind;
	Formatting text;	// Lit/Sp: the text; Expr: filled by interpreter
	NodePtr node;		// Expr: node to format

private:
	FmtStep(Kind k, Formatting f, NodePtr n)
		: kind(k), text(std::move(f)), node(std::move(n)) {}
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
	int align_piece = -1;		// for AlignWith
	bool skip_if_multiline = false;	// skip when sub-exprs are multiline
};

// Try flat layout first.  If overflow, generate a split candidate
// for each SplitAt spec by breaking after the designated piece and
// re-formatting any Expr pieces after the break with the derived
// context.  Returns flat + any split candidates.
Candidates flat_or_split(FmtSteps steps, const std::vector<SplitAt>& splits,
                         const FmtContext& ctx);
