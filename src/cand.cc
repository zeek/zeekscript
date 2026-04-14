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

void Candidate::AppendBody(const Formatting& body)
	{
	fmt += body;
	int nl = body.CountLines() - 1;
	if ( nl > 0 )
		{
		lines += nl;
		width = fmt.LastLineLen();
		}
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
		// Also let reluctant win with 1 line savings
		// when it has strictly less overflow.
		if ( rb > orb && line_diff < 0 && Ovf() < o.Ovf() )
			return true;
		if ( orb > rb && -line_diff < 0 && o.Ovf() < Ovf() )
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

// Partition candidates into best non-reluctant and best reluctant.
struct BestPair {
	const Candidate* normal;
	const Candidate* reluctant;
};

static BestPair partition_best(const Candidates& cs)
	{
	const Candidate* normal = nullptr;
	const Candidate* reluctant = nullptr;

	for ( const auto& c : cs )
		if ( c.ReluctantBreaks() > 0 )
			{
			if ( ! reluctant || c.BetterThan(*reluctant) )
				reluctant = &c;
			}
		else if ( ! normal || c.BetterThan(*normal) )
			normal = &c;

	return {normal, reluctant};
	}

// Prefer non-reluctant candidates.  Reluctant ($-split) candidates
// propagate through the beam for width reduction but are not chosen
// by local best() - they would steal overflow that the outer context
// handles at a better break.  Exception: when the best non-reluctant
// significantly overflows and a reluctant candidate eliminates it,
// the reluctant wins - the outer context cannot help with overflow
// baked into a sub-expression.
const Candidate& best(const Candidates& cs)
	{
	assert(! cs.empty());
	auto [normal, reluctant] = partition_best(cs);

	if ( ! normal )
		return *reluctant;

	if ( reluctant && normal->Ovf() > 2 && reluctant->Ovf() <= 0 )
		return *reluctant;

	return *normal;
	}

// Like best() but lets reluctant candidates win when they save >= 2
// lines (via BetterThan's built-in gate).  Used at statement level
// where no outer context can help.
const Candidate& best_overall(const Candidates& cs)
	{
	assert(! cs.empty());
	auto [normal, reluctant] = partition_best(cs);

	if ( ! normal )
		return *reluctant;
	if ( ! reluctant )
		return *normal;

	return reluctant->BetterThan(*normal) ? *reluctant : *normal;
	}
