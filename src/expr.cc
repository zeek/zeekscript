#include <algorithm>

#include "expr.h"
#include "fmt_util.h"

// Lambda without captures: function(params): ret { body }
// Children: [0]=KEYWORD [1]=SP [2]=PARAMS ...
Formatting LambdaNode::BuildPrefix(const FmtContext& /*ctx*/) const
	{
	return Formatting(Child(0, Tag::Keyword));
	}

// Lambda with captures: function[captures](params): ret { body }
// Children: [0]=KEYWORD [1]=SP [2]=CAPTURES [3]=PARAMS ...
Formatting LambdaCapturesNode::BuildPrefix(const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	auto captures = Child(2, Tag::Captures);
	auto clb = captures->Child(0, Tag::LBracket);
	const auto& crb = captures->Children().back();
	auto cap_items = collect_args(captures->Children());

	if ( cap_items.empty() )
		return Formatting(kw) + clb + crb;

	auto cs = flat_or_fill(kw, clb, crb, "", cap_items, ctx);
	return best(cs).Fmt();
	}

Candidates LambdaNode::Format(const FmtContext& ctx) const
	{
	return FormatLambda(BuildPrefix(ctx), ctx);
	}


// LAMBDA:          [0]=KW [1]=SP [2]=PARAMS [opt COLON RETURNS] BODY(last)
// LAMBDA-CAPTURES: [0]=KW [1]=SP [2]=CAPTURES [3]=PARAMS [opt ...] BODY(last)
Candidates LambdaNode::FormatLambda(const Formatting& prefix,
                                    const FmtContext& ctx) const
	{
	int pp = ParamsPos();

	// Return type (COLON and RETURNS follow PARAMS when present).
	Formatting ret_fmt;
	auto after_params = Child(pp + 1);
	if ( after_params->GetTag() == Tag::Colon )
		{
		auto returns = Child(pp + 2, Tag::Returns);
		if ( auto rt = returns->FindTypeChild() )
			ret_fmt = Formatting(after_params) + " " +
				best(format_expr(*rt, ctx)).Fmt();
		}

	// Params.
	auto params = Child(pp, Tag::Params);
	auto lp = params->Child(0, Tag::LParen);
	const auto& rp = params->Children().back();
	auto items = collect_args(params->Children());
	Formatting sig;

	if ( items.empty() )
		sig = prefix + lp + rp + ret_fmt;
	else
		{
		auto cs = flat_or_fill(prefix, lp, rp, ret_fmt, items, ctx);
		sig = best(cs).Fmt();
		}

	// Format body with indent level based on the lambda's column
	// position, so the Whitesmith block aligns to the next tab stop.
	int lambda_indent = ctx.Col() / INDENT_WIDTH;
	FmtContext body_ctx(lambda_indent, ctx.Col(), ctx.MaxCol() - ctx.Col());
	const auto& body = Children().back();
	auto block = body->FormatWhitesmithBlock(body_ctx);

	auto fmt = sig + block;
	int last_w = fmt.LastLineLen();
	int lines = fmt.CountLines();
	int ovf = fmt.TextOverflow(ctx.Col(), ctx.MaxCol());

	return {{std::move(fmt), last_w, lines, ovf, ctx.Col()}};
	}

// Shared constructor layout: flat or vertical (one per line).
static Candidates FormatConstructor_args(const Formatting& open,
	const Formatting& close, const ArgComments& items,
	const FmtContext& ctx)
	{
	Candidates result;

	if ( ! has_breaks(items) )
		{
		int open_w = open.Size();
		int close_w = close.Size();
		FmtContext args_ctx(ctx.Indent(), ctx.Col() + open_w,
		                    ctx.Width() - open_w - close_w);
		auto flat_args = format_args_flat(items, args_ctx);
		auto flat_fmt = open + flat_args.Fmt() + close;
		Candidate flat_c(std::move(flat_fmt), ctx);
		result.push_back(flat_c);

		if ( flat_c.Ovf() == 0 )
			return result;
		}

	result.push_back(format_args_vertical(open, close, items, ctx));
	return result;
	}

// Constructor: table(...), set(...), vector(...), {1, 2, 3}
// Children: [0]=KEYWORD [1]=LPAREN ... [last]=RPAREN
Candidates ConstructorNode::Format(const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	auto lp = Child(1, Tag::LParen);
	const auto& rp = Children().back();
	auto open = Formatting(kw) + lp;

	auto items = collect_args(Children());
	if ( items.empty() )
		return {Candidate(open + rp, ctx)};

	// Detect trailing comma: N items have N-1 separators, so
	// N commas means the last one is trailing.
	int commas = 0;
	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Comma )
			++commas;

	bool trailing = commas >= static_cast<int>(items.size());
	if ( trailing )
		return {format_args_vertical(open, rp, items, ctx, true)};

	return FormatConstructor_args(open, rp, items, ctx);
	}

// Index literal: [$field=expr, ...]
// Children: [0]=LBRACKET ... [last]=RBRACKET
Candidates IndexLiteralNode::Format(const FmtContext& ctx) const
	{
	auto lb = Child(0, Tag::LBracket);
	const auto& rb = Children().back();

	auto items = collect_args(Children());
	if ( items.empty() )
		return {Candidate(Formatting(lb) + rb, ctx)};

	bool has_trailing_comma =
		FindOptChild(Tag::TrailingComma) != nullptr;
	std::string close_pfx = has_trailing_comma ? ", " : "";

	// When every item has a trailing comment, use vertical indented
	// layout (each item on its own line).  Otherwise use fill, which
	// packs items and wraps after any trailing comment.
	if ( has_breaks(items) )
		{
		bool all_trailing = true;
		for ( size_t i = 0; i < items.size(); ++i )
			{
			auto& it = items[i];
			auto nc = (i + 1 < items.size()) ?
					items[i + 1].comma : nullptr;
			bool has_trail = ! it.comment.empty() ||
						(nc && nc->MustBreakAfter());
			if ( ! has_trail )
				all_trailing = false;
			}

		if ( all_trailing )
			return {format_args_vertical(lb, rb, items,
				ctx, has_trailing_comma)};
		}

	return flat_or_fill("", lb, rb, "", items, ctx, "", close_pfx);
	}

// Boolean chain: operands are direct children (flattened by emitter),
// pack with fill layout breaking at the boolean operator.
Candidates BoolChainNode::Format(const FmtContext& ctx) const
	{
	auto operands = ContentChildren();
	const auto& op = Arg();
	auto sep = " " + op + " ";
	int sep_w = static_cast<int>(sep.size());

	// Try flat.  Only use when every operand fits on one line.
	Formatting flat;
	int flat_w = 0;
	bool any_multiline = false;

	for ( size_t i = 0; i < operands.size(); ++i )
		{
		auto cs = format_expr(*operands[i], ctx.After(flat_w));
		auto bc = best(cs);

		if ( bc.Lines() > 1 )
			any_multiline = true;

		if ( i > 0 )
			{
			flat += sep;
			flat_w += sep_w;
			}

		flat += bc.Fmt();
		flat_w += bc.Width();
		}

	int flat_ovf = ovf(flat_w, ctx);
	if ( flat_ovf == 0 && ! any_multiline )
		return {Candidate(flat, ctx)};

	// Fill: pack operands with " op " separator, wrap at operator.
	FmtContext cont_ctx = ctx.Col() == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(ctx.Col());
	auto pad = line_prefix(cont_ctx.Indent(), cont_ctx.Col());
	int max_col = ctx.MaxCol() - ctx.Trail();

	Formatting text;
	int cur_col = ctx.Col();
	int lines = 1;
	int total_overflow = 0;

	for ( size_t i = 0; i < operands.size(); ++i )
		{
		FmtContext sub(cont_ctx.Indent(), cur_col, max_col - cur_col);
		auto bc = best(format_expr(*operands[i], sub));
		int w = bc.Width();

		if ( i == 0 )
			{
			text += bc.Fmt();
			cur_col += w;
			}

		else
			{
			int need = bc.Lines() > 1 ? max_col + 1 : sep_w + w;

			if ( cur_col + need <= max_col )
				{
				text += sep + bc.Fmt();
				cur_col += need;
				}

			else
				{
				text += " " + op + "\n" + pad;
				cur_col = cont_ctx.Col();
				++lines;

				FmtContext wrap_sub(cont_ctx.Indent(),
						cur_col, max_col - cur_col);

				auto wcs = format_expr(*operands[i], wrap_sub);
				auto wb = best(wcs);
				text += wb.Fmt();
				cur_col += wb.Width();
				total_overflow += wb.Ovf();
				}
			}

		if ( bc.Lines() > 1 )
			{
			lines += bc.Lines() - 1;
			cur_col = text.LastLineLen();
			}

		total_overflow += bc.Ovf();
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {{text, cur_col, lines, total_overflow, ctx.Col()}};
	}

