// Layout core: tree construction, Format() entry point, beam search
// engine, and the tag-to-layout table.  Compute/format helpers that
// the table references live in layout_util.cc.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

#include "flat_split.h"
#include "fmt_util.h"

// Line prefix: tabs for indent, spaces for remaining offset
std::string line_prefix(int indent, int col)
	{
	std::string s(indent, '\t');
	int space_col = indent * INDENT_WIDTH;

	if ( col > space_col )
		s.append(col - space_col, ' ');

	return s;
	}

// ---- Core Layout methods ------------------------------------------------

Candidates Layout::Format(const FmtContext& ctx) const
	{
	if ( ! layout.empty() )
		return BuildLayout(layout, ctx);

	auto fallback = std::string("/* ") + TagToString(tag) + " */";
	return {Candidate(fallback, ctx)};
	}

const std::string& Layout::Arg(size_t i) const
	{
	static const std::string empty;
	return i < args.size() ? args[i] : empty;
	}

const LayoutPtr null_node;

const LayoutPtr& Layout::Child(size_t i, Tag t) const
	{
	const auto& c = children[i];
	if ( c->GetTag() != t )
		throw std::runtime_error(std::string("internal error: ") +
			TagToString(tag) + " child " + std::to_string(i) +
			" is " + TagToString(c->GetTag()) + ", expected " +
			TagToString(t));
	return c;
	}

const LayoutPtr& Layout::FindOptChild(Tag t) const
	{
	for ( const auto& c : children )
		if ( c->GetTag() == t )
			return c;
	return null_node;
	}

const LayoutPtr& Layout::FindChild(Tag t) const
	{
	const auto& n = FindOptChild(t);
	if ( ! n )
		throw std::runtime_error(std::string("internal error: ") +
					TagToString(tag) + " has no " +
					TagToString(t) + " child");
	return n;
	}

const LayoutPtr& Layout::FindChild(Tag t, const LayoutPtr& after) const
	{
	bool past = false;
	for ( const auto& c : children )
		{
		if ( c == after )
			past = true;
		else if ( past && c->GetTag() == t )
			return c;
		}

	return null_node;
	}

LayoutVec Layout::ContentChildren() const
	{
	LayoutVec result;
	for ( const auto& c : children )
		if ( ! c->IsToken() && ! c->IsMarker() )
			result.push_back(c);

	return result;
	}

LayoutVec Layout::ContentChildren(const char* name, int n) const
	{
	auto result = ContentChildren();
	if ( static_cast<int>(result.size()) < n )
		throw FormatError(name + std::string(" node needs ") +
					std::to_string(n) + " children");

	return result;
	}

static const std::unordered_map<Tag, const char*> token_syntax = {
	{Tag::Comma, ","}, {Tag::LParen, "("}, {Tag::RParen, ")"},
	{Tag::LBrace, "{"}, {Tag::RBrace, "}"}, {Tag::LBracket, "["},
	{Tag::RBracket, "]"}, {Tag::Colon, ":"}, {Tag::Dollar, "$"},
	{Tag::Question, "?"}, {Tag::Semi, ";"},
};

std::string Layout::Text() const
	{
	auto it = token_syntax.find(tag);
	if ( it != token_syntax.end() )
		return std::string(it->second) + trailing_comment;

	if ( ! args.empty() )
		return args.back() + trailing_comment;

	return trailing_comment;
	}

static void print_quoted(const std::string& s)
	{
	putchar('"');

	for ( char c : s )
		{
		switch ( c ) {
		case '"': printf("\\\""); break;
		case '\\': printf("\\\\"); break;
		case '\n': printf("\\n"); break;
		case '\t': printf("\\t"); break;
		case '\r': printf("\\r"); break;
		default: putchar(c); break;
		}
		}

	putchar('"');
	}

static void do_indent(int n)
	{
	for ( int i = 0; i < n; ++i )
		printf("  ");
	}

void Layout::Dump(int indent) const
	{
	// Emit pre-comments as COMMENT-LEADING siblings, then any
	// interleaved markers (BLANK etc.), before this node.
	for ( const auto& pc : pre_comments )
		{
		do_indent(indent);
		printf("COMMENT-LEADING ");
		print_quoted(pc);
		printf("\n");
		}

	for ( const auto& pm : pre_markers )
		pm->Dump(indent);

	do_indent(indent);

	printf("%s", TagToString(tag));

	for ( const auto& a : args )
		{
		putchar(' ');
		print_quoted(a);
		}

	if ( ! has_block )
		{
		printf("\n");

		// Emit trailing comment as a sibling COMMENT-TRAILING line.
		// Strip leading space added by SetTrailingComment.
		if ( ! trailing_comment.empty() )
			{
			do_indent(indent);
			printf("COMMENT-TRAILING ");
			print_quoted(trailing_comment.substr(1));
			printf("\n");
			}

		return;
		}

	if ( children.empty() && trailing_comment.empty() )
		{
		printf(" {\n");
		do_indent(indent);
		printf("}\n");
		return;
		}

	printf(" {\n");

	for ( const auto& child : children )
		child->Dump(indent + 1);

	do_indent(indent);
	printf("}\n");

	// Emit trailing comment as a sibling after the block.
	// Strip leading space added by SetTrailingComment.
	if ( ! trailing_comment.empty() )
		{
		do_indent(indent);
		printf("COMMENT-TRAILING ");
		print_quoted(trailing_comment.substr(1));
		printf("\n");
		}
	}

LIPtr tok(const LayoutPtr& n)
	{
	auto item = std::make_shared<LILit>(Formatting(n));
	item->SetMustBreak(n->MustBreakAfter());
	return item;
	}

static constexpr int BEAM_WIDTH = 4;

// Default LayoutStep: asserts - resolution items must be resolved
// before the beam runs.
Partials LayoutItem::LayoutStep(Partials&, const FmtContext&, int) const
	{
	assert(false);
	return {};
	}

Partials LILit::LayoutStep(Partials& beam, const FmtContext& ctx, int) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		Partial np = p;
		const auto& f = Fmt();
		np.fmt += f;
		int nl = f.CountLines() - 1;
		if ( nl > 0 )
			{
			np.lines += nl;
			np.col = f.LastLineLen();
			np.overflow += f.MaxLineOverflow(p.col, ctx.MaxCol());
			}
		else
			{
			np.col += f.Size();
			np.overflow += std::max(0, np.col - ctx.MaxCol());
			}
		np.must_break = MustBreak();
		next.push_back(std::move(np));
		}
	return next;
	}

Partials LIExpr::LayoutStep(Partials& beam, const FmtContext& ctx,
                             int trail) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		int avail = ctx.MaxCol() - p.col;
		FmtContext sub(ctx.Indent(), p.col, avail, trail);
		auto cs = format_expr(*LI_Node(), sub);
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
		}
	return next;
	}

Partials LISp::LayoutStep(Partials& beam, const FmtContext&, int) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		// Option 1: space (skip if preceding token forces a break).
		if ( ! p.must_break )
			{
			Partial s = p;
			s.fmt += " ";
			++s.col;
			s.must_break = false;
			next.push_back(std::move(s));
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
		}

	return next;
	}

Partials LIHardBreak::LayoutStep(Partials& beam, const FmtContext&, int) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		int brk_col = p.indent * INDENT_WIDTH;
		auto pad = "\n" + line_prefix(p.indent, brk_col);

		Partial np = p;
		np.fmt += pad;
		np.col = brk_col;
		++np.lines;
		np.must_break = false;

		next.push_back(std::move(np));
		}

	return next;
	}

Partials LIIndentUp::LayoutStep(Partials& beam, const FmtContext&, int) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		Partial np = p;
		++np.indent;
		next.push_back(std::move(np));
		}

	return next;
	}

Partials LIIndentDown::LayoutStep(Partials& beam, const FmtContext&, int) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		Partial np = p;

		// Emit pre-comments for the following token at the
		// current (inner) indent.
		if ( LI_Node() )
			{
			int inner_col = np.indent * INDENT_WIDTH;
			auto inner_pad = line_prefix(np.indent, inner_col);
			np.fmt += LI_Node()->EmitPreComments(inner_pad);
			}

		--np.indent;
		int new_col = np.indent * INDENT_WIDTH;
		auto pad = line_prefix(np.indent, new_col);
		np.fmt += pad;
		np.col = new_col;

		next.push_back(std::move(np));
		}
	return next;
	}

Partials LIArgListR::LayoutStep(Partials& beam, const FmtContext& ctx,
                                int trail) const
	{
	Partials next;
	auto child = LI_Node();
	auto open = Formatting(child->Children().front());
	auto close = Formatting(child->Children().back());
	auto items = collect_args(child->Children());
	auto& suffix = Fmt();
	auto& prefix = Prefix();

	if ( items.empty() )
		{
		auto empty_list = prefix + open + close + suffix;
		for ( auto& p : beam )
			{
			Partial np = p;
			np.fmt += empty_list;
			np.col += empty_list.Size();
			np.overflow += std::max(0, np.col - ctx.MaxCol());
			np.must_break = false;
			next.push_back(std::move(np));
			}
		return next;
		}

	int fl = Flags();
	const bool trail_comma_vert = fl & AL_TrailingCommaVertical;
	const bool all_comments_vert = fl & AL_AllCommentsVertical;
	const bool flat_or_vert = fl & AL_FlatOrVertical;
	const bool trail_comma_fill = fl & AL_TrailingCommaFill;
	const bool vert_upgrade = fl & AL_VerticalUpgrade;

	bool has_tc =
		trail_comma_vert && child->FindOptChild(Tag::TrailingComma);

	for ( auto& p : beam )
		{
		int avail = ctx.MaxCol() - p.col;
		FmtContext sub(ctx.Indent(), p.col, avail, trail);

		// All-comments or trailing comma: force vertical.
		bool force_vert = has_tc;
		if ( ! force_vert && all_comments_vert && has_breaks(items) )
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
			auto c = format_args_vertical(open, close, items,
							sub, has_tc);
			Partial np = p;
			np.fmt += c.Fmt();
			np.overflow += c.Ovf();
			np.must_break = false;
			np.lines += c.Lines() - 1;
			np.col = c.Width();

			next.push_back(std::move(np));
			continue;
			}

		Candidates cs;
		if ( flat_or_vert )
			{
			if ( ! has_breaks(items) )
				{
				int pfx_w = prefix.Size();
				int open_w = open.Size();
				int close_w = close.Size();
				FmtContext ac(sub.Indent(),
					p.col + pfx_w + open_w,
					sub.Width() - pfx_w - open_w - close_w);
				auto flat = prefix + open +
					format_args_flat(items, ac).Fmt() +
					close + suffix;
				Candidate fc(std::move(flat), sub);
				cs.push_back(fc);
				}

			if ( cs.empty() || cs.back().Ovf() > 0 )
				cs.push_back(format_args_vertical(open, close,
								items, sub));
			}
		else
			{
			std::string close_pfx;
			if ( trail_comma_fill &&
			     child->FindOptChild(Tag::TrailingComma) )
				close_pfx = ", ";

			cs = flat_or_fill(prefix, open, close, suffix, items,
						sub, child->TrailingComment(),
						close_pfx);

			if ( vert_upgrade && items.size() >= 3 &&
			     cs.size() > 1 &&
			     cs.back().Lines() == static_cast<int>(items.size()) )
				{
				cs.pop_back();
				cs.push_back(format_args_vertical(open, close,
								items, sub));
				}
			}

		int al_col = p.col + prefix.Size() + open.Size();
		for ( const auto& c : cs )
			{
			Partial np = p;
			np.fmt += c.Fmt();
			np.overflow += c.Ovf();
			np.must_break = false;
			np.align_col = al_col;

			if ( c.Lines() > 1 )
				{
				np.lines += c.Lines() - 1;
				np.col = 0;
				}

			np.col += c.Width();
			next.push_back(std::move(np));
			}
		}

	return next;
	}

Partials LIOpFillR::LayoutStep(Partials& beam, const FmtContext& ctx,
                               int trail) const
	{
	Partials next;
	auto& operands = Operands();
	auto& op = Fmt();
	auto sep = " " + op + " ";
	int sep_w = static_cast<int>(sep.Size());
	int max_col = ctx.MaxCol() - trail;

	for ( auto& p : beam )
		{
		// Try flat.
		Formatting flat;
		int flat_w = 0;
		bool any_multiline = false;
		for ( size_t j = 0; j < operands.size(); ++j )
			{
			auto cs = format_expr(*operands[j],
						ctx.After(p.col + flat_w));
			auto bc = best(cs);
			if ( bc.Lines() > 1 )
				any_multiline = true;

			if ( j > 0 )
				{
				flat += sep;
				flat_w += sep_w;
				}

			flat += bc.Fmt();
			flat_w += bc.Width();
			}

		int flat_ovf = std::max(0, p.col + flat_w +
						trail - ctx.MaxCol());
		if ( flat_ovf == 0 && ! any_multiline )
			{
			Partial np = p;
			np.fmt += flat;
			np.col += flat_w;
			np.must_break = false;
			next.push_back(std::move(np));
			continue;
			}

		// Fill: greedy pack with wrap at operator.
		FmtContext cont_ctx = p.col == ctx.IndentCol() ?
					ctx.Indented() : ctx.AtCol(p.col);
		auto pad = line_prefix(cont_ctx.Indent(), cont_ctx.Col());

		Formatting text;
		int cur_col = p.col;
		int fill_lines = 0;
		int fill_ovf = 0;

		for ( size_t j = 0; j < operands.size(); ++j )
			{
			FmtContext sub(cont_ctx.Indent(), cur_col,
					max_col - cur_col);
			auto bc = best(format_expr(*operands[j], sub));
			int w = bc.Width();

			if ( j == 0 )
				{
				text += bc.Fmt();
				cur_col += w;
				}
			else
				{
				int need = bc.Lines() > 1 ?
						max_col + 1 : sep_w + w;

				if ( cur_col + need <= max_col )
					{
					text += sep + bc.Fmt();
					cur_col += need;
					}
				else
					{
					text += " " + op + "\n" + pad;
					cur_col = cont_ctx.Col();
					++fill_lines;

					FmtContext ws(cont_ctx.Indent(),
						cur_col, max_col - cur_col);
					auto wb = best(format_expr(*operands[j], ws));
					text += wb.Fmt();
					cur_col += wb.Width();
					fill_ovf += wb.Ovf();
					}
				}

			if ( bc.Lines() > 1 )
				{
				fill_lines += bc.Lines() - 1;
				cur_col = text.LastLineLen();
				}

			fill_ovf += bc.Ovf();
			}

		fill_ovf += std::max(0, cur_col - max_col);

		Partial np = p;
		np.fmt += text;
		np.col = cur_col;
		np.lines += fill_lines;
		np.overflow += fill_ovf;
		np.must_break = false;

		next.push_back(std::move(np));
		}

	return next;
	}

Partials LIFlatSplitR::LayoutStep(Partials& beam, const FmtContext& ctx,
                                   int trail) const
	{
	Partials next;

	for ( auto& p : beam )
		{
		int avail = ctx.MaxCol() - p.col;
		FmtContext sub(ctx.Indent(), p.col, avail, trail);
		auto cs = flat_or_split(Steps(), Splits(), sub, ForceFlatSubs());
		for ( const auto& c : cs )
			{
			Partial np = p;
			np.fmt += c.Fmt();
			np.overflow += c.Ovf();
			np.must_break = false;

			if ( c.Lines() > 1 )
				{
				np.lines += c.Lines() - 1;
				np.col = 0;
				}

			np.col += c.Width();
			next.push_back(std::move(np));
			}
		}

	return next;
	}

Partials LIDeclCandsR::LayoutStep(Partials& beam, const FmtContext&, int) const
	{
	Partials next;

	for ( auto& p : beam )
		for ( const auto& c : Cands() )
			{
			Partial np = p;
			np.fmt += c.Fmt();
			np.overflow += c.Ovf();
			np.must_break = false;

			if ( c.Lines() > 1 )
				{
				np.lines += c.Lines() - 1;
				np.col = 0;
				}

			np.col += c.Width();
			next.push_back(std::move(np));
			}

	return next;
	}

Partials LISoftContR::LayoutStep(Partials& beam,
					const FmtContext& ctx, int) const
	{
	Partials next;
	auto& f = Fmt();
	if ( f.Empty() )
		{
		next = beam;
		return next;
		}

	for ( auto& p : beam )
		{
		// Option 1: inline (space + content).
		if ( ! p.must_break )
			{
			Partial np = p;
			np.fmt += " " + f;
			np.col += 1 + f.Size();
			np.overflow += std::max(0, np.col - ctx.MaxCol());
			np.must_break = false;
			next.push_back(std::move(np));
			}

		// Option 2: continuation at arglist alignment column
		// (or indented if no preceding arglist).
		int brk_col = (p.align_col >= 0) ?
				p.align_col : (p.indent + 1) * INDENT_WIDTH;
		auto pad = "\n" + line_prefix(p.indent, brk_col);

		Partial bp = p;
		bp.fmt += pad + f;
		bp.col = brk_col + f.Size();
		++bp.lines;
		bp.overflow += std::max(0, bp.col - ctx.MaxCol());
		bp.must_break = false;

		next.push_back(std::move(bp));
		}

	return next;
	}

// ---- Beam search driver --------------------------------------------------

// Prune beam to best BEAM_WIDTH using same priority as
// Candidate::BetterThan: overflow > lines > spread.
static void prune_beam(Partials& beam)
	{
	if ( static_cast<int>(beam.size()) <= BEAM_WIDTH )
		return;

	std::sort(beam.begin(), beam.end(),
		[](const Partial& a, const Partial& b)
			{
			return std::tie(a.overflow, a.lines) <
			       std::tie(b.overflow, b.lines);
			});

	beam.resize(BEAM_WIDTH);
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
			auto& i_j = items[j];
			auto k = i_j->kind;

			if ( k == Lit )
				{
				if ( i_j->Fmt().Contains('\n') )
					break;
				w += i_j->Fmt().Size();
				}

			else if ( k == Sp )
				++w;  // space in the flat case

			else if ( k == SoftCont )
				{
				// Conservative: assume inline placement.
				auto& f = i_j->Fmt();
				if ( f.Contains('\n') )
					break;
				w += 1 + f.Size();
				}

			else if ( k == IndentUp || k == IndentDown ||
				  k == HardBreak )
				continue;

			else
				break;
			}

		w += ctx.Trail();
		return w;
		};

	Partials beam = {{Formatting(), ctx.Col(), ctx.Indent(),
	                   1, 0, false, -1}};

	for ( size_t i = 0; i < items.size(); ++i )
		{
		beam = items[i]->LayoutStep(beam, ctx, trail_after(i));
		prune_beam(beam);
		}

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

// ---- BuildLayout resolution ----------------------------------------------

Candidates Layout::BuildLayout(LayoutItems items, const FmtContext& ctx) const
	{
	for ( size_t i = 0; i < items.size(); ++i )
		ResolveItem(items, i, ctx);

	return build_layout(items, ctx);
	}

void Layout::ResolveItem(LayoutItems& items, size_t i,
                          const FmtContext& ctx) const
	{
	auto& item = items[i];

	if ( auto cf = item->GetComputeFn() )
		{
		item = (this->*cf)(ctx);
		return;
		}

	switch ( item->kind ) {
	case DeclCands:
		item = std::make_shared<LIDeclCandsR>(ComputeDecl(ctx));
		break;

	case SoftCont:
		{
		auto result = (this->*(item->SuffixFn()))(ctx);
		item = std::make_shared<LISoftContR>(result->Fmt());
		break;
		}

	case Tok:
		{
		auto c = Child(item->ChildIdx());
		if ( item->SubChildIdx() >= 0 )
			c = c->Child(item->SubChildIdx());
		item = tok(c);
		break;
		}

	case ExprIdx:
		item = std::make_shared<LIExpr>(Child(item->ChildIdx()));
		break;

	case LastTok:
		item = tok(Children().back());
		break;

	case ArgIdx:
		item = lit(Formatting(Arg(item->ChildIdx())));
		break;

	case ArgList:
		{
		Formatting prefix, suffix;
		int flags = item->Flags();

		if ( item->PrefixFn() )
			prefix = (this->*(item->PrefixFn()))(ctx)->Fmt();
		if ( item->SuffixFn() )
			suffix = (this->*(item->SuffixFn()))(ctx)->Fmt();
		else
			suffix = item->Fmt();

		auto& c = Child(item->ChildIdx());
		if ( flags )
			item = std::make_shared<LIArgListR>(c,
				std::move(prefix), std::move(suffix), flags);
		else
			item = std::make_shared<LIArgListR>(c,
				std::move(prefix), std::move(suffix));
		break;
		}

	case FlatSplit:
		{
		// Resolve deferred child references in steps.
		auto steps = item->Steps();
		for ( auto& s : steps )
			{
			if ( s.kind == FmtStep::SExprIdx )
				s = FmtStep::E(Child(s.child_idx));
			else if ( s.kind == FmtStep::STokIdx )
				s = FmtStep::L(Child(s.child_idx));
			}

		item = std::make_shared<LIFlatSplitR>(std::move(steps),
					item->Splits(), item->ForceFlatSubs());
		break;
		}

	case FillList:
		{
		auto prefix = Formatting(Children().front()) + " ";
		auto args = collect_args(Children());
		auto cs = flat_or_fill(prefix, "", "", "", args, ctx);
		item = lit(best(cs).Fmt());
		break;
		}

	case StmtBody:
		{
		int fl = item->Flags();
		auto all_children = fl & SB_AllChildren;
		auto skip_blanks = fl & SB_SkipBlanks;
		auto strip_newline = fl & SB_StripNewline;

		auto cidx = item->ChildIdx();
		const Layout& src = (cidx >= 0) ? *Child(cidx) : *this;

		LayoutVec body;
		if ( all_children )
			body = src.Children();
		else
			for ( const auto& c : src.Children() )
				if ( ! c->IsToken() )
					body.push_back(c);

		auto sub = ctx.Indented();
		auto text = format_stmt_list(body, sub, skip_blanks);

		if ( strip_newline && ! text.Empty() && text.Back() == '\n' )
			text.PopBack();

		item = lit(Formatting("\n" + text));
		break;
		}

	case BodyText:
		item = lit(*Child(item->ChildIdx())->FormatBodyText(ctx));
		break;

	case OpFill:
		item = std::make_shared<LIOpFillR>(Arg(), ContentChildren());
		break;

	case IndentDown:
		{
		// Peek ahead to find the next token and
		// attach its node for pre-comment emission.
		for ( size_t j = i + 1; j < items.size(); ++j )
			{
			auto k = items[j]->kind;

			if ( k == Tok )
				{
				auto& i_j = items[j];
				auto c = Child(i_j->ChildIdx());
				if ( i_j->SubChildIdx() >= 0 )
					c = c->Child(i_j->SubChildIdx());
				item = std::make_shared<LIIndentDown>(c);
				break;
				}

			if ( k == LastTok )
				{
				auto& cb = Children().back();
				item = std::make_shared<LIIndentDown>(cb);
				break;
				}
			}

		// If no following token found, create without node.
		if ( item->kind == IndentDown &&
		     ! dynamic_cast<LIIndentDown*>(item.get()) )
			item = std::make_shared<LIIndentDown>();
		break;
		}

	default:
		break;
	}
	}

// ---- Layout table + factory ----------------------------------------------

// Compute function pointer constants for the layout table.
static constexpr ComputeFn CParamType = &Layout::ComputeParamType;
static constexpr ComputeFn COfType = &Layout::ComputeOfType;
static constexpr ComputeFn CRetType = &Layout::ComputeRetType;
static constexpr ComputeFn CEnumBody = &Layout::ComputeEnumBody;
static constexpr ComputeFn CRecordBody = &Layout::ComputeRecordBody;
static constexpr ComputeFn CLambdaPrefix = &Layout::ComputeLambdaPrefix;
static constexpr ComputeFn CLambdaRet = &Layout::ComputeLambdaRet;
static constexpr ComputeFn CLambdaBody = &Layout::ComputeLambdaBody;
static constexpr ComputeFn CFuncRet = &Layout::ComputeFuncRet;
static constexpr ComputeFn CFuncAttrs = &Layout::ComputeFuncAttrs;
static constexpr ComputeFn CFuncBody = &Layout::ComputeFuncBody;
static constexpr ComputeFn CSwitchExpr = &Layout::ComputeSwitchExpr;
static constexpr ComputeFn CSwitchCases = &Layout::ComputeSwitchCases;
static constexpr ComputeFn CElseFollowOn = &Layout::ComputeElseFollowOn;

// Tag-to-layout table for purely declarative nodes.
static const std::unordered_map<Tag, LayoutItems> layout_table = {
	{Tag::Identifier, {arg(0)}},
	{Tag::Constant, {arg(0)}},
	{Tag::TypeAtom, {arg(0)}},
	{Tag::Interval, {arg(0), lit(" "), arg(1)}},
	{Tag::Cardinality, {tok(0), expr(1), tok(2)}},
	{Tag::Negation, {tok(0), lit(" "), expr(1)}},
	{Tag::UnaryOp, {tok(0), expr(1)}},
	{Tag::FieldAccess, {expr(0), tok(1), tok(2)}},
	{Tag::FieldAssign, {tok(0), arg(0), tok(1), expr(2)}},
	{Tag::HasField, {expr(0), tok(1), tok(2)}},
	{Tag::Paren, {tok(0), expr(1), tok(2)}},
	{Tag::Schedule, {tok(0), sp(), expr(2), sp(), tok(3), sp(),
		expr(4), sp(), tok(5)}},
	{Tag::Param, {arg(0), computed(CParamType)}},
	{Tag::Call, {expr(0), arglist(1,
		AL_TrailingCommaVertical | AL_VerticalUpgrade)}},
	{Tag::Constructor, {tok(0), arglist(1,
		AL_TrailingCommaVertical | AL_FlatOrVertical)}},
	{Tag::IndexLiteral, {arglist(0,
		AL_AllCommentsVertical | AL_TrailingCommaFill)}},
	{Tag::Index, {expr(0), arglist(1)}},
	{Tag::TypeParameterized, {arg(0), arglist(0, COfType)}},
	{Tag::TypeOf, {arg(0), lit(" "), tok(0), lit(" "), expr(2)}},
	{Tag::TypeFunc, {arg(0), arglist(0)}},
	{Tag::TypeFuncRet, {arg(0), arglist(0, CRetType)}},
	{Tag::CommentLeading, {arg(0)}},
	{Tag::ExprStmt, {expr(0), last()}},
	{Tag::ReturnVoid, {tok(0), last()}},
	{Tag::Return, {tok(0), sp(), expr(2), last()}},
	{Tag::Add, {tok(0), sp(), expr(2), last()}},
	{Tag::Delete, {tok(0), sp(), expr(2), last()}},
	{Tag::Assert, {tok(0), sp(), expr(2), last()}},
	{Tag::Print, {fill_list(), last()}},
	{Tag::EventStmt, {tok(0), lit(" "), arg(0), arglist(2), tok(3)}},
	{Tag::ExportDecl, {tok(0), sp(), tok(2), indent_up(),
		stmt_body(), indent_down(), last()}},
	{Tag::ModuleDecl, {tok(0), sp(), tok(2), tok(3)}},
	{Tag::TypeDeclAlias, {tok(0), sp(), tok(2), tok(3), sp(),
		expr(5), tok(6)}},
	{Tag::TypeDeclEnum, {tok(0), sp(), tok(2), tok(3), sp(),
		tok(5, 0), sp(), tok(5, 2),
		computed(CEnumBody), last()}},
	{Tag::TypeDeclRecord, {tok(0), sp(), tok(2), tok(3), sp(),
		tok(5, 0), sp(), tok(5, 2),
		computed(CRecordBody), last()}},
	{Tag::IfNoElse, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5)}},
	{Tag::IfElse, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5), computed(CElseFollowOn)}},
	{Tag::While, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5)}},
	{Tag::ForCond, {expr(0), lit(" "), tok(1), lit(" "), expr(2)}},
	{Tag::ForCondVal, {expr(0), tok(1), lit(" "), expr(2), lit(" "),
		tok(3), lit(" "), expr(4)}},
	{Tag::ForCondBracket, {arglist(0), lit(" "), tok(1), lit(" "),
		expr(2)}},
	{Tag::ForCondBracketVal, {arglist(0), tok(1), lit(" "), expr(2),
		lit(" "), tok(3), lit(" "), expr(4)}},
	{Tag::For, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5)}},
	{Tag::Slice, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1), FmtStep::EI(2),
		 FmtStep::L(" "), FmtStep::TI(3), FmtStep::S(),
		 FmtStep::EI(4), FmtStep::TI(5)},
		{{4, SplitAt::AlignWith, 2}}, true)}},
	{Tag::SlicePartial, {expr(0), tok(1), expr(2), tok(3),
		expr(4), tok(5)}},
	{Tag::Div, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1),
		 FmtStep::S(""), FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame, true}})}},
	{Tag::BinaryOp, {flat_split(
		{FmtStep::EI(0), FmtStep::L(" "),
		 FmtStep::TI(1), FmtStep::S(), FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame}})}},
	{Tag::Lambda, {arglist_prefix(2, CLambdaPrefix, CLambdaRet),
		computed(CLambdaBody)}},
	{Tag::LambdaCaptures, {arglist_prefix(3, CLambdaPrefix, CLambdaRet),
		computed(CLambdaBody)}},
	{Tag::FuncDecl, {tok(0), lit(" "), tok(2), arglist(3, CFuncRet),
		soft_cont(CFuncAttrs), computed(CFuncBody)}},
	{Tag::FuncDeclRet, {tok(0), lit(" "), tok(2),
		arglist(3, CFuncRet), soft_cont(CFuncAttrs),
		computed(CFuncBody)}},
	{Tag::Switch, {tok(0), lit(" "), computed(CSwitchExpr), lit(" "),
		tok(3), computed(CSwitchCases), hard_brk(), last()}},
	{Tag::GlobalDecl, {decl_cands()}},
	{Tag::LocalDecl, {decl_cands()}},
	{Tag::BoolChain, {op_fill()}},
	{Tag::Ternary, {flat_split(
		{FmtStep::EI(0), FmtStep::L(" "), FmtStep::TI(1),
		 FmtStep::S(), FmtStep::EI(2), FmtStep::L(" "),
		 FmtStep::TI(3), FmtStep::S(), FmtStep::EI(4)},
		{{6, SplitAt::AlignWith, 4},
		 {2, SplitAt::SameCol}})}},
};

LayoutPtr MakeNode(Tag tag)
	{
	auto it = layout_table.find(tag);
	if ( it != layout_table.end() )
		return std::make_shared<Layout>(tag, it->second);

	return std::make_shared<Layout>(tag);
	}
