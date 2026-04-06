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
