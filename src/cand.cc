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
		auto [s, m] = ComputeMetrics(fmt.Str(), first_col);
		spread = s;
		max_line_width = m;
		}
	else
		{
		spread = 0;
		max_line_width = 0;
		}
	}

Candidate::Candidate(Formatting t, const FmtContext& ctx)
	: fmt(std::move(t)), lines(fmt.CountLines()),
	  spread(0), max_line_width(0)
	{
	if ( lines > 1 )
		{
		width = fmt.LastLineLen();
		overflow = fmt.TextOverflow(ctx.Col(), ctx.MaxCol());
		auto [s, m] = ComputeMetrics(fmt.Str(), ctx.Col());
		spread = s;
		max_line_width = m;
		}
	else
		{
		width = fmt.Size();
		int avail = ctx.Width() - ctx.Trail();
		int excess = width - avail;
		overflow = excess > 0 ? excess : 0;
		}
	}

std::pair<int, int> Candidate::ComputeMetrics(const std::string& t,
                                               int first_col)
	{
	int max_w = 0;
	int min_w = INT_MAX;
	int line_w = first_col;

	for ( char c : t )
		{
		if ( c == '\n' )
			{
			max_w = std::max(max_w, line_w);
			min_w = std::min(min_w, line_w);
			line_w = 0;
			}

		else if ( c == '\t' )
			line_w = next_tab(line_w);
		else
			++line_w;
		}

	// Last line (after final \n or entire string).
	max_w = std::max(max_w, line_w);
	min_w = std::min(min_w, line_w);

	return {max_w - min_w, max_w};
	}

bool Candidate::BetterThan(const Candidate& o) const
	{
	if ( Ovf() != o.Ovf() ) return Ovf() < o.Ovf();
	if ( Lines() != o.Lines() ) return Lines() < o.Lines();
	if ( MaxLineWidth() != o.MaxLineWidth() )
		return MaxLineWidth() < o.MaxLineWidth();
	return Spread() < o.Spread();
	}

const Candidate& best(const Candidates& cs)
	{
	assert(! cs.empty());
	auto result = &cs[0];

	for ( size_t i = 1; i < cs.size(); ++i )
		if ( cs[i].BetterThan(*result) )
			result = &cs[i];

	return *result;
	}
