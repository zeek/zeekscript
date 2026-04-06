#include <algorithm>
#include <cassert>

#include "flat_split.h"
#include "fmt_util.h"

// Layout combinator

const LayoutItem soft_sp{Sp};
const LayoutItem hard_break{HardBreak};
const LayoutItem indent_up{IndentUp};
const LayoutItem indent_down{IndentDown};

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
		case Lit:
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

		case FmtExpr:
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

		case ArgList:
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

			int fl = item.Flags();
			bool has_tc = (fl & AL_TrailingCommaVertical) &&
				child->FindOptChild(Tag::TrailingComma);

			int avail = ctx.MaxCol() - p.col;
			FmtContext sub(ctx.Indent(), p.col, avail, trail);

			// All-comments or trailing comma: force vertical.
			bool force_vert = has_tc;
			if ( ! force_vert &&
			     (fl & AL_AllCommentsVertical) &&
			     has_breaks(items) )
				{
				force_vert = true;
				for ( size_t j = 0; j < items.size(); ++j )
					{
					auto& it = items[j];
					auto nc = (j + 1 < items.size()) ?
						items[j + 1].comma : nullptr;
					if ( it.comment.empty() &&
					     ! (nc && nc->MustBreakAfter()) )
						{
						force_vert = false;
						break;
						}
					}
				}

			if ( force_vert )
				{
				auto c = format_args_vertical(
					open, close, items, sub, has_tc);
				Partial np = p;
				np.fmt += c.Fmt();
				np.overflow += c.Ovf();
				np.must_break = false;
				np.lines += c.Lines() - 1;
				np.col = c.Width();
				next.push_back(std::move(np));
				break;
				}

			Candidates cs;
			if ( fl & AL_FlatOrVertical )
				{
				if ( ! has_breaks(items) )
					{
					int open_w = open.Size();
					int close_w = close.Size();
					FmtContext ac(sub.Indent(),
						p.col + open_w,
						sub.Width() - open_w - close_w);
					auto flat = open +
						format_args_flat(items, ac).Fmt()
						+ close + suffix;
					Candidate fc(std::move(flat), sub);
					cs.push_back(fc);
					}
				if ( cs.empty() || cs.back().Ovf() > 0 )
					cs.push_back(format_args_vertical(
						open, close, items, sub));
				}
			else
				{
				std::string close_pfx;
				if ( (fl & AL_TrailingCommaFill) &&
				     child->FindOptChild(Tag::TrailingComma) )
					close_pfx = ", ";

				cs = flat_or_fill(Formatting(), open, close,
					suffix, items, sub,
					child->TrailingComment(), close_pfx);

				if ( (fl & AL_VerticalUpgrade) &&
				     items.size() >= 3 && cs.size() > 1 &&
				     cs.back().Lines() ==
				       static_cast<int>(items.size()) )
					{
					cs.pop_back();
					cs.push_back(format_args_vertical(
						open, close, items, sub));
					}
				}

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

		case FlatSplit:
			{
			int avail = ctx.MaxCol() - p.col;
			FmtContext sub(ctx.Indent(), p.col, avail, trail);
			auto cs = flat_or_split(item.Steps(), item.Splits(),
						sub, item.ForceFlatSubs());
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

		case Tok: case ExprIdx: case LastTok: case ArgIdx:
		case StmtBody: case FillList: case Computed:
			assert(false);  // resolved before reaching here
			break;

		case Sp:
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
			auto pad = "\n" + line_prefix(brk_indent, brk_col);
			Partial bp = p;
			bp.fmt += pad;
			bp.col = brk_col;
			++bp.lines;
			bp.must_break = false;
			next.push_back(std::move(bp));
			break;
			}

		case HardBreak:
			{
			int brk_col = p.indent * INDENT_WIDTH;
			auto pad = "\n" + line_prefix(p.indent, brk_col);
			Partial np = p;
			np.fmt += pad;
			np.col = brk_col;
			++np.lines;
			np.must_break = false;
			next.push_back(std::move(np));
			break;
			}

		case IndentUp:
			{
			Partial np = p;
			++np.indent;
			next.push_back(std::move(np));
			break;
			}

		case IndentDown:
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
			if ( k == Lit )
				w += items[j].Fmt().Size();
			else if ( k == Sp )
				++w;  // space in the flat case
			else if ( k == IndentUp || k == IndentDown ||
				  k == HardBreak )
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
	ComputeCtx cctx;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& item = items[i];

		if ( item.kind == Computed )
			{
			item = (this->*item.CompFn())(cctx, ctx);
			continue;
			}

		if ( item.kind == Tok )
			{
			auto c = Child(item.ChildIdx());
			if ( item.SubChildIdx() >= 0 )
				c = c->Child(item.SubChildIdx());
			item = tok(c);
			}
		else if ( item.kind == ExprIdx )
			item = LayoutItem(Child(item.ChildIdx()));
		else if ( item.kind == LastTok )
			item = tok(Children().back());
		else if ( item.kind == ArgIdx )
			item = LayoutItem(Arg(item.ChildIdx()));
		else if ( item.kind == ArgList )
			{
			Formatting suffix = item.Fmt();
			int flags = item.Flags();
			if ( item.CompFn() )
				suffix = (this->*item.CompFn())(cctx, ctx).Fmt();
			item = LayoutItem(ArgList, Child(item.ChildIdx()),
					std::move(suffix));
			if ( flags )
				{
				Formatting s = item.Fmt();
				item = LayoutItem(ArgList, item.LI_Node(),
					std::move(s), flags);
				}
			}
		else if ( item.kind == FlatSplit )
			{
			// Resolve deferred child references in steps.
			auto steps = item.Steps();
			for ( auto& s : steps )
				{
				if ( s.kind == FmtStep::SExprIdx )
					s = FmtStep::E(Child(s.child_idx));
				else if ( s.kind == FmtStep::STokIdx )
					s = FmtStep::L(Child(s.child_idx));
				}
			item = LayoutItem(std::move(steps),
				item.Splits(), item.ForceFlatSubs());
			}
		else if ( item.kind == FillList )
			{
			auto prefix = Formatting(Children().front()) + " ";
			auto args = collect_args(Children());
			auto cs = flat_or_fill(prefix, "", "", "", args, ctx);
			item = LayoutItem(best(cs).Fmt());
			}
		else if ( item.kind == StmtBody )
			{
			// Collect children and format as stmt list.
			const Node& src = (item.ChildIdx() >= 0)
				? *Child(item.ChildIdx()) : *this;
			int fl = item.Flags();

			NodeVec body;
			if ( fl & SB_AllChildren )
				body = src.Children();
			else
				for ( const auto& c : src.Children() )
					if ( ! c->IsToken() )
						body.push_back(c);

			auto sub = ctx.Indented();
			auto text = format_stmt_list(body, sub,
				(fl & SB_SkipBlanks) != 0);

			if ( (fl & SB_StripNewline) &&
			     ! text.Empty() && text.Back() == '\n' )
				text.PopBack();

			item = LayoutItem(Formatting("\n" + text));
			}
		else if ( item.kind == IndentDown )
			{
			// Peek ahead to find the next token and
			// attach its node for pre-comment emission.
			for ( size_t j = i + 1; j < items.size(); ++j )
				{
				auto k = items[j].kind;
				if ( k == Tok )
					{
					auto& i_j = items[j];
					auto c = Child(i_j.ChildIdx());
					if ( i_j.SubChildIdx() >= 0 )
						c = c->Child(i_j.SubChildIdx());
					item = LayoutItem(IndentDown, c,
						Formatting());
					break;
					}
				if ( k == LastTok )
					{
					item = LayoutItem(IndentDown,
						Children().back(),
						Formatting());
					break;
					}
				}
			}
		}

	return build_layout(items, ctx);
	}
