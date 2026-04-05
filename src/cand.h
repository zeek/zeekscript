#pragma once

#include <string>
#include <vector>

#include "formatting.h"

class FmtContext;

// A single formatting candidate: the actual fmt plus metrics that let
// a parent choose among alternatives.
class Candidate {
public:
	// Single-line candidate with overflow computed from context.
	// Accounts for trailing reservation (e.g. comment after stmt).
	// Defined in cand.cc (needs complete FmtContext).
	Candidate(const std::string& t, const FmtContext& ctx);
	Candidate(Formatting t, const FmtContext& ctx);

	// Multi-line candidate.  first_col is the absolute column where the
	// first line starts (needed for balance/spread computation).
	Candidate(Formatting t, int w, int l, int ovf, int first_col = 0)
		: fmt(std::move(t)), width(w), lines(l), overflow(ovf),
		  spread(l > 1 ? ComputeSpread(fmt.Str(), first_col) : 0) {}

	Candidate(Formatting t) : fmt(std::move(t)), width(fmt.Size()),
		  lines(1), overflow(0), spread(0) { }

	const Formatting& Fmt() const { return fmt; }
	int Width() const { return width; }
	int Lines() const { return lines; }
	int Ovf() const { return overflow; }
	int Spread() const { return spread; }

	// Build a new single-line candidate by appending.
	// Overflow is not set; use In() to finalize.
	Candidate Cat(const Candidate& o) const
		{ return Candidate(fmt + o.fmt); }
	Candidate Cat(const NodePtr& n) const
		{ return Candidate(fmt + n); }

	// Return a copy with overflow computed against a context.
	Candidate In(const FmtContext& ctx) const
		{ return Candidate(fmt, ctx); }

	// Is this a clean single-line result?
	bool Fits() const { return lines == 1 && overflow <= 0; }

	// Comparison: fewer overflows wins, then fewer lines, then
	// more balanced (smaller spread).
	bool BetterThan(const Candidate& o) const;

private:
	// Compute spread (max line width - min line width).
	// first_col is the absolute column where the first line starts.
	static int ComputeSpread(const std::string& text, int first_col);

	Formatting fmt;
	int width;	// width of last (or only) line
	int lines;	// number of lines (1 = single line)
	int overflow;	// columns past the allowed width
	int spread;	// max line width - min line width (0 = balanced)
};

using Candidates = std::vector<Candidate>;

// Pick the best candidate from a non-empty vector.
const Candidate& best(const Candidates& cs);
