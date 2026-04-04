#include <algorithm>
#include <cassert>

#include "fmt_internal.h"

// Line prefix: tabs for indent, spaces for remaining offset
std::string LinePrefix(int indent, int col)
	{
	std::string s(indent, '\t');
	int space_col = indent * INDENT_WIDTH;

	if ( col > space_col )
		s.append(col - space_col, ' ');

	return s;
	}

// Pre-comment / pre-marker emission
std::string EmitPreComments(const Node& node, const std::string& pad)
	{
	std::string result;

	for ( const auto& pc : node.PreComments() )
		{
		// Leading '\n' = blank line before this comment.
		size_t start = 0;
		while ( start < pc.size() && pc[start] == '\n' )
			{
			result += "\n";
			++start;
			}

		// The comment text itself.
		size_t end = pc.size();
		while ( end > start && pc[end - 1] == '\n' )
			--end;

		result += pad + pc.substr(start, end - start) + "\n";

		// Trailing '\n' = blank line after this comment.
		for ( size_t j = end; j < pc.size(); ++j )
			result += "\n";
		}

	for ( const auto& pm : node.PreMarkers() )
		if ( pm->GetTag() == Tag::Blank )
			result += "\n";

	return result;
	}

// Candidate comparison
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

const Candidate& Best(const Candidates& cs)
	{
	assert(! cs.empty());
	auto best = &cs[0];

	for ( size_t i = 1; i < cs.size(); ++i )
		if ( cs[i].BetterThan(*best) )
			best = &cs[i];

	return *best;
	}

int Ovf(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width() + ctx.Trail());
	}

int OvfNoTrail(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width());
	}

int FitCol(int align_col, int w, int max_col)
	{
	if ( align_col + w <= max_col - 1 )
		return align_col;
	return max_col - 1 - w;
	}

int CountLines(const std::string& s)
	{
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

int LastLineLen(const std::string& s)
	{
	auto n = s.size();
	auto pos = s.rfind('\n');
	if ( pos != std::string::npos )
		n -= (pos + 1);
	return static_cast<int>(n);
	}

int TextOverflow(const std::string& text, int start_col, int max_col)
	{
	int ovf = 0;
	int pos = 0;
	int line_start_col = start_col;

	for ( size_t j = 0; j < text.size(); ++j )
		if ( text[j] == '\n' )
			{
			int line_w = static_cast<int>(j) - pos + line_start_col;
			if ( line_w > max_col )
				ovf += line_w - max_col;
			pos = static_cast<int>(j) + 1;
			line_start_col = 0;
			}

	// Check the last line too.
	int final_w = static_cast<int>(text.size()) - pos + line_start_col;
	if ( final_w > max_col )
		ovf += final_w - max_col;

	return ovf;
	}

// Like TextOverflow but returns the maximum overflow of any single
// line rather than the sum.  Handles tab indentation correctly
// (each tab = INDENT_WIDTH columns).
int MaxLineOverflow(const std::string& text, int start_col, int max_col)
	{
	int max_ovf = 0;
	int col = start_col;

	for ( char c : text )
		{
		if ( c == '\n' )
			{
			int ovf = std::max(0, col - max_col);
			if ( ovf > max_ovf )
				max_ovf = ovf;
			col = 0;
			}
		else if ( c == '\t' )
			col = (col / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++col;
		}

	int ovf = std::max(0, col - max_col);
	if ( ovf > max_ovf )
		max_ovf = ovf;

	return max_ovf;
	}

// Layout combinator
const LayoutItem SoftSp{LayoutItem::Kind::Sp};

LayoutItem Tok(const Node* n)
	{
	LayoutItem item(n->Text());
	item.SetMustBreak(n->MustBreakAfter());
	return item;
	}

static constexpr int BEAM_WIDTH = 4;

struct Partial {
	std::string text;
	int col;      // current column (end of last line)
	int lines;
	int overflow;
	bool must_break;  // preceding token forces next Sp to break
};

using Partials = std::vector<Partial>;

static Partials layout_one_item(const LayoutItem& item, Partials& beam,
				const FmtContext& ctx, int trail)
	{
	Partials next;

	auto ovf_at = [&](int c)
		{ return std::max(0, c - ctx.MaxCol()); };

	for ( auto& p : beam )
		{
		switch ( item.kind ) {
		case LayoutItem::Kind::Lit:
			{
			Partial np = p;
			np.text += item.Text();
			np.col += static_cast<int>(item.Text().size());
			np.overflow += ovf_at(np.col);
			np.must_break = item.MustBreak();
			next.push_back(std::move(np));
			break;
			}

		case LayoutItem::Kind::Fmt:
			{
			int avail = ctx.MaxCol() - p.col;
			FmtContext sub(ctx.Indent(), p.col, avail, trail);
			auto cs = FormatExpr(*item.LI_Node(), sub);
			for ( const auto& c : cs )
				{
				Partial np = p;
				np.text += c.Text();
				np.overflow += c.Ovf();
				np.must_break = false;
				if ( c.Lines() > 1 )
					{
					np.lines += c.Lines() - 1;
					np.col = c.Width();
					}
				else
					np.col += c.Width();
				next.push_back(std::move(np));
				}
			break;
			}

		case LayoutItem::Kind::Sp:
			{
			// Option 1: space (skip if preceding
			// token forces a break).
			if ( ! p.must_break )
				{
				Partial sp = p;
				sp.text += " ";
				++sp.col;
				sp.must_break = false;
				next.push_back(std::move(sp));
				}

			// Option 2: break + indent.
			FmtContext brk = ctx.Indented();
			auto pad = "\n" + LinePrefix(brk.Indent(),
							brk.IndentCol());
			Partial bp = p;
			bp.text += pad;
			bp.col = brk.IndentCol();
			++bp.lines;
			bp.must_break = false;
			next.push_back(std::move(bp));
			break;
			}
		}
		}

	// Prune to best BEAM_WIDTH using same priority as
	// Candidate::BetterThan: overflow > lines > spread.
	if ( static_cast<int>(next.size()) > BEAM_WIDTH )
		{
		std::sort(next.begin(), next.end(),
			[](const Partial& a, const Partial& b)
				{
				if ( a.overflow != b.overflow )
					return a.overflow < b.overflow;
				return a.lines < b.lines;
				});
		next.resize(BEAM_WIDTH);
		}

	return next;
	}

// Trailing literal width after a Fmt node is automatically reserved
// so the formatted expression accounts for what follows it.
Candidates BuildLayout(LayoutItems items_init, const FmtContext& ctx)
	{
	std::vector<LayoutItem> items(items_init);

	// Compute trailing literal width after position i, assuming
	// SoftSp items resolve to a single space.  If the trailing
	// items extend to the end of the layout, include the outer
	// context's trail reservation (e.g. a ";" appended by the
	// caller after the whole layout).
	auto trail_after = [&](size_t i) -> int
		{
		int w = 0;
		for ( size_t j = i + 1; j < items.size(); ++j )
			{
			if ( items[j].kind == LayoutItem::Kind::Lit )
				w += static_cast<int>(items[j].Text().size());
			else if ( items[j].kind == LayoutItem::Kind::Sp )
				++w;  // space in the flat case
			else
				break;
			}
		w += ctx.Trail();
		return w;
		};

	Partials beam = {{"", ctx.Col(), 1, 0, false}};

	for ( size_t i = 0; i < items.size(); ++i )
		beam = layout_one_item(items[i], beam, ctx, trail_after(i));

	// Convert partials to Candidates.  Width is relative to the
	// start column so callers can combine it with other text.
	Candidates result;
	for ( auto& p : beam )
		{
		int w = (p.lines == 1) ? static_cast<int>(p.text.size()) : p.col;
		result.push_back({std::move(p.text), w, p.lines,
		                  p.overflow, ctx.Col()});
		}

	return result;
	}
