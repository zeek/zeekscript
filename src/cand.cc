#include <algorithm>
#include <cassert>

#include "cand.h"
#include "layout.h"

Candidate::Candidate(Formatting t, const FmtContext& ctx)
	: fmt(std::move(t)), width(fmt.Size()), lines(1), spread(0)
	{
	int avail = ctx.Width() - ctx.Trail();
	int excess = width - avail;
	overflow = excess > 0 ? excess : 0;
	}

int Candidate::ComputeSpread(const std::string& t, int first_col)
	{
	int max_w = 0;
	int min_w = 99999;
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
			line_w = (line_w / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++line_w;
		}

	// Last line (after final \n or entire string).
	max_w = std::max(max_w, line_w);
	min_w = std::min(min_w, line_w);

	return max_w - min_w;
	}

bool Candidate::BetterThan(const Candidate& o) const
	{
	if ( Ovf() != o.Ovf() ) return Ovf() < o.Ovf();
	if ( Lines() != o.Lines() ) return Lines() < o.Lines();
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
