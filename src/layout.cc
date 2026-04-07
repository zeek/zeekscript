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
					TagToString(tag) + " child " +
					std::to_string(i) + " is " +
					TagToString(c->GetTag()) + ", expected " +
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

// ---- Beam search engine --------------------------------------------------

// Layout combinator

const LayoutItem soft_sp{Sp};
const LayoutItem hard_break{HardBreak};
const LayoutItem indent_up{IndentUp};
const LayoutItem indent_down{IndentDown};

LayoutItem tok(const LayoutPtr& n)
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
	int align_col;    // alignment column set by ArgList for SoftCont
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
			auto& prefix = item.Prefix();

			if ( items.empty() )
				{
				auto empty_list = prefix + open + close + suffix;
				Partial np = p;
				np.fmt += empty_list;
				np.col += empty_list.Size();
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
					int pfx_w = prefix.Size();
					int open_w = open.Size();
					int close_w = close.Size();
					FmtContext ac(sub.Indent(),
						p.col + pfx_w + open_w,
						sub.Width() - pfx_w - open_w
							- close_w);
					auto flat = prefix + open +
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

				cs = flat_or_fill(prefix, open, close,
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
					np.col = c.Width();
					}
				else
					np.col += c.Width();
				next.push_back(std::move(np));
				}
			break;
			}

		case OpFill:
			{
			auto& operands = item.Operands();
			auto& op = item.Fmt();
			auto sep = " " + op + " ";
			int sep_w = static_cast<int>(sep.Size());
			int max_col = ctx.MaxCol() - trail;

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

			int flat_ovf = std::max(0, p.col + flat_w + trail - ctx.MaxCol());
			if ( flat_ovf == 0 && ! any_multiline )
				{
				Partial np = p;
				np.fmt += flat;
				np.col += flat_w;
				np.must_break = false;
				next.push_back(std::move(np));
				break;
				}

			// Fill: greedy pack with wrap at operator.
			FmtContext cont_ctx = p.col == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(p.col);
			auto pad = line_prefix(cont_ctx.Indent(),
						cont_ctx.Col());

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
							cur_col,
							max_col - cur_col);
						auto wb = best(
							format_expr(*operands[j], ws));
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

		case DeclCands:
			{
			for ( const auto& c : item.Cands() )
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

		case SoftCont:
			{
			auto& f = item.Fmt();
			if ( f.Empty() )
				{
				next.push_back(p);
				break;
				}

			// Option 1: inline (space + content).
			if ( ! p.must_break )
				{
				Partial np = p;
				np.fmt += " " + f;
				np.col += 1 + f.Size();
				np.overflow += ovf_at(np.col);
				np.must_break = false;
				next.push_back(std::move(np));
				}

			// Option 2: continuation at arglist alignment column
			// (or indented if no preceding arglist).
			{
			int brk_col = (p.align_col >= 0) ? p.align_col
				: (p.indent + 1) * INDENT_WIDTH;
			auto pad = "\n" + line_prefix(p.indent, brk_col);
			Partial bp = p;
			bp.fmt += pad + f;
			bp.col = brk_col + f.Size();
			++bp.lines;
			bp.overflow += std::max(0, bp.col - ctx.MaxCol());
			bp.must_break = false;
			next.push_back(std::move(bp));
			}
			break;
			}

		case Tok: case ExprIdx: case LastTok: case ArgIdx:
		case StmtBody: case BodyText: case FillList:
		case ParamType: case OfType: case RetType:
		case EnumBody: case RecordBody:
		case LambdaPrefix: case LambdaRet: case LambdaBody:
		case FuncRet: case FuncAttrs: case FuncBody:
		case SwitchExpr: case SwitchCases: case ElseFollowOn:
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
				{
				if ( items[j].Fmt().Contains('\n') )
					break;
				w += items[j].Fmt().Size();
				}
			else if ( k == Sp )
				++w;  // space in the flat case
			else if ( k == SoftCont )
				{
				// Conservative: assume inline placement.
				auto& f = items[j].Fmt();
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

	Partials beam = {{Formatting(), ctx.Col(), ctx.Indent(), 1, 0, false, -1}};

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

static bool is_computed(LIKind k)
	{
	switch ( k ) {
	case ParamType: case OfType: case RetType:
	case EnumBody: case RecordBody:
	case LambdaPrefix: case LambdaRet: case LambdaBody:
	case FuncRet: case FuncAttrs: case FuncBody:
	case SwitchExpr: case SwitchCases:
	case ElseFollowOn: case DeclCands:
		return true;
	default:
		return false;
	}
	}

static LayoutItem dispatch_compute(const Layout& node, LIKind op,
                                   const FmtContext& ctx)
	{
	switch ( op ) {
	case ParamType: return node.ComputeParamType(ctx);
	case OfType: return node.ComputeOfType(ctx);
	case RetType: return node.ComputeRetType(ctx);
	case EnumBody: return node.ComputeEnumBody(ctx);
	case RecordBody: return node.ComputeRecordBody(ctx);
	case LambdaPrefix: return node.ComputeLambdaPrefix(ctx);
	case LambdaRet: return node.ComputeLambdaRet(ctx);
	case LambdaBody: return node.ComputeLambdaBody(ctx);
	case FuncRet: return node.ComputeFuncRet(ctx);
	case FuncAttrs: return node.ComputeFuncAttrs(ctx);
	case FuncBody: return node.ComputeFuncBody(ctx);
	case SwitchExpr: return node.ComputeSwitchExpr(ctx);
	case SwitchCases: return node.ComputeSwitchCases(ctx);
	case ElseFollowOn: return node.ComputeElseFollowOn(ctx);
	case DeclCands:
		assert(false);  // DeclCands returns Candidates, not LayoutItem
		return Formatting();
	default:
		assert(false);
		return Formatting();
	}
	}

Candidates Layout::BuildLayout(LayoutItems items, const FmtContext& ctx) const
	{
	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& item = items[i];

		if ( item.kind == DeclCands )
			{
			auto cs = ComputeDecl(ctx);
			item = LayoutItem(DeclCands, std::move(cs));
			continue;
			}

		if ( is_computed(item.kind) )
			{
			item = dispatch_compute(*this, item.kind, ctx);
			continue;
			}

		if ( item.kind == SoftCont )
			{
			auto result = dispatch_compute(*this, item.SuffixOp(), ctx);
			item = LayoutItem(SoftCont, result.Fmt());
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
			Formatting prefix;
			int flags = item.Flags();
			if ( item.SuffixOp() != Lit )
				suffix = dispatch_compute(*this, item.SuffixOp(), ctx).Fmt();
			if ( item.PrefixOp() != Lit )
				prefix = dispatch_compute(*this, item.PrefixOp(), ctx).Fmt();
			if ( flags )
				item = LayoutItem(ArgList, Child(item.ChildIdx()),
					std::move(prefix), std::move(suffix),
					flags);
			else
				item = LayoutItem(ArgList, Child(item.ChildIdx()),
					std::move(prefix), std::move(suffix));
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
			const Layout& src = (item.ChildIdx() >= 0)
				? *Child(item.ChildIdx()) : *this;
			int fl = item.Flags();

			LayoutVec body;
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
		else if ( item.kind == BodyText )
			item = LayoutItem(*Child(item.ChildIdx())->FormatBodyText(ctx));
		else if ( item.kind == OpFill )
			{
			auto op = Arg();
			auto ops = ContentChildren();
			item = LayoutItem(OpFill, std::move(op), std::move(ops));
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

// ---- Layout table + factory ----------------------------------------------

// Tag-to-layout table for purely declarative nodes.  Uses LIKind
// enum values directly instead of the soft_sp/indent_up/indent_down
// globals to avoid cross-TU static initialization order issues.
static const std::unordered_map<Tag, LayoutItems> layout_table = {
	{Tag::Identifier, {arg(0)}},
	{Tag::Constant, {arg(0)}},
	{Tag::TypeAtom, {arg(0)}},
	{Tag::Interval, {arg(0), " ", arg(1)}},
	{Tag::Cardinality, {0U, expr(1), 2}},
	{Tag::Negation, {0U, " ", expr(1)}},
	{Tag::UnaryOp, {0U, expr(1)}},
	{Tag::FieldAccess, {expr(0), 1, 2}},
	{Tag::FieldAssign, {0U, arg(0), 1, expr(2)}},
	{Tag::HasField, {expr(0), 1, 2}},
	{Tag::Paren, {0U, expr(1), 2}},
	{Tag::Schedule, {0U, {Sp}, expr(2), {Sp}, 3, {Sp}, expr(4), {Sp}, 5}},
	{Tag::Param, {arg(0), {ParamType}}},
	{Tag::Call, {expr(0), arglist(1,
		AL_TrailingCommaVertical | AL_VerticalUpgrade)}},
	{Tag::Constructor, {0U, arglist(1,
		AL_TrailingCommaVertical | AL_FlatOrVertical)}},
	{Tag::IndexLiteral, {arglist(0,
		AL_AllCommentsVertical | AL_TrailingCommaFill)}},
	{Tag::Index, {expr(0), arglist(1)}},
	{Tag::TypeParameterized, {arg(0), arglist(0, OfType)}},
	{Tag::TypeOf, {arg(0), " ", 0U, " ", expr(2)}},
	{Tag::TypeFunc, {arg(0), arglist(0)}},
	{Tag::TypeFuncRet, {arg(0), arglist(0, RetType)}},
	{Tag::CommentLeading, {arg(0)}},
	{Tag::ExprStmt, {expr(0), last()}},
	{Tag::ReturnVoid, {0U, last()}},
	{Tag::Return, {0U, {Sp}, expr(2), last()}},
	{Tag::Add, {0U, {Sp}, expr(2), last()}},
	{Tag::Delete, {0U, {Sp}, expr(2), last()}},
	{Tag::Assert, {0U, {Sp}, expr(2), last()}},
	{Tag::Print, {fill_list(), last()}},
	{Tag::EventStmt, {0U, " ", arg(0), arglist(2), 3}},
	{Tag::ExportDecl, {0U, {Sp}, 2, {IndentUp},
		stmt_body(), {IndentDown}, last()}},
	{Tag::ModuleDecl, {0U, {Sp}, 2, 3}},
	{Tag::TypeDeclAlias, {0U, {Sp}, 2, 3, {Sp}, expr(5), 6}},
	{Tag::TypeDeclEnum, {0U, {Sp}, 2, 3, {Sp}, {5, 0U}, {Sp}, {5, 2},
		{EnumBody}, last()}},
	{Tag::TypeDeclRecord, {0U, {Sp}, 2, 3, {Sp}, {5, 0U}, {Sp}, {5, 2},
		{RecordBody}, last()}},
	{Tag::IfNoElse, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5)}},
	{Tag::IfElse, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5),
		{ElseFollowOn}}},
	{Tag::While, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5)}},
	{Tag::ForCond, {expr(0), " ", 1, " ", expr(2)}},
	{Tag::ForCondVal, {expr(0), 1, " ", expr(2), " ", 3, " ", expr(4)}},
	{Tag::ForCondBracket, {arglist(0), " ", 1, " ", expr(2)}},
	{Tag::ForCondBracketVal, {arglist(0), 1, " ", expr(2), " ", 3, " ", expr(4)}},
	{Tag::For, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5)}},
	{Tag::Slice, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1),
		 FmtStep::EI(2),
		 FmtStep::L(" "), FmtStep::TI(3), FmtStep::S(),
		 FmtStep::EI(4),
		 FmtStep::TI(5)},
		{{4, SplitAt::AlignWith, 2}}, true)}},
	{Tag::SlicePartial, {expr(0), 1, expr(2), 3, expr(4), 5}},
	{Tag::Div, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1),
		 FmtStep::S(""), FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame, true}})}},
	{Tag::BinaryOp, {flat_split(
		{FmtStep::EI(0), FmtStep::L(" "),
		 FmtStep::TI(1), FmtStep::S(),
		 FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame}})}},
	{Tag::Lambda, {arglist_prefix(2, LambdaPrefix, LambdaRet),
		{LambdaBody}}},
	{Tag::LambdaCaptures, {arglist_prefix(3, LambdaPrefix, LambdaRet),
		{LambdaBody}}},
	{Tag::FuncDecl, {0U, " ", 2U, arglist(3, FuncRet),
		soft_cont(FuncAttrs), {FuncBody}}},
	{Tag::FuncDeclRet, {0U, " ", 2U, arglist(3, FuncRet),
		soft_cont(FuncAttrs), {FuncBody}}},
	{Tag::Switch, {0U, " ", {SwitchExpr}, " ", 3U,
		{SwitchCases}, {HardBreak}, last()}},
	{Tag::GlobalDecl, {{DeclCands}}},
	{Tag::LocalDecl, {{DeclCands}}},
	{Tag::BoolChain, {op_fill()}},
	{Tag::Ternary, {flat_split(
		{FmtStep::EI(0),
		 FmtStep::L(" "), FmtStep::TI(1),
		 FmtStep::S(),
		 FmtStep::EI(2),
		 FmtStep::L(" "), FmtStep::TI(3),
		 FmtStep::S(),
		 FmtStep::EI(4)},
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
