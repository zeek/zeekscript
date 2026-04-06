#include <algorithm>
#include <cassert>

#include "fmt_util.h"

// Layout combinator

const LayoutItem soft_sp{LayoutItem::Kind::Sp};
const LayoutItem indent_up{LayoutItem::Kind::IndentUp};
const LayoutItem indent_down{LayoutItem::Kind::IndentDown};

LayoutItem tok(const NodePtr& n)
	{
	LayoutItem item{Formatting(n)};
	item.SetMustBreak(n->MustBreakAfter());
	return item;
	}

LayoutItem expr(unsigned child_index)
	{
	return {LayoutItem::Kind::ExprIdx, child_index};
	}

LayoutItem last()
	{
	return {LayoutItem::Kind::LastTok};
	}

LayoutItem arg(unsigned arg_index)
	{
	return {LayoutItem::Kind::ArgIdx, arg_index};
	}

LayoutItem arglist(unsigned child_index)
	{
	return {LayoutItem::Kind::ArgList, child_index};
	}

LayoutItem arglist(unsigned child_index, Formatting suffix)
	{
	return {LayoutItem::Kind::ArgList, child_index, std::move(suffix)};
	}

static constexpr int BEAM_WIDTH = 4;

struct Partial {
	Formatting fmt;
	int col;      // current column (end of last line)
	int indent;   // current indent level
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
			const auto& f = item.Fmt();
			np.fmt += f;
			int nl = f.CountLines() - 1;
			if ( nl > 0 )
				{
				np.lines += nl;
				np.col = f.LastLineLen();
				np.overflow += f.MaxLineOverflow(
					p.col, ctx.MaxCol());
				}
			else
				{
				np.col += f.Size();
				np.overflow += ovf_at(np.col);
				}
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

		case LayoutItem::Kind::ArgList:
			{
			auto child = item.LI_Node();
			auto open = Formatting(child->Children().front());
			auto close = Formatting(child->Children().back());
			auto items = collect_args(child->Children());
			auto& suffix = item.Fmt();

			if ( items.empty() )
				{
				Partial np = p;
				np.fmt += open + close + suffix;
				np.col += open.Size() + close.Size()
						+ suffix.Size();
				np.overflow += ovf_at(np.col);
				np.must_break = false;
				next.push_back(std::move(np));
				break;
				}

			int avail = ctx.MaxCol() - p.col;
			FmtContext sub(ctx.Indent(), p.col, avail, trail);
			auto cs = flat_or_fill(Formatting(), open, close,
					suffix, items, sub,
					child->TrailingComment());

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
		case LayoutItem::Kind::ExprIdx:
		case LayoutItem::Kind::LastTok:
		case LayoutItem::Kind::ArgIdx:
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
			int brk_indent = p.indent + 1;
			int brk_col = brk_indent * INDENT_WIDTH;
			auto pad = "\n" + line_prefix(brk_indent,
							brk_col);
			Partial bp = p;
			bp.fmt += pad;
			bp.col = brk_col;
			++bp.lines;
			bp.must_break = false;
			next.push_back(std::move(bp));
			break;
			}

		case LayoutItem::Kind::IndentUp:
			{
			Partial np = p;
			++np.indent;
			next.push_back(std::move(np));
			break;
			}

		case LayoutItem::Kind::IndentDown:
			{
			Partial np = p;

			// Emit pre-comments for the following token at the
			// current (inner) indent.
			if ( item.LI_Node() )
				{
				int inner_col = np.indent * INDENT_WIDTH;
				auto inner_pad = line_prefix(np.indent, inner_col);
				np.fmt += item.LI_Node()->EmitPreComments(inner_pad);
				}

			--np.indent;
			int new_col = np.indent * INDENT_WIDTH;
			auto pad = line_prefix(np.indent, new_col);
			np.fmt += pad;
			np.col = new_col;
			next.push_back(std::move(np));
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
			auto k = items[j].kind;
			if ( k == LayoutItem::Kind::Lit )
				w += items[j].Fmt().Size();
			else if ( k == LayoutItem::Kind::Sp )
				++w;  // space in the flat case
			else if ( k == LayoutItem::Kind::IndentUp ||
				  k == LayoutItem::Kind::IndentDown )
				continue;
			else
				break;
			}
		w += ctx.Trail();
		return w;
		};

	Partials beam = {{Formatting(), ctx.Col(), ctx.Indent(), 1, 0, false}};

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
	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& item = items[i];

		if ( item.kind == LayoutItem::Kind::Tok )
			{
			auto c = Child(item.ChildIdx());
			if ( item.SubChildIdx() >= 0 )
				c = c->Child(item.SubChildIdx());
			item = tok(c);
			}
		else if ( item.kind == LayoutItem::Kind::ExprIdx )
			item = LayoutItem(Child(item.ChildIdx()));
		else if ( item.kind == LayoutItem::Kind::LastTok )
			item = tok(Children().back());
		else if ( item.kind == LayoutItem::Kind::ArgIdx )
			item = LayoutItem(Arg(item.ChildIdx()));
		else if ( item.kind == LayoutItem::Kind::ArgList )
			item = LayoutItem(LayoutItem::Kind::ArgList,
					Child(item.ChildIdx()),
					item.Fmt());
		else if ( item.kind == LayoutItem::Kind::IndentDown )
			{
			// Peek ahead to find the next token and
			// attach its node for pre-comment emission.
			for ( size_t j = i + 1; j < items.size(); ++j )
				{
				auto k = items[j].kind;
				if ( k == LayoutItem::Kind::Tok )
					{
					auto& i_j = items[j];
					auto c = Child(i_j.ChildIdx());
					if ( i_j.SubChildIdx() >= 0 )
						c = c->Child(i_j.SubChildIdx());
					item = LayoutItem(LayoutItem::Kind::IndentDown,
						c, Formatting());
					break;
					}
				if ( k == LayoutItem::Kind::LastTok )
					{
					item = LayoutItem(
						LayoutItem::Kind::IndentDown,
						Children().back(),
						Formatting());
					break;
					}
				}
			}
		}

	return build_layout(items, ctx);
	}
