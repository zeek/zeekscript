// Layout core: tree construction, Format() entry point, beam search
// engine, and the tag-to-layout table.  Compute/format helpers that
// the table references live in layout_util.cc.

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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

// Tags whose argument lists are treated as parameter lists for
// balanced fill.  Currently includes calls (Call, EventStmt) as
// well as declarations (FuncDecl, TypeFunc, etc.).  Could be
// tightened to only declarations, whose typed parameters make
// balanced fill more clearly beneficial.
static const std::unordered_set<Tag> param_list_tags = {
	Tag::FuncDecl, Tag::FuncDeclRet, Tag::Call, Tag::EventStmt,
	Tag::TypeFunc, Tag::TypeFuncRet,
};

Candidates Layout::Format(const FmtContext& ctx) const
	{
	if ( ! layout.empty() )
		{
		auto fctx = ctx;
		if ( param_list_tags.count(tag) )
			fctx.SetIsParamList();

		return BuildLayout(layout, fctx);
		}

	return {Candidate(render, ctx)};
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

const LayoutPtr& Layout::ChildFromEnd(size_t offset, Tag expected) const
	{
	assert(offset < children.size());
	auto& c = children[children.size() - 1 - offset];
	assert(c->GetTag() == expected);
	return c;
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

void Layout::ComputeRender()
	{
	auto it = token_syntax.find(tag);
	if ( it != token_syntax.end() )
		render = it->second;
	else if ( tag == Tag::PreprocCond )
		render = args[0] + " " + children[0]->Text() + " " +
			args[1] + " " + children[1]->Text();
	else if ( tag == Tag::Preproc )
		{
		render = args[0];
		if ( args.size() > 1 )
			render += " " + args[1];
		}
	else if ( ! args.empty() )
		render = args.back();
	}

LIPtr tok(const LayoutPtr& n)
	{
	auto item = std::make_shared<LILit>(Formatting(n));
	item->SetMustBreak(n->MustBreakAfter());
	return item;
	}

static constexpr int BEAM_WIDTH = 4;

// Merge a Candidate into a Partial: append text, accumulate
// overflow/lines, and advance column.
static void merge_candidate(Partial& np, const Candidate& c)
	{
	np.fmt += c.Fmt();
	np.overflow += c.Ovf();
	np.must_break = false;

	if ( c.Lines() > 1 )
		{
		np.lines += c.Lines() - 1;
		np.col = 0;
		}

	np.col += c.Width();
	}

// Check whether every item in a list carries a trailing comment
// (either on the item itself or on the following comma).
static bool all_items_commented(const ArgItems& items)
	{
	for ( size_t j = 0; j < items.size(); ++j )
		{
		auto nc = (j + 1 < items.size()) ? items[j + 1].comma : nullptr;
		if ( ! items[j].arg->MustBreakAfter() &&
		     ! (nc && nc->MustBreakAfter()) )
			return false;
		}

	return true;
	}

// Default LayoutStep: asserts - resolution items must be resolved
// before the beam runs.
Partials LayoutItem::LayoutStep(Partials&, const FmtContext&, int, int) const
	{
	assert(false);
	return {};
	}

Partials LILit::LayoutStep(Partials& beam, const FmtContext& ctx,
				int, int) const
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
                             int trail, int soft_trail) const
	{
	Partials next;
	for ( auto& p : beam )
		{
		int avail = ctx.MaxCol() - p.col;
		FmtContext sub(ctx.Indent(), p.col, avail, trail, soft_trail);
		auto cs = format_expr(*LI_Node(), sub);
		for ( const auto& c : cs )
			{
			Partial np = p;
			merge_candidate(np, c);
			next.push_back(std::move(np));
			}
		}

	return next;
	}

Partials LISp::LayoutStep(Partials& beam, const FmtContext&, int, int) const
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

Partials LIHardBreak::LayoutStep(Partials& beam, const FmtContext&, int, int) const
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

Partials LIIndentUp::LayoutStep(Partials& beam, const FmtContext&, int, int) const
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

Partials LIIndentDown::LayoutStep(Partials& beam, const FmtContext&, int, int) const
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
                                int trail, int soft_trail) const
	{
	Partials next;
	auto child = LI_Node();
	auto open = Formatting(child->Children().front());
	auto close = Formatting(child->Children().back());
	LayoutPtr dangling_comma;
	auto items = collect_args(child->Children(), &dangling_comma);
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
		FmtContext sub(ctx.Indent(), p.col, avail, trail, soft_trail);
		if ( ctx.IsParamList() )
			sub.SetIsParamList();

		// All-comments or trailing comma: force vertical
		// (but not for single-item lists that fit on one line).
		bool force_vert = has_tc && items.size() > 1;
		if ( ! force_vert && all_comments_vert && has_breaks(items) )
			force_vert = all_items_commented(items);

		if ( force_vert )
			{
			auto c = format_args_vertical(open, close, items,
						sub, has_tc, dangling_comma);
			Partial np = p;
			merge_candidate(np, c);
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
					(has_tc ? "," : "") + close + suffix;
				Candidate fc(std::move(flat), sub);
				cs.push_back(fc);
				}

			if ( cs.empty() || cs.back().Ovf() > 0 )
				cs.push_back(format_args_vertical(open, close,
							items, sub, false,
							dangling_comma));
			}
		else
			{
			std::string close_pfx;
			if ( trail_comma_fill &&
			     child->FindOptChild(Tag::TrailingComma) )
				close_pfx = ", ";

			cs = flat_or_fill(prefix, open, close, suffix, items,
						sub, child->Text(), close_pfx,
						dangling_comma);

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
			np.align_col = al_col;
			merge_candidate(np, c);
			next.push_back(std::move(np));
			}
		}

	return next;
	}

Partials LIFlatSplitR::LayoutStep(Partials& beam, const FmtContext& ctx,
                                   int trail, int) const
	{
	Partials next;

	for ( auto& p : beam )
		{
		int avail = ctx.MaxCol() - p.col;
		FmtContext sub(ctx.Indent(), p.col, avail, trail);
		auto cs = flat_or_split(Steps(), Splits(), sub,
					ForceFlatSubs(), OfferSplit());
		for ( const auto& c : cs )
			{
			Partial np = p;
			merge_candidate(np, c);
			next.push_back(std::move(np));
			}
		}

	return next;
	}

Partials LIDeclCandsR::LayoutStep(Partials& beam, const FmtContext&, int, int) const
	{
	Partials next;

	for ( auto& p : beam )
		for ( const auto& c : Cands() )
			{
			Partial np = p;
			merge_candidate(np, c);
			next.push_back(std::move(np));
			}

	return next;
	}

Partials LISoftContR::LayoutStep(Partials& beam,
					const FmtContext& ctx, int, int) const
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

// Count reluctant breaks ($\n) in a Formatting.
static int reluctant_breaks(const Formatting& f)
	{
	auto& s = f.Str();
	int count = 0;
	for ( size_t i = 1; i < s.size(); ++i )
		if ( s[i] == '\n' && s[i - 1] == '$' )
			++count;
	return count;
	}

// Prune beam to best BEAM_WIDTH using same priority as
// Candidate::BetterThan: reluctant (saving lines) > overflow > lines.
static void prune_beam(Partials& beam)
	{
	if ( static_cast<int>(beam.size()) <= BEAM_WIDTH )
		return;

	std::sort(beam.begin(), beam.end(),
		[](const Partial& a, const Partial& b)
			{
			int ra = reluctant_breaks(a.fmt);
			int rb = reluctant_breaks(b.fmt);
			if ( ra != rb )
				{
				int ld = a.lines - b.lines;
				if ( ra > rb && ld <= -2 ) return true;
				if ( rb > ra && -ld <= -2 ) return false;
				return ra < rb;
				}

			if ( a.overflow != b.overflow )
				return a.overflow < b.overflow;

			return a.lines < b.lines;
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
	// Returns {total_trail, soft_trail} where soft_trail is the
	// portion from SoftCont items that can break to their own line.
	auto trail_after = [&](size_t i) -> std::pair<int, int>
		{
		int w = 0;
		int soft = 0;
		size_t j;
		for ( j = i + 1; j < items.size(); ++j )
			{
			auto& i_j = items[j];
			auto k = i_j->kind;

			if ( k == Lit )
				{
				int nl = i_j->Fmt().Find('\n');
				if ( nl >= 0 )
					{
					w += nl;
					break;
					}
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
				int sc = 1 + f.Size();
				w += sc;
				soft += sc;
				}

			else if ( k == IndentUp || k == IndentDown ||
				  k == HardBreak )
				continue;

			else
				break;
			}

		// Only add outer trail when all remaining items
		// are inline - a newline-producing item means the
		// outer trail applies to a later line, not this one.
		if ( j == items.size() )
			{
			w += ctx.Trail();
			soft += ctx.SoftTrail();
			}
		return {w, soft};
		};

	Partials beam = {{Formatting(), ctx.Col(), ctx.Indent(),
	                   1, 0, false, -1}};

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto [total, soft] = trail_after(i);
		beam = items[i]->LayoutStep(beam, ctx, total, soft);
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
					item->Splits(), item->ForceFlatSubs(),
					item->OfferSplit());
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
static constexpr ComputeFn CRedefEnumBody = &Layout::ComputeRedefEnumBody;
static constexpr ComputeFn CRecordBody = &Layout::ComputeRecordBody;
static constexpr ComputeFn CRedefRecordBody = &Layout::ComputeRedefRecordBody;
static constexpr ComputeFn CLambdaPrefix = &Layout::ComputeLambdaPrefix;
static constexpr ComputeFn CLambdaRet = &Layout::ComputeLambdaRet;
static constexpr ComputeFn CLambdaBody = &Layout::ComputeLambdaBody;
static constexpr ComputeFn CBlock = &Layout::ComputeBlock;
static constexpr ComputeFn CWhenTimeout = &Layout::ComputeWhenTimeout;
static constexpr ComputeFn CFuncRet = &Layout::ComputeFuncRet;
static constexpr ComputeFn CFuncAttrs = &Layout::ComputeFuncAttrs;
static constexpr ComputeFn CCallAttrs = &Layout::ComputeCallAttrs;
static constexpr ComputeFn CFuncBody = &Layout::ComputeFuncBody;
static constexpr ComputeFn CSwitchExpr = &Layout::ComputeSwitchExpr;
static constexpr ComputeFn CSwitchCases = &Layout::ComputeSwitchCases;
static constexpr ComputeFn CElseFollowOn = &Layout::ComputeElseFollowOn;

// Tag-to-layout table for purely declarative nodes.
static const std::unordered_map<Tag, LayoutItems> layout_table = {
	// Identifier, Constant, TypeAtom have no layout entries;
	// Format() uses render directly.
	{Tag::Interval, {arg(0), lit(" "), arg(1)}},
	{Tag::Cardinality, {tok(0), expr(1), tok(2)}},
	{Tag::Negation, {tok(0), lit(" "), expr(1)}},
	{Tag::UnaryOp, {tok(0), expr(1)}},
	{Tag::FieldAccess, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1), FmtStep::TI(2)},
		{{1, SplitAt::IndentedOrSame}}, false, true)}},
	{Tag::FieldAssign, {tok(0), arg(0), tok(1), expr(2)}},
	{Tag::HasField, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1), FmtStep::TI(2)},
		{{1, SplitAt::IndentedOrSame}}, false, true)}},
	{Tag::Paren, {tok(0), expr(1), tok(2)}},
	{Tag::Schedule, {tok(0), sp(), expr(2), sp(), tok(3), sp(),
		expr(4), sp(), tok(5)}},
	{Tag::Param, {arg(0), computed(CParamType)}},
	{Tag::Call, {expr(0), arglist(1,
		AL_TrailingCommaVertical | AL_VerticalUpgrade, CCallAttrs)}},
	{Tag::Constructor, {tok(0), arglist(1,
		AL_TrailingCommaVertical | AL_FlatOrVertical, CCallAttrs)}},
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
	{Tag::RedefEnum, {tok(0), sp(), tok(2), sp(), tok(4), sp(),
		tok(5), sp(), tok(6),
		computed(CRedefEnumBody), last()}},
	{Tag::TypeDeclRecord, {tok(0), sp(), tok(2), tok(3), sp(),
		tok(5, 0), sp(), tok(5, 2),
		computed(CRecordBody), last()}},
	{Tag::RedefRecord, {tok(0), sp(), tok(2), sp(), tok(4), sp(),
		tok(5), sp(), tok(6),
		computed(CRedefRecordBody), last()}},
	{Tag::Block, {computed(CBlock)}},
	{Tag::KeywordExpr, {tok(0), lit(" "), expr(2)}},
	{Tag::WhenLocal, {lit("local "), arg(0), lit(" = "), expr(0)}},
	{Tag::When, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5)}},
	{Tag::WhenTimeout, {tok(0), lit(" "), tok(2), lit(" "), expr(3),
		lit(" "), tok(4), body_text(5), computed(CWhenTimeout)}},
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
