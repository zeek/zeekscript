#include <algorithm>
#include <cassert>
#include <climits>

#include "cand.h"
#include "fmt_context.h"

Candidate::Candidate(Formatting t, int w, int l, int ovf, int first_col)
	: fmt(std::move(t)), width(w), lines(l), overflow(ovf)
	{
	if ( l > 1 )
		{
		auto m = ComputeMetrics(fmt.Str(), first_col);
		spread = m.spread;
		max_line_width = m.max_line_width;
		reluctant = m.reluctant_breaks;
		}
	else
		{
		spread = 0;
		max_line_width = 0;
		reluctant = 0;
		}
	}

Candidate::Candidate(Formatting t, const FmtContext& ctx)
	: fmt(std::move(t)), lines(fmt.CountLines()),
	  spread(0), max_line_width(0), reluctant(0)
	{
	if ( lines > 1 )
		{
		width = fmt.LastLineLen();
		overflow = fmt.TextOverflow(ctx.Col(), ctx.MaxCol());
		auto m = ComputeMetrics(fmt.Str(), ctx.Col());
		spread = m.spread;
		max_line_width = m.max_line_width;
		reluctant = m.reluctant_breaks;
		}
	else
		{
		width = fmt.Size();
		int avail = ctx.Width() - ctx.Trail();
		int excess = width - avail;
		overflow = excess > 0 ? excess : 0;
		}
	}

Candidate::Metrics Candidate::ComputeMetrics(const std::string& t,
                                              int first_col)
	{
	int max_w = 0;
	int min_w = INT_MAX;
	int line_w = first_col;
	int rb = 0;
	char prev = 0;

	for ( char c : t )
		{
		if ( c == '\n' )
			{
			if ( prev == '$' )
				++rb;
			max_w = std::max(max_w, line_w);
			min_w = std::min(min_w, line_w);
			line_w = 0;
			}

		else if ( c == '\t' )
			line_w = next_tab(line_w);
		else
			++line_w;

		prev = c;
		}

	// Last line (after final \n or entire string).
	max_w = std::max(max_w, line_w);
	min_w = std::min(min_w, line_w);

	return {max_w - min_w, max_w, rb};
	}

bool Candidate::BetterThan(const Candidate& o) const
	{
	// Reluctant breaks ($-splits) are checked first so that
	// small overflow reductions don't justify a $-split.
	// Only saving >= 2 lines makes a reluctant break acceptable.
	int rb = ReluctantBreaks(), orb = o.ReluctantBreaks();
	if ( rb != orb )
		{
		int line_diff = Lines() - o.Lines();
		if ( rb > orb && line_diff <= -2 )
			return true;
		if ( orb > rb && -line_diff <= -2 )
			return false;
		return rb < orb;
		}

	if ( Ovf() != o.Ovf() )
		return Ovf() < o.Ovf();

	if ( Lines() != o.Lines() )
		return Lines() < o.Lines();

	if ( MaxLineWidth() != o.MaxLineWidth() )
		return MaxLineWidth() < o.MaxLineWidth();

	return Spread() < o.Spread();
	}

const Candidate& best(const Candidates& cs)
	{
	assert(! cs.empty());

	// Prefer non-reluctant candidates.  Reluctant ($-split)
	// candidates propagate through the beam for width reduction
	// but are not chosen by local best() - they would steal
	// overflow that the outer context handles at a better break.
	const Candidate* result = nullptr;
	const Candidate* reluctant_best = nullptr;

	for ( const auto& c : cs )
		if ( c.ReluctantBreaks() > 0 )
			{
			if ( ! reluctant_best || c.BetterThan(*reluctant_best) )
				reluctant_best = &c;
			}
		else if ( ! result || c.BetterThan(*result) )
			result = &c;

	return result ? *result : *reluctant_best;
	}

const Candidate& best_overall(const Candidates& cs)
	{
	assert(! cs.empty());

	// Like best() but lets reluctant candidates win when they
	// save >= 2 lines (via BetterThan's built-in gate).  Used
	// at statement level where no outer context can help.
	const Candidate* result = nullptr;
	const Candidate* reluctant_best = nullptr;

	for ( const auto& c : cs )
		if ( c.ReluctantBreaks() > 0 )
			{
			if ( ! reluctant_best || c.BetterThan(*reluctant_best) )
				reluctant_best = &c;
			}
		else if ( ! result || c.BetterThan(*result) )
			result = &c;

	if ( ! result )
		return *reluctant_best;
	if ( ! reluctant_best )
		return *result;

	return reluctant_best->BetterThan(*result) ? *reluctant_best : *result;
	}
