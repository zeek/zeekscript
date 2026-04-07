#include "fmt_util.h"
#include "node.h"

// LAMBDA:          [0]=KW [1]=SP [2]=PARAMS [opt COLON RETURNS] BODY(last)
// LAMBDA-CAPTURES: [0]=KW [1]=SP [2]=CAPTURES [3]=PARAMS [opt ...] BODY(last)

// Prefix for arglist: keyword for plain lambda, keyword[captures]
// for lambda-with-captures.
LayoutItem Layout::ComputeLambdaPrefix(ComputeCtx& /*cctx*/,
                                     const FmtContext& ctx) const
	{
	if ( GetTag() != Tag::LambdaCaptures )
		return Formatting(Child(0, Tag::Keyword));

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

// Return type suffix for lambda: ": type" or empty.
LayoutItem Layout::ComputeLambdaRet(ComputeCtx& /*cctx*/,
                                  const FmtContext& ctx) const
	{
	int pp = (GetTag() == Tag::LambdaCaptures) ? 3 : 2;
	auto after_params = Child(pp + 1);

	if ( after_params->GetTag() != Tag::Colon )
		return Formatting();

	auto returns = Child(pp + 2, Tag::Returns);
	if ( auto rt = returns->FindTypeChild() )
		return Formatting(after_params) + " " +
			best(format_expr(*rt, ctx)).Fmt();

	return Formatting();
	}

// Body block for lambda: uses column-based indent so the
// Whitesmith block aligns to the next tab stop.
LayoutItem Layout::ComputeLambdaBody(ComputeCtx& /*cctx*/,
                                   const FmtContext& ctx) const
	{
	int lambda_indent = ctx.Col() / INDENT_WIDTH;
	FmtContext body_ctx(lambda_indent, ctx.Col(),
			ctx.MaxCol() - ctx.Col());
	return Children().back()->FormatWhitesmithBlock(body_ctx);
	}
