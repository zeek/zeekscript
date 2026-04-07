#include "fmt_util.h"
#include "node.h"

// LAMBDA:          [0]=KW [1]=SP [2]=PARAMS [opt COLON RETURNS] BODY(last)
// LAMBDA-CAPTURES: [0]=KW [1]=SP [2]=CAPTURES [3]=PARAMS [opt ...] BODY(last)

// Signature for lambda: prefix + (params) + optional return type.
LayoutItem Node::ComputeLambdaSig(ComputeCtx& /*cctx*/,
                                  const FmtContext& ctx) const
	{
	// Build prefix: keyword for plain lambda, keyword[captures]
	// for lambda-with-captures.
	int pp;
	Formatting prefix;

	if ( GetTag() == Tag::LambdaCaptures )
		{
		pp = 3;
		auto kw = Child(0, Tag::Keyword);
		auto captures = Child(2, Tag::Captures);
		auto clb = captures->Child(0, Tag::LBracket);
		const auto& crb = captures->Children().back();
		auto cap_items = collect_args(captures->Children());

		if ( cap_items.empty() )
			prefix = Formatting(kw) + clb + crb;
		else
			{
			auto cs = flat_or_fill(kw, clb, crb, "", cap_items, ctx);
			prefix = best(cs).Fmt();
			}
		}
	else
		{
		pp = 2;
		prefix = Formatting(Child(0, Tag::Keyword));
		}

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

	if ( items.empty() )
		return prefix + lp + rp + ret_fmt;

	auto cs = flat_or_fill(prefix, lp, rp, ret_fmt, items, ctx);
	return best(cs).Fmt();
	}

// Body block for lambda: uses column-based indent so the
// Whitesmith block aligns to the next tab stop.
LayoutItem Node::ComputeLambdaBody(ComputeCtx& /*cctx*/,
                                   const FmtContext& ctx) const
	{
	int lambda_indent = ctx.Col() / INDENT_WIDTH;
	FmtContext body_ctx(lambda_indent, ctx.Col(),
			ctx.MaxCol() - ctx.Col());
	return Children().back()->FormatWhitesmithBlock(body_ctx);
	}
