#include "flat_split.h"
#include "fmt_util.h"

// Format all Expr pieces using the flat context, adjusting the
// context after each piece to reflect the accumulated width.
static void format_flat(FmtSteps& steps, const FmtContext& ctx)
	{
	int used = 0;
	for ( auto& s : steps )
		{
		if ( s.kind == FmtStep::Expr )
			{
			auto c = best(format_expr(*s.node, ctx.After(used)));
			s.text = c.Fmt();
			}
		used += s.text.Size();
		}
	}

// Concatenate all pieces into a single Formatting.
static Formatting concat(const FmtSteps& steps)
	{
	Formatting result;
	for ( const auto& s : steps )
		result += s.text;
	return result;
	}

// Derive the continuation context for a split.
static FmtContext split_ctx(const SplitAt& sp, const FmtContext& ctx)
	{
	switch ( sp.style ) {
	case SplitAt::Indented: return ctx.Indented();
	case SplitAt::SameCol: return ctx.AtCol(ctx.Col());

	case SplitAt::IndentedOrSame:
		if ( ctx.Col() == ctx.IndentCol() )
			return ctx.Indented();
		return ctx.AtCol(ctx.Col());
	}

	return ctx.Indented();	// unreachable
	}

// Build a split candidate: pieces 0..after on the first line(s),
// then a line break, then pieces (after+1).. re-formatted with
// the continuation context.
static Candidate build_split(const FmtSteps& steps, const SplitAt& sp,
                             const FmtContext& ctx)
	{
	FmtContext cont = split_ctx(sp, ctx);
	auto pad = line_prefix(cont.Indent(), cont.Col());

	// First line: pieces up through the split point.
	Formatting first;
	for ( int i = 0; i <= sp.after; ++i )
		first += steps[i].text;

	// Continuation: re-format Expr pieces with new context.
	// Skip leading Sp pieces - the newline+indent replaces them.
	Formatting rest;
	int used = 0;
	bool at_start = true;
	for ( size_t i = sp.after + 1; i < steps.size(); ++i )
		{
		auto& s = steps[i];

		if ( at_start && s.kind == FmtStep::Sp )
			continue;

		at_start = false;

		if ( s.kind == FmtStep::Expr )
			{
			auto c = best(format_expr(*s.node, cont.After(used)));
			rest += c.Fmt();
			used += c.Fmt().Size();
			}
		else
			{
			rest += s.text;
			used += s.text.Size();
			}
		}

	auto split = first + "\n" + pad + rest;
	int last_w = split.LastLineLen();
	int lines = split.CountLines();

	// Match the hand-tuned overflow: first line uses no-trail
	// overflow (trailing content is on the continuation line),
	// continuation uses the continuation context's overflow.
	int first_ovf = ovf_no_trail(first.Size(), ctx);
	int rest_ovf = rest.TextOverflow(cont.Col(), ctx.MaxCol());
	int split_ovf = first_ovf + rest_ovf;

	return {std::move(split), last_w, lines, split_ovf, ctx.Col()};
	}

Candidates flat_or_split(FmtSteps steps, const std::vector<SplitAt>& splits,
                         const FmtContext& ctx)
	{
	// Format all sub-expressions assuming flat layout.
	format_flat(steps, ctx);

	auto flat = concat(steps);
	int lines = flat.CountLines();

	Candidates result;

	if ( lines > 1 )
		{
		int last_w = flat.LastLineLen();
		int ovf = flat.TextOverflow(ctx.Col(), ctx.MaxCol());
		result.push_back({std::move(flat), last_w, lines, ovf,
					ctx.Col()});
		}
	else
		result.push_back(Candidate(std::move(flat), ctx));

	// If flat fits, no need for splits.
	if ( result[0].Ovf() <= 0 )
		return result;

	// Generate a split candidate for each split point.
	bool multiline = lines > 1;
	for ( const auto& sp : splits )
		{
		if ( sp.skip_if_multiline && multiline )
			continue;
		result.push_back(build_split(steps, sp, ctx));
		}

	return result;
	}
