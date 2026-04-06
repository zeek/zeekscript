#include <algorithm>
#include <cassert>

#include "fmt_util.h"

// Layout combinator

const LayoutItem soft_sp{LayoutItem::Kind::Sp};

LayoutItem tok(const NodePtr& n)
	{
	LayoutItem item{Formatting(n)};
	item.SetMustBreak(n->MustBreakAfter());
	return item;
	}

static constexpr int BEAM_WIDTH = 4;

struct Partial {
	Formatting fmt;
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
			np.fmt += item.Fmt();
			np.col += item.Fmt().Size();
			np.overflow += ovf_at(np.col);
			np.must_break = item.MustBreak();
			next.push_back(std::move(np));
			break;
			}

		case LayoutItem::Kind::Fmt:
			{
			int avail = ctx.MaxCol() - p.col;
			FmtContext sub(ctx.Indent(), p.col, avail, trail);
			auto cs = format_expr(*item.LI_Node(), sub);
			for ( const auto& c : cs )
				{
				Partial np = p;
				np.fmt += c.Fmt();
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

		case LayoutItem::Kind::Tok:
			assert(false);  // resolved before reaching here
			break;

		case LayoutItem::Kind::Sp:
			{
			// Option 1: space (skip if preceding
			// token forces a break).
			if ( ! p.must_break )
				{
				Partial sp = p;
				sp.fmt += " ";
				++sp.col;
				sp.must_break = false;
				next.push_back(std::move(sp));
				}

			// Option 2: break + indent.
			FmtContext brk = ctx.Indented();
			auto pad = "\n" + line_prefix(brk.Indent(),
							brk.IndentCol());
			Partial bp = p;
			bp.fmt += pad;
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
Candidates build_layout(LayoutItems items, const FmtContext& ctx)
	{
	// Compute trailing literal width after position i, assuming
	// soft_sp items resolve to a single space.  If the trailing
	// items extend to the end of the layout, include the outer
	// context's trail reservation (e.g. a ";" appended by the
	// caller after the whole layout).
	auto trail_after = [&](size_t i) -> int
		{
		int w = 0;
		for ( size_t j = i + 1; j < items.size(); ++j )
			{
			if ( items[j].kind == LayoutItem::Kind::Lit )
				w += items[j].Fmt().Size();
			else if ( items[j].kind == LayoutItem::Kind::Sp )
				++w;  // space in the flat case
			else
				break;
			}
		w += ctx.Trail();
		return w;
		};

	Partials beam = {{Formatting(), ctx.Col(), 1, 0, false}};

	for ( size_t i = 0; i < items.size(); ++i )
		beam = layout_one_item(items[i], beam, ctx, trail_after(i));

	// Convert partials to Candidates.  Width is relative to the
	// start column so callers can combine it with other text.
	Candidates result;
	for ( auto& p : beam )
		{
		int w = (p.lines == 1) ? p.fmt.Size() : p.col;
		result.push_back({std::move(p.fmt), w, p.lines,
		                  p.overflow, ctx.Col()});
		}

	return result;
	}

Candidates Node::BuildLayout(LayoutItems items, const FmtContext& ctx) const
	{
	for ( auto& item : items )
		if ( item.kind == LayoutItem::Kind::Tok )
			{
			auto c = Child(item.ChildIdx());
			if ( item.SubChildIdx() >= 0 )
				c = c->Child(item.SubChildIdx());
			item = tok(c);
			}

	return build_layout(items, ctx);
	}
