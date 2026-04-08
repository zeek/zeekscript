#include "flat_split.h"
#include "fmt_util.h"

// Format all Expr pieces using the flat context, adjusting the
// context after each piece to reflect the accumulated width.
// When force_flat is true, sub-expressions are formatted at the
// base column (ctx) rather than the accumulated position, so they
// don't try to wrap internally - overflow is detected at this level.
static void format_flat(FmtSteps& steps, const FmtContext& ctx,
                        bool force_flat)
	{
	int used = 0;
	for ( auto& s : steps )
		{
		if ( s.kind == FmtStep::SExpr )
			{
			auto sub_ctx = force_flat ? ctx : ctx.After(used);
			s.text = best(format_expr(*s.node, sub_ctx)).Fmt();
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
static FmtContext split_ctx(const SplitAt& sp, const FmtSteps& steps,
                            const FmtContext& ctx)
	{
	switch ( sp.style ) {
	case SplitAt::Indented: return ctx.Indented();
	case SplitAt::SameCol: return ctx.AtCol(ctx.Col());

	case SplitAt::IndentedOrSame:
		if ( ctx.Col() == ctx.IndentCol() )
			return ctx.Indented();
		return ctx.AtCol(ctx.Col());

	case SplitAt::AlignWith:
		{
		int col = ctx.Col();
		for ( int i = 0; i < sp.align_piece; ++i )
			col += steps[i].text.Size();
		return ctx.AtCol(col);
		}
	}

	return ctx.Indented();	// unreachable
	}

// Format continuation pieces (after the split point) with the given
// context.  Expr pieces are re-formatted; Sp pieces at the start
// are dropped (newline+indent replaces them).
static Formatting format_rest(const FmtSteps& steps, int after,
                              const FmtContext& cont)
	{
	Formatting rest;
	int used = 0;
	bool at_start = true;

	for ( size_t i = after + 1; i < steps.size(); ++i )
		{
		auto& s = steps[i];

		if ( at_start && s.kind == FmtStep::SSp )
			continue;

		at_start = false;

		if ( s.kind == FmtStep::SExpr )
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

	return rest;
	}

// Assemble a split candidate from first-line text and continuation.
static Candidate assemble_split(const Formatting& first, const Formatting& rest,
                                const std::string& pad, const FmtContext& ctx)
	{
	auto split = first + "\n" + pad + rest;
	int last_w = split.LastLineLen();
	int lines = split.CountLines();
	int ovf = split.TextOverflow(ctx.Col(), ctx.MaxCol() - ctx.Trail());
	return {std::move(split), last_w, lines, ovf, ctx.Col()};
	}

// Re-format expr piece i with reduced width to account for trailing
// first-line pieces (e.g. " in" after an INDEX-LITERAL).  For each
// candidate, try both a split form and a flat form (all pieces with
// the wrapped expr, no split).
static void try_narrowed_expr(const FmtSteps& steps, int i, int used,
                              const SplitAt& sp, const FmtContext& ctx,
                              const Formatting& rest, const std::string& pad,
                              Candidate& result)
	{
	int first_trail = 0;
	for ( int j = i + 1; j <= sp.after; ++j )
		first_trail += steps[j].text.Size();

	auto& s_i = steps[i];
	int first_line_end = ctx.Col() + used + s_i.text.Size() + first_trail;
	if ( first_line_end <= ctx.MaxCol() )
		return;

	int sub_col = ctx.Col() + used;
	int sub_w = ctx.MaxCol() - sub_col - first_trail;
	if ( sub_w <= 0 )
		return;

	FmtContext sub(ctx.Indent(), sub_col, sub_w);

	auto cs = format_expr(*s_i.node, sub);
	for ( const auto& c : cs )
		{
		Formatting alt_first;
		for ( int j = 0; j < i; ++j )
			alt_first += steps[j].text;
		alt_first += c.Fmt();
		for ( int j = i + 1; j <= sp.after; ++j )
			alt_first += steps[j].text;

		auto alt = assemble_split(alt_first, rest, pad, ctx);
		if ( alt.BetterThan(result) )
			result = std::move(alt);

		// Also try a flat form with the wrapped expr.
		// Avoids a split when the remaining pieces fit
		// on the last line of the wrapped expr.
		Formatting flat;
		for ( size_t j = 0; j < steps.size(); ++j )
			{
			auto k = static_cast<int>(j);
			flat += (k == i) ? c.Fmt() : steps[j].text;
			}

		int fl = flat.CountLines();
		int fw = flat.LastLineLen();
		int fo = flat.TextOverflow(ctx.Col(),
						ctx.MaxCol() - ctx.Trail());
		Candidate fc(std::move(flat), fw, fl, fo, ctx.Col());
		if ( fc.BetterThan(result) )
			result = std::move(fc);
		}
	}

// Build split candidate(s): pieces 0..after on the first line(s),
// then a line break, then pieces (after+1).. re-formatted with
// the continuation context.
//
// When a first-line expr piece has multiple candidates (e.g. a
// balanced vs greedy fill), try each variant and return the best
// overall split so that local formatting choices don't prevent
// good global layout.
static Candidate build_split(const FmtSteps& steps, const SplitAt& sp,
                             const FmtContext& ctx)
	{
	FmtContext cont = split_ctx(sp, steps, ctx);
	auto pad = line_prefix(cont.Indent(), cont.Col());
	auto rest = format_rest(steps, sp.after, cont);

	// Default: use the pre-formatted (best()) text for line 1.
	Formatting first;
	for ( int i = 0; i <= sp.after; ++i )
		first += steps[i].text;

	Candidate result = assemble_split(first, rest, pad, ctx);

	// Try alternate candidates for first-line expr pieces.
	// Re-format each expr piece and try each candidate,
	// keeping the split with the best overall metrics.
	int used = 0;
	for ( int i = 0; i <= sp.after; ++i )
		{
		auto& s = steps[i];
		if ( s.kind != FmtStep::SExpr || ! s.node )
			{
			used += s.text.Size();
			continue;
			}

		auto cs = format_expr(*s.node, ctx.After(used));
		if ( cs.size() <= 1 )
			{
			used += s.text.Size();
			continue;
			}

		for ( const auto& c : cs )
			{
			Formatting alt_first;
			for ( int j = 0; j < i; ++j )
				alt_first += steps[j].text;
			alt_first += c.Fmt();
			for ( int j = i + 1; j <= sp.after; ++j )
				alt_first += steps[j].text;

			auto alt = assemble_split(alt_first, rest, pad, ctx);
			if ( alt.BetterThan(result) )
				result = std::move(alt);
			}

		used += s.text.Size();
		}

	// If the split still overflows significantly, try re-formatting
	// expr pieces with reduced width.  Small overflows (1-2 columns)
	// are tolerated to avoid ugly sub-expression splits.
	if ( result.Ovf() > 2 )
		{
		used = 0;
		for ( int i = 0; i <= sp.after; ++i )
			{
			auto& s = steps[i];
			if ( s.kind == FmtStep::SExpr && s.node )
				try_narrowed_expr(steps, i, used, sp, ctx,
							rest, pad, result);
			used += s.text.Size();
			}
		}

	return result;
	}

// Try alternate candidates for each expr piece to see if a different
// choice produces a better overall flat form.
static void try_flat_alternates(const FmtSteps& steps, Candidates& result,
                                const FmtContext& ctx, bool force_flat)
	{
	for ( size_t i = 0; i < steps.size(); ++i )
		{
		auto& s = steps[i];
		if ( s.kind != FmtStep::SExpr || ! s.node )
			continue;

		int used = 0;
		for ( size_t j = 0; j < i; ++j )
			used += steps[j].text.Size();

		auto sub_ctx = force_flat ? ctx : ctx.After(used);
		auto cs = format_expr(*s.node, sub_ctx);
		if ( cs.size() <= 1 )
			continue;

		for ( const auto& c : cs )
			{
			if ( c.Fmt().Str() == s.text.Str() )
				continue;

			Formatting flat;
			for ( size_t j = 0; j < steps.size(); ++j )
				flat += (j == i) ? c.Fmt() : steps[j].text;

			int lines = flat.CountLines();
			int last_w = flat.LastLineLen();
			int fovf = flat.TextOverflow(ctx.Col(),
						ctx.MaxCol() - ctx.Trail());

			Candidate alt = lines > 1 ?
					Candidate(std::move(flat), last_w,
						lines, fovf, ctx.Col()) :
					Candidate(std::move(flat), ctx);

			if ( alt.BetterThan(result[0]) )
				result[0] = std::move(alt);
			}
		}
	}

Candidates flat_or_split(FmtSteps steps, const std::vector<SplitAt>& splits,
                         const FmtContext& ctx, bool force_flat)
	{
	// Format all sub-expressions assuming flat layout.
	format_flat(steps, ctx, force_flat);

	auto flat = concat(steps);
	int lines = flat.CountLines();

	Candidates result;

	if ( lines > 1 )
		{
		int last_w = flat.LastLineLen();
		int fovf = flat.TextOverflow(ctx.Col(),
					ctx.MaxCol() - ctx.Trail());
		result.push_back({std::move(flat), last_w, lines, fovf,
					ctx.Col()});
		}
	else
		result.push_back(Candidate(std::move(flat), ctx));

	// If flat fits, no need for splits.
	if ( result[0].Ovf() <= 0 )
		return result;

	// A different sub-expression candidate might produce a
	// flat form that fits or has less overflow.
	try_flat_alternates(steps, result, ctx, force_flat);
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
