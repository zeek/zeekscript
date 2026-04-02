#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "condition_block.h"
#include "formatter.h"

// ------------------------------------------------------------------
// Line prefix: tabs for indent, spaces for remaining offset
// ------------------------------------------------------------------

std::string LinePrefix(int indent, int col)
	{
	std::string s(indent, '\t');
	int space_col = indent * INDENT_WIDTH;

	if ( col > space_col )
		s.append(col - space_col, ' ');

	return s;
	}

void AppendToken(const Node* node, std::string& head,
                 int& col, int& indent, int break_indent)
	{
	head += node->Text();
	col += static_cast<int>(node->Text().size());

	if ( node->MustBreakAfter() )
		{
		indent = break_indent;
		col = indent * INDENT_WIDTH;
		head += "\n" + LinePrefix(indent, col);
		}
	else
		{
		head += " ";
		++col;
		}
	}

// ------------------------------------------------------------------
// Pre-comment / pre-marker emission
// ------------------------------------------------------------------

static std::string EmitPreComments(const Node& node,
                                   const std::string& pad)
	{
	std::string result;

	for ( const auto& pc : node.PreComments() )
		result += pad + pc + "\n";

	for ( const auto& pm : node.PreMarkers() )
		if ( pm->GetTag() == Tag::Blank )
			result += "\n";

	return result;
	}

// ------------------------------------------------------------------
// Candidate comparison
// ------------------------------------------------------------------

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
	if ( Ovf() != o.Ovf() )
		return Ovf() < o.Ovf();
	if ( Lines() != o.Lines() )
		return Lines() < o.Lines();
	return Spread() < o.Spread();
	}

const Candidate& Best(const Candidates& cs)
	{
	assert(! cs.empty());
	const Candidate* best = &cs[0];

	for ( size_t i = 1; i < cs.size(); ++i )
		if ( cs[i].BetterThan(*best) )
			best = &cs[i];

	return *best;
	}

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

// How many columns a candidate overflows past the available context width,
// accounting for trailing reservation.
static int Ovf(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width() + ctx.Trail());
	}

// Overflow ignoring trailing reservation (for intermediate lines).
static int OvfNoTrail(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width());
	}

// Compute the starting column for content of width w so that it
// ends at max_col - 1 (the last usable column).  Prefers align_col
// but backs up when needed.
static int FitCol(int align_col, int w, int max_col)
	{
	if ( align_col + w <= max_col - 1 )
		return align_col;
	return max_col - 1 - w;
	}

// How many lines are needed to represent a string.
static int CountLines(const std::string& s)
	{
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

// Width of the last line in a (possibly multi-line) string, counting only
// characters (no tab expansion - candidates use spaces for alignment).
static int LastLineLen(const std::string& s)
	{
	auto n = s.size();
	auto pos = s.rfind('\n');
	if ( pos != std::string::npos )
		n -= (pos + 1);
	return static_cast<int>(n);
	}

// Compute total overflow across all lines in a multi-line string.
// start_col is the absolute column where the first line starts;
// subsequent lines start at column 0 (the padding is in the string).
static int TextOverflow(const std::string& text, int start_col,
                        int max_col)
	{
	int ovf = 0;
	int pos = 0;
	int line_start_col = start_col;

	for ( size_t j = 0; j < text.size(); ++j )
		if ( text[j] == '\n' )
			{
			int line_w = static_cast<int>(j) - pos +
				line_start_col;
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

// Forward declaration for mutual recursion.
static Candidates FormatExprStmt(const Node& node, const FmtContext& ctx);

// Forward declaration for dispatch table (used by FormatStmtList).
using FormatFunc = Candidates (*)(const Node&, const FmtContext&);
static const std::unordered_map<Tag, FormatFunc>& FormatDispatch();

// ------------------------------------------------------------------
// Atoms
// ------------------------------------------------------------------

static Candidates FormatAtom(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(), ctx)};
	}

// ------------------------------------------------------------------
// Field access: rec$field
// ------------------------------------------------------------------

static Candidates FormatFieldAccess(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.size() < 2 )
		throw FormatError("FIELD-ACCESS node needs 2 children");

	const Node* dollar = node.FindChild(Tag::Dollar);

	auto lhs_cs = FormatExpr(*content[0], ctx);
	const auto& lhs = Best(lhs_cs);

	int dw = dollar->Width();
	auto rhs_cs = FormatExpr(*content[1], ctx.After(lhs.Width() + dw));
	const auto& rhs = Best(rhs_cs);

	return {lhs.Cat(dollar->Text()).Cat(rhs).In(ctx)};
	}

// ------------------------------------------------------------------
// Field assign: $field=expr
// ------------------------------------------------------------------

static Candidates FormatFieldAssign(const Node& node, const FmtContext& ctx)
	{
	const Node* dollar = node.FindChild(Tag::Dollar);
	const Node* assign = node.FindChild(Tag::Assign);

	std::string prefix = dollar->Text() + node.Arg() + assign->Text();

	auto content = node.ContentChildren();
	if ( content.empty() )
		throw FormatError("FIELD-ASSIGN node needs a value child");

	int pw = static_cast<int>(prefix.size());
	auto val_cs = FormatExpr(*content[0], ctx.After(pw));
	const auto& val = Best(val_cs);

	return {Candidate(prefix + val.Text(), ctx)};
	}

// ------------------------------------------------------------------
// Arg lists with trailing comments
// ------------------------------------------------------------------

// An argument paired with its trailing comment (if any)
// and the comma token that precedes it (nullptr for first item).
struct ArgComment
	{
	const Node* arg;
	std::string comment;	// trailing: empty or " # ..."
	std::vector<std::string> leading;	// leading comments before item
	const Node* comma = nullptr;	// preceding COMMA token, if any
	};

// Scan a node's children, pairing each non-comment child with
// its trailing comment and any pre-comments (leading comments
// attached by the parser).
// Returns the items and whether any comments were found.
static std::pair<std::vector<ArgComment>, bool>
CollectArgs(const Node::NodeVec& children)
	{
	std::vector<ArgComment> items;
	bool has_comments = false;
	const Node* pending_comma = nullptr;

	for ( size_t i = 0; i < children.size(); ++i )
		{
		auto& c = children[i];
		Tag t = c->GetTag();

		if ( t == Tag::TrailingComma )
			continue;

		if ( is_token(t) )
			{
			if ( t == Tag::Comma )
				{
				pending_comma = c.get();
				if ( c->MustBreakAfter() )
					has_comments = true;
				}
			continue;
			}

		// Orphaned COMMENT-LEADING (end of block, no following
		// sibling) - still appears as a standalone child.
		if ( is_comment(t) )
			{
			has_comments = true;
			continue;
			}

		if ( c->MustBreakAfter() || c->MustBreakBefore() )
			has_comments = true;

		std::vector<std::string> leading(c->PreComments().begin(),
		                                 c->PreComments().end());

		items.push_back({c.get(), c->TrailingComment(),
		                 std::move(leading), pending_comma});
		pending_comma = nullptr;
		}

	return {items, has_comments};
	}

// ------------------------------------------------------------------
// Arg list formatting
// ------------------------------------------------------------------

// Format a comma-separated arg list on one line.
static Candidate FormatArgsFlat(const std::vector<ArgComment>& items,
				const FmtContext& ctx)
	{
	std::string text;
	int w = 0;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& it = items[i];

		if ( it.comma )
			{
			text += it.comma->Text() + " ";
			w += it.comma->Width() + 1;
			}

		auto cs = FormatExpr(*it.arg, ctx.After(w));
		const auto& best = Best(cs);
		text += best.Text();
		w += best.Width();
		}

	return {text, static_cast<int>(text.size()), 1, 0};
	}

// Append trailing material after an item in a fill layout.  Handles
// the item's own trailing comment and the next comma (which may carry
// a trailing comment that forces a wrap).
static void AppendTrailing(const ArgComment& it,
                           const Node* next_comma,
                           std::string& text, int& cur_col,
                           bool& force_wrap)
	{
	// The item's own trailing comment (rare for non-last items).
	if ( ! it.comment.empty() )
		{
		text += it.comment;
		cur_col += static_cast<int>(it.comment.size());
		force_wrap = true;
		}

	// The comma between this item and the next may carry a
	// trailing comment (e.g., "x, # note").
	if ( next_comma && next_comma->MustBreakAfter() )
		{
		text += next_comma->Text();
		cur_col += next_comma->Width();
		force_wrap = true;
		}
	}

// Format an arg at the current column, appending to text and
// updating position/lines/overflow.
static void FormatFillArg(const Node& arg, int indent, int max_col,
                           std::string& text, int& cur_col,
                           int& lines, int& total_overflow)
	{
	FmtContext sub(indent, cur_col, max_col - cur_col);
	auto cs = FormatExpr(arg, sub);
	const auto& best = Best(cs);
	text += best.Text();

	if ( best.Lines() > 1 )
		{
		lines += best.Lines() - 1;
		cur_col = LastLineLen(text);
		}
	else
		cur_col += best.Width();

	total_overflow += best.Ovf();
	}

// Greedy-fill args: pack as many per line as fit, wrapping to align_col.
// When an item has a trailing comment, the comma is placed before the
// comment and the next item is forced to a new line.
static Candidate FormatArgsFill(const std::vector<ArgComment>& items,
                                int align_col, int indent,
                                const FmtContext& first_line_ctx)
	{
	std::string pad = LinePrefix(indent, align_col);
	std::string text;

	int max_col = first_line_ctx.MaxCol();
	int cur_col = first_line_ctx.Col();
	int lines = 1;
	int total_overflow = 0;
	bool force_wrap = false;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& it = items[i];
		bool is_last = (i + 1 == items.size());
		const Node* nc = is_last ? nullptr : items[i + 1].comma;

		// Leading comments force a wrap and appear on their
		// own lines before the item.
		if ( ! it.leading.empty() )
			{
			if ( it.comma )
				{
				text += it.comma->Text();
				cur_col += it.comma->Width();
				}

			for ( const auto& lc : it.leading )
				{
				text += "\n" + pad + lc;
				++lines;
				}

			text += "\n" + pad;
			++lines;
			cur_col = align_col;
			force_wrap = false;

			FormatFillArg(*it.arg, indent, max_col,
			              text, cur_col, lines, total_overflow);
			AppendTrailing(it, nc, text, cur_col, force_wrap);
			continue;
			}

		FmtContext sub(indent, cur_col, max_col - cur_col);
		auto cs = FormatExpr(*it.arg, sub);
		const auto& best = Best(cs);
		int aw = best.Width();

		if ( i == 0 )
			{
			text += best.Text();
			cur_col += aw;
			}
		else if ( force_wrap )
			{
			text += "\n" + pad;
			cur_col = align_col;
			force_wrap = false;

			FormatFillArg(*it.arg, indent, max_col,
			              text, cur_col, lines, total_overflow);
			++lines;
			AppendTrailing(it, nc, text, cur_col, force_wrap);
			continue;
			}
		else
			{
			int need = 2 + aw;
			// Multi-line args always wrap to a fresh line where
			// they may fit flat at the alignment column.
			if ( best.Lines() > 1 )
				need = max_col + 1;
			if ( cur_col + need <= max_col )
				{
				text += it.comma->Text() + " " + best.Text();
				cur_col += need;
				}
			else
				{
				text += it.comma->Text() + "\n" + pad;
				cur_col = align_col;

				FormatFillArg(*it.arg, indent, max_col, text,
						cur_col, lines, total_overflow);
				++lines;
				AppendTrailing(it, nc, text, cur_col, force_wrap);
				continue;
				}
			}

		if ( best.Lines() > 1 )
			{
			lines += best.Lines() - 1;
			cur_col = LastLineLen(text);
			}

		total_overflow += best.Ovf();
		AppendTrailing(it, nc, text, cur_col, force_wrap);
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {text, cur_col, lines, total_overflow};
	}

// Try flat, then greedy-fill for a bracketed list of items.
// prefix: text before the open bracket (e.g. "func_name" or "table")
// open/close: bracket characters
// suffix: text after close bracket (e.g. " of type", usually empty)
static Candidates FlatOrFill(const std::string& prefix,
                             const std::string& open,
                             const std::string& close,
                             const std::string& suffix,
                             const std::vector<ArgComment>& items,
                             bool has_comments,
                             const FmtContext& ctx,
                             const std::string& open_comment = "",
                             const std::string& close_prefix = "")
	{
	int prefix_w = static_cast<int>(prefix.size());
	int open_w = static_cast<int>(open.size());
	int close_w = static_cast<int>(close.size());
	int close_extra = static_cast<int>(close_prefix.size());
	int suffix_w = static_cast<int>(suffix.size());
	int open_col = ctx.Col() + prefix_w + open_w;
	int inner_w = ctx.MaxCol() - open_col - close_extra
		- close_w - suffix_w;
	FmtContext inner_ctx(ctx.Indent(), open_col, inner_w);

	std::string cb = close_prefix + close;

	Candidates result;

	// Try flat (only when no comments - a trailing comment
	// forces a line break, so flat would lose it).
	if ( ! has_comments )
		{
		auto flat_args = FormatArgsFlat(items, inner_ctx);
		std::string flat_text = prefix + open + flat_args.Text() +
			cb + suffix;
		Candidate flat_c(flat_text, ctx);
		result.push_back(flat_c);

		if ( flat_c.Ovf() == 0 || items.size() <= 1 )
			return result;
		}

	// Greedy-fill: pack as many items per line as fit.
	// When there's a comment after the open bracket, it occupies
	// the first line and args start on the next line.
	std::string fill_prefix;
	if ( ! open_comment.empty() )
		{
		std::string pad = LinePrefix(ctx.Indent(), open_col);
		fill_prefix = open_comment + "\n" + pad;
		}

	auto fill = FormatArgsFill(items, open_col, ctx.Indent(), inner_ctx);
	std::string fill_text = prefix + open + fill_prefix +
		fill.Text() + cb + suffix;
	int flast_w = fill.Width() + close_extra + close_w + suffix_w;
	int fill_lines = fill.Lines() + (open_comment.empty() ? 0 : 1);
	result.push_back({fill_text, flast_w, fill_lines,
	                  fill.Ovf(), ctx.Col()});

	return result;
	}

// Format items one-per-line with indented body: open\n\titem,\n\titem\nclose.
static Candidate FormatArgsVertical(const std::string& open,
                                    const std::string& close,
                                    const std::vector<ArgComment>& items,
                                    const FmtContext& ctx,
                                    bool trailing_comma = false)
	{
	int body_indent = ctx.Indent() + 1;
	int body_col = body_indent * INDENT_WIDTH;
	FmtContext body_ctx(body_indent, body_col, ctx.MaxCol() - body_col);
	std::string body_pad = LinePrefix(body_indent, body_col);
	std::string close_pad = LinePrefix(ctx.Indent(), ctx.IndentCol());

	std::string text = open;
	int lines = 1;
	int ovf = 0;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		text += "\n" + body_pad;
		++lines;

		auto& it = items[i];
		auto cs = FormatExpr(*it.arg, body_ctx);
		const auto& best = Best(cs);
		text += best.Text();

		int line_w = body_col + best.Width();

		const Node* nc = (i + 1 < items.size())
			? items[i + 1].comma : nullptr;
		if ( nc || trailing_comma )
			{
			std::string ct = nc ? nc->Text() : ",";
			text += ct;
			line_w += static_cast<int>(ct.size());
			}

		text += it.comment;
		line_w += static_cast<int>(it.comment.size());

		if ( line_w > ctx.MaxCol() )
			ovf += line_w - ctx.MaxCol();
		}

	text += "\n" + close_pad + close;
	++lines;

	int last_w = ctx.IndentCol() + static_cast<int>(close.size());
	return {text, last_w, lines, ovf, ctx.Col()};
	}

// ------------------------------------------------------------------
// Call: func(args)
// ------------------------------------------------------------------

static Candidates FormatCall(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.empty() )
		throw FormatError("CALL node needs children");

	auto func_cs = FormatExpr(*content[0], ctx);
	const auto& func = Best(func_cs);

	const Node* args_node = node.FindChild(Tag::Args);

	if ( ! args_node )
		return {func.Cat("()").In(ctx)};

	const Node* lp = args_node->FindChild(Tag::LParen);
	const Node* rp = args_node->FindChild(Tag::RParen);

	auto [items, has_comments] = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {func.Cat(lp->Text() + rp->Text()).In(ctx)};

	// Trailing comment on ARGS acts as an open-bracket comment
	// (goes after the open paren, with args on the next line).
	if ( ! args_node->TrailingComment().empty() )
		has_comments = true;

	return FlatOrFill(func.Text(), lp->Text(), rp->Text(), "",
		items, has_comments, ctx,
		args_node->TrailingComment());
	}

// ------------------------------------------------------------------
// Schedule: schedule interval { event() }
// ------------------------------------------------------------------

static Candidates FormatSchedule(const Node& node, const FmtContext& ctx)
	{
	const Node* kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	auto content = node.ContentChildren();
	if ( content.size() < 2 )
		throw FormatError("SCHEDULE node needs 2 content children");

	std::string prefix = kw->Text() + " ";
	int pw = static_cast<int>(prefix.size());

	// First content child: interval expression.
	auto interval_cs = FormatExpr(*content[0], ctx.After(pw));
	const auto& interval = Best(interval_cs);

	// Second content child: event call.
	std::string mid = " " + lb->Text() + " ";
	int after_brace = pw + interval.Width() +
		static_cast<int>(mid.size());
	auto event_cs = FormatExpr(*content[1], ctx.After(after_brace));
	const auto& event = Best(event_cs);

	std::string text = prefix + interval.Text() + mid +
		event.Text() + " " + rb->Text();
	return {Candidate(text, ctx)};
	}

// ------------------------------------------------------------------
// Constructor: table(...), set(...), vector(...)
// ------------------------------------------------------------------

static Candidates FormatConstructor(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* lp_node = node.FindChild(Tag::LParen);
	const Node* rp_node = node.FindChild(Tag::RParen);
	std::string kw = kw_node->Text();
	std::string lp = lp_node->Text();
	std::string rp = rp_node->Text();

	auto [items, has_comments] = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate(kw + lp + rp, ctx)};

	Candidates result;

	// Candidate 1: flat (only when no comments).
	if ( ! has_comments )
		{
		int open_w = static_cast<int>(kw.size() + lp.size());
		FmtContext args_ctx(ctx.Indent(), ctx.Col() + open_w,
		                    ctx.Width() - open_w
		                    - static_cast<int>(rp.size()));
		auto flat_args = FormatArgsFlat(items, args_ctx);
		auto flat_c = Candidate(kw + lp + flat_args.Text() + rp,
		                        ctx);
		result.push_back(flat_c);

		if ( flat_c.Ovf() == 0 )
			return result;
		}

	// Candidate 2: one element per line, indented body.
	result.push_back(FormatArgsVertical(kw + lp, rp,
	                                    items, ctx));

	return result;
	}

// ------------------------------------------------------------------
// Index: expr[subscripts]
// ------------------------------------------------------------------

static Candidates FormatIndex(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.empty() )
		throw FormatError("INDEX node needs children");

	auto base_cs = FormatExpr(*content[0], ctx);
	const auto& base = Best(base_cs);

	const Node* subs_node = node.FindChild(Tag::Subscripts);
	if ( ! subs_node )
		return {base.Cat("[]").In(ctx)};

	const Node* lb = subs_node->FindChild(Tag::LBracket);
	const Node* rb = subs_node->FindChild(Tag::RBracket);

	auto subs_content = subs_node->ContentChildren();
	if ( subs_content.empty() )
		return {base.Cat(lb->Text() + rb->Text()).In(ctx)};

	if ( subs_content.size() == 1 )
		{
		int lb_w = lb->Width();
		int rb_w = rb->Width();
		int sub_col = ctx.Col() + base.Width() + lb_w;
		FmtContext bracket_ctx(ctx.Indent(), sub_col,
			ctx.Width() - base.Width() - lb_w - rb_w);
		auto sub_cs = FormatExpr(*subs_content[0], bracket_ctx);
		const auto& sub = Best(sub_cs);
		return {base.Cat(lb->Text()).Cat(sub)
			.Cat(rb->Text()).In(ctx)};
		}

	// Multiple subscripts: format as comma-separated list.
	auto [items, has_comments] = CollectArgs(subs_node->Children());
	return FlatOrFill(base.Text(), lb->Text(), rb->Text(), "",
		items, has_comments, ctx);
	}

// ------------------------------------------------------------------
// Index literal: [$field=expr, ...]
// ------------------------------------------------------------------

static Candidates FormatIndexLiteral(const Node& node, const FmtContext& ctx)
	{
	const Node* lb = node.FindChild(Tag::LBracket);
	const Node* rb = node.FindChild(Tag::RBracket);
	std::string lbt = lb->Text();
	std::string rbt = rb->Text();

	auto [items, has_comments] = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate(lbt + rbt, ctx)};

	bool has_trailing_comma =
		node.FindOptChild(Tag::TrailingComma) != nullptr;
	std::string close_pfx = has_trailing_comma ? ", " : "";

	// When every item has a trailing comment, use vertical indented
	// layout (each item on its own line).  Otherwise use fill, which
	// packs items and wraps after any trailing comment.
	if ( has_comments )
		{
		bool all_trailing = true;
		for ( size_t i = 0; i < items.size(); ++i )
			{
			auto& it = items[i];
			const Node* nc = (i + 1 < items.size())
				? items[i + 1].comma : nullptr;
			bool has_trail = ! it.comment.empty() ||
				(nc && nc->MustBreakAfter());
			if ( ! has_trail )
				all_trailing = false;
			}

		if ( all_trailing )
			return {FormatArgsVertical(lbt, rbt, items, ctx,
				has_trailing_comma)};
		}

	return FlatOrFill("", lbt, rbt, "", items, has_comments, ctx,
		"", close_pfx);
	}

// ------------------------------------------------------------------
// Slice: expr[lo:hi]
// ------------------------------------------------------------------

static Candidates FormatSlice(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.size() < 3 )
		throw FormatError("SLICE node needs 3 content children");

	const Node* lb = node.FindChild(Tag::LBracket);
	const Node* rb = node.FindChild(Tag::RBracket);
	const Node* colon = node.FindChild(Tag::Colon);
	std::string lbt = lb->Text();
	std::string rbt = rb->Text();

	auto base_cs = FormatExpr(*content[0], ctx);
	const auto& base = Best(base_cs);

	std::string lo = Best(FormatExpr(*content[1], ctx)).Text();
	std::string hi = Best(FormatExpr(*content[2], ctx)).Text();

	std::string sep = (! lo.empty() && ! hi.empty())
		? " " + colon->Text() + " " : colon->Text();
	Candidate flat = base.Cat(lbt + lo + sep + hi + rbt).In(ctx);

	if ( flat.Fits() || lo.empty() || hi.empty() )
		return {flat};

	// Split after ":" - continuation aligns after "[".
	int bracket_col = ctx.Col() + base.Width() + lb->Width();
	FmtContext hi_ctx = ctx.AtCol(bracket_col);
	std::string hi2 = Best(FormatExpr(*content[2], hi_ctx)).Text();

	std::string prefix = LinePrefix(hi_ctx.Indent(), bracket_col);
	std::string split = base.Text() + lbt + lo + " " +
		colon->Text() + "\n" + prefix + hi2 + rbt;
	int last_w = static_cast<int>(hi2.size()) + rb->Width();
	int split_ovf = Ovf(last_w, hi_ctx);
	int lines = 1 + CountLines(hi2);

	return {flat, {split, last_w, lines, split_ovf, ctx.Col()}};
	}

// ------------------------------------------------------------------
// Paren: (expr)
// ------------------------------------------------------------------

static Candidates FormatParen(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.empty() )
		throw FormatError("PAREN node needs a child");

	const Node* lp = node.FindChild(Tag::LParen);
	const Node* rp = node.FindChild(Tag::RParen);

	auto inner_cs = FormatExpr(*content[0],
		ctx.After(lp->Width()));
	const auto& inner = Best(inner_cs);

	return {Candidate(lp->Text(), ctx).Cat(inner)
		.Cat(rp->Text()).In(ctx)};
	}

// ------------------------------------------------------------------
// Unary: ! expr, -expr, ~expr
// ------------------------------------------------------------------

static Candidates FormatUnary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	auto content = node.ContentChildren();

	if ( content.empty() )
		throw FormatError("UNARY-OP node needs a child");

	// Cardinality/absolute value: |expr|
	if ( op == "|...|" )
		{
		auto operand_cs = FormatExpr(*content[0], ctx.After(2));
		const auto& operand = Best(operand_cs);
		return {Candidate("|", ctx).Cat(operand).Cat("|").In(ctx)};
		}

	// Zeek style: space after "!".
	std::string ps = op;
	if ( op == "!" )
		ps += " ";

	Candidate prefix(ps, ctx);
	auto operand_cs = FormatExpr(*content[0], ctx.After(prefix.Width()));
	const auto& operand = Best(operand_cs);

	return {prefix.Cat(operand).In(ctx)};
	}

// ------------------------------------------------------------------
// Boolean chain: flatten left-associative && or || into operand list,
// then pack with fill layout breaking at the boolean operator.
// ------------------------------------------------------------------

// Collect operands of a left-associative chain of the same boolean op.
static void FlattenBoolChain(const Node& node, const std::string& op,
                             std::vector<const Node*>& out)
	{
	auto content = node.ContentChildren();
	if ( node.GetTag() == Tag::BinaryOp && node.Arg() == op &&
	     content.size() >= 2 )
		{
		FlattenBoolChain(*content[0], op, out);
		out.push_back(content[1]);
		}
	else
		out.push_back(&node);
	}

static Candidates FormatBoolChain(const std::string& op,
                                  const std::vector<const Node*>& operands,
                                  const FmtContext& ctx)
	{
	std::string sep = " " + op + " ";
	int sep_w = static_cast<int>(sep.size());

	// Try flat.  Only use when every operand fits on one line.
	std::string flat;
	int flat_w = 0;
	bool any_multiline = false;
	for ( size_t i = 0; i < operands.size(); ++i )
		{
		auto cs = FormatExpr(*operands[i], ctx.After(flat_w));
		const auto& best = Best(cs);
		if ( best.Lines() > 1 )
			any_multiline = true;
		if ( i > 0 )
			{
			flat += sep;
			flat_w += sep_w;
			}
		flat += best.Text();
		flat_w += best.Width();
		}

	int flat_ovf = Ovf(flat_w, ctx);
	if ( flat_ovf == 0 && ! any_multiline )
		return {Candidate(flat, ctx)};

	// Fill: pack operands with " op " separator, wrap at operator.
	FmtContext cont_ctx = ctx.Col() == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(ctx.Col());
	std::string pad = LinePrefix(cont_ctx.Indent(), cont_ctx.Col());
	int max_col = ctx.MaxCol() - ctx.Trail();

	std::string text;
	int cur_col = ctx.Col();
	int lines = 1;
	int total_overflow = 0;

	for ( size_t i = 0; i < operands.size(); ++i )
		{
		FmtContext sub(cont_ctx.Indent(), cur_col,
			max_col - cur_col);
		auto cs = FormatExpr(*operands[i], sub);
		const auto& best = Best(cs);
		int w = best.Width();

		if ( i == 0 )
			{
			text += best.Text();
			cur_col += w;
			}
		else
			{
			int need = sep_w + w;
			if ( best.Lines() > 1 )
				need = max_col + 1;
			if ( cur_col + need <= max_col )
				{
				text += sep + best.Text();
				cur_col += need;
				}
			else
				{
				text += " " + op + "\n" + pad;
				cur_col = cont_ctx.Col();
				++lines;

				FmtContext wrap_sub(cont_ctx.Indent(),
					cur_col, max_col - cur_col);
				auto wcs = FormatExpr(*operands[i], wrap_sub);
				const auto& wb = Best(wcs);
				text += wb.Text();
				cur_col += wb.Width();
				total_overflow += wb.Ovf();
				}
			}

		if ( best.Lines() > 1 )
			{
			lines += best.Lines() - 1;
			cur_col = LastLineLen(text);
			}

		total_overflow += best.Ovf();
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {{text, cur_col, lines, total_overflow, ctx.Col()}};
	}

// ------------------------------------------------------------------
// Binary: lhs op rhs
// ------------------------------------------------------------------

static Candidates FormatBinary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	auto content = node.ContentChildren();

	if ( content.size() < 2 )
		throw FormatError("BINARY-OP node needs 2 children");

	// Boolean chains: flatten and fill-pack at && or ||.
	if ( op == "&&" || op == "||" )
		{
		std::vector<const Node*> operands;
		FlattenBoolChain(node, op, operands);
		return FormatBoolChain(op, operands, ctx);
		}

	// ?$ binds without spaces, like field access.
	// Reserve trail space so the LHS splits to leave room.
	if ( op == "?$" )
		{
		std::string rhs_text =
			Best(FormatExpr(*content[1], ctx)).Text();
		int suffix_w = 2 + static_cast<int>(rhs_text.size());

		auto lhs_cs = FormatExpr(*content[0], ctx.Reserve(suffix_w));
		const auto& lhs = Best(lhs_cs);

		std::string text = lhs.Text() + "?$" + rhs_text;

		if ( lhs.Lines() > 1 )
			{
			int last_w = lhs.Width() + suffix_w;
			return {{text, last_w, lhs.Lines(), lhs.Ovf(),
			         ctx.Col()}};
			}

		return {Candidate(text, ctx)};
		}

	// "/" with atomic RHS: no spaces (subnet masking heuristic).
	// Division typically has a compound RHS; masking has a bare
	// constant or identifier.
	bool tight = (op == "/" && ! content[1]->HasChildren());
	std::string lsep = tight ? "" : " ";
	std::string rsep = tight ? "" : " ";
	int op_w = static_cast<int>(op.size()) + (tight ? 0 : 2);

	auto lhs_cs = FormatExpr(*content[0], ctx);
	const auto& lhs = Best(lhs_cs);

	auto rhs_cs = FormatExpr(*content[1], ctx.After(lhs.Width() + op_w));
	const auto& rhs = Best(rhs_cs);

	// Candidate 1: flat - lhs op rhs
	std::string flat = lhs.Text() + lsep + op + rsep + rhs.Text();
	int flat_w = lhs.Width() + op_w + rhs.Width();
	int flat_ovf = Ovf(flat_w, ctx);
	bool need_split = flat_ovf > 0;

	Candidates result;

	if ( lhs.Lines() > 1 || rhs.Lines() > 1 )
		{
		// One side is multi-line.  Compute overflow from the
		// widest line, not just the last.
		int last_w = LastLineLen(flat);
		int lines = CountLines(flat);
		int ovf = TextOverflow(flat, ctx.Col(), ctx.MaxCol());

		result.push_back({flat, last_w, lines, ovf, ctx.Col()});

		if ( ovf > 0 )
			need_split = true;
		}
	else
		result.push_back({flat, flat_w, 1, flat_ovf});

	if ( ! need_split )
		return result;

	// Split after operator.  The continuation column depends on
	// where the expression starts relative to the indent column:
	// - At the indent column: indent one more level (the natural
	//   "next level" continuation).
	// - Past the indent column: align to the expression start
	//   (the principled continuation point for a sub-expression).
	FmtContext cont_ctx = ctx.Col() == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(ctx.Col());

	auto rhs2_cs = FormatExpr(*content[1], cont_ctx);
	const auto& rhs2 = Best(rhs2_cs);

	std::string cont_prefix = LinePrefix(cont_ctx.Indent(), cont_ctx.Col());

	std::string split = lhs.Text() + lsep + op + "\n" +
				cont_prefix + rhs2.Text();
	int line1_w = lhs.Width() + static_cast<int>(lsep.size()) +
		static_cast<int>(op.size());
	int line2_ovf = Ovf(rhs2.Width(), cont_ctx);
	int split_ovf = OvfNoTrail(line1_w, ctx) + line2_ovf;

	int split_lines = 1 + rhs2.Lines();
	int last_w = rhs2.Lines() > 1 ? LastLineLen(split) : rhs2.Width();

	result.push_back({split, last_w, split_lines, split_ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Interval: 1 sec, 3.5 hrs
// ------------------------------------------------------------------

static Candidates FormatInterval(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(0) + " " + node.Arg(1), ctx)};
	}

// ------------------------------------------------------------------
// Types
// ------------------------------------------------------------------

// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t
// Children now include LBRACKET, RBRACKET, COMMA, KEYWORD "of".
static Candidates FormatTypeParam(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "table", "set", "vector"

	const Node* lb = node.FindOptChild(Tag::LBracket);
	const Node* rb = node.FindOptChild(Tag::RBracket);
	const Node* of_kw = node.FindOptChild(Tag::Keyword);

	// Collect bracketed type args (between LBRACKET/RBRACKET)
	// and "of" type (after KEYWORD "of").
	std::vector<ArgComment> bt_items;
	const Node* of_type = nullptr;
	const Node* pending_comma = nullptr;
	bool in_brackets = false;
	bool past_of = false;

	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBracket )
			{ in_brackets = true; continue; }
		if ( t == Tag::RBracket )
			{ in_brackets = false; continue; }
		if ( t == Tag::Keyword )
			{ past_of = true; continue; }
		if ( t == Tag::Comma )
			{ pending_comma = c.get(); continue; }
		if ( is_token(t) || is_comment(t) )
			continue;

		if ( past_of )
			of_type = c.get();
		else if ( in_brackets )
			{
			bt_items.push_back({c.get(), "", {}, pending_comma});
			pending_comma = nullptr;
			}
		}

	std::string suffix;

	if ( of_type )
		suffix = " " + of_kw->Text() + " " +
			Best(FormatExpr(*of_type, ctx)).Text();

	if ( bt_items.empty() )
		return {Candidate(keyword + suffix, ctx)};

	return FlatOrFill(keyword, lb->Text(), rb->Text(), suffix,
		bt_items, false, ctx);
	}

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
static const Node* FindTypeChild(const Node& node)
	{
	for ( const auto& c : node.Children() )
		if ( is_type_tag(c->GetTag()) )
			return c.get();
	return nullptr;
	}

// TYPE-FUNC: event(params), function(params): rettype
// PARAMS now has LPAREN/RPAREN, COLON before RETURNS.
static Candidates FormatTypeFunc(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "event", "function", "hook"

	const Node* params = node.FindChild(Tag::Params);
	const Node* returns = node.FindOptChild(Tag::Returns);

	const Node* lp = params->FindChild(Tag::LParen);
	const Node* rp = params->FindChild(Tag::RParen);
	std::string text = keyword + lp->Text();

	for ( const auto& p : params->Children() )
		{
		Tag t = p->GetTag();

		if ( t == Tag::Comma )
			{
			text += p->Text() + " ";
			continue;
			}

		if ( is_comment(t) || is_token(t) )
			continue;
		if ( t != Tag::Param )
			continue;

		text += p->Arg();
		const Node* ptype = FindTypeChild(*p);
		if ( ptype )
			{
			const Node* pcol = p->FindChild(Tag::Colon);
			text += pcol->Text() + " " +
				Best(FormatExpr(*ptype, ctx)).Text();
			}
		}

	text += rp->Text();

	if ( returns )
		{
		const Node* colon = node.FindChild(Tag::Colon);
		const Node* rt = FindTypeChild(*returns);
		if ( rt )
			text += colon->Text() + " " +
				Best(FormatExpr(*rt, ctx)).Text();
		}

	return {Candidate(text, ctx)};
	}

// ------------------------------------------------------------------
// Attributes: &redef, &default=expr
// ------------------------------------------------------------------

// Check whether any attr value in an ATTR-LIST contains blanks.
// If so, all attrs use " = " spacing; otherwise "=".
static bool AttrListNeedsSpaces(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		// Find the value expression (first non-token child).
		const Node* val = nullptr;
		for ( const auto& c : attr->Children() )
			if ( ! is_token(c->GetTag()) )
				{ val = c.get(); break; }
		if ( ! val )
			continue;

		auto val_cs = FormatExpr(*val, ctx);
		if ( Best(val_cs).Text().find(' ') != std::string::npos )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static std::string FormatOneAttr(const Node& attr, bool spaced,
                                 const FmtContext& ctx)
	{
	std::string text = attr.Arg();

	const Node* eq = attr.FindOptChild(Tag::Assign);
	if ( eq )
		{
		const Node* val = nullptr;
		for ( const auto& c : attr.Children() )
			if ( ! is_token(c->GetTag()) )
				{ val = c.get(); break; }

		std::string sep = spaced ? " " : "";
		text += sep + eq->Text() + sep;
		if ( val )
			text += Best(FormatExpr(*val, ctx)).Text();
		}

	return text;
	}

// Format all attrs as a single flat string: "&a=x &b".
static std::string FormatAttrList(const Node& node, const FmtContext& ctx)
	{
	bool spaced = AttrListNeedsSpaces(node, ctx);
	std::string text;

	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		if ( ! text.empty() )
			text += " ";

		text += FormatOneAttr(*attr, spaced, ctx);
		}

	return text;
	}

// Collect individual attr strings.
static std::vector<std::string> FormatAttrStrings(const Node& node,
                                                  const FmtContext& ctx)
	{
	bool spaced = AttrListNeedsSpaces(node, ctx);
	std::vector<std::string> result;

	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		result.push_back(FormatOneAttr(*attr, spaced, ctx));
		}

	return result;
	}

// ------------------------------------------------------------------
// Declarations: global/local/const/redef name [: type] [= init] [attrs] ;
// ------------------------------------------------------------------

// Build the suffix: attrs + optional semicolon.
static std::string DeclSuffix(const Node* attrs_node,
                              const Node* semi_node,
                              const FmtContext& ctx)
	{
	std::string suffix;

	if ( attrs_node )
		{
		std::string as = FormatAttrList(*attrs_node, ctx);
		if ( ! as.empty() )
			suffix += " " + as;
		}

	if ( semi_node )
		suffix += semi_node->Text();

	return suffix;
	}

// Shared state for declaration candidate generation.
struct DeclParts {
	std::string head;	// "global foo", "local bar", etc.
	std::string type_str;	// ": type" or ""
	std::string suffix;	// " &attr1 &attr2;" or ";" or ""
	const Node* type_node;	// direct type child (after COLON)
	const Node* colon_node;	// COLON before type
	std::string assign_op;	// "=", "+=", or ""
	const Node* init_val;	// direct init value (after ASSIGN)
	const Node* attrs_node;
	const Node* semi_node;
};

// Flat candidate + split-after-init for declarations with initializers.
static void DeclWithInit(const DeclParts& d, Candidates& result,
                         const FmtContext& ctx)
	{
	std::string before_val = d.head + d.type_str + " " +
		d.assign_op + " ";
	int before_w = static_cast<int>(before_val.size());
	int suffix_w = static_cast<int>(d.suffix.size());

	FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
	auto val_cs = FormatExpr(*d.init_val, val_ctx);
	const auto& val = Best(val_cs);

	std::string flat = before_val + val.Text() + d.suffix;

	if ( val.Lines() > 1 )
		{
		int last_w = LastLineLen(flat);
		int lines = CountLines(flat);

		auto nl = val.Text().find('\n');
		int first_val_w = (nl != std::string::npos) ?
				static_cast<int>(nl) : val.Width();
		int ovf = OvfNoTrail(before_w + first_val_w, ctx)
			+ val.Ovf();

		if ( last_w > ctx.MaxCol() )
			ovf += last_w - ctx.MaxCol();

		result.push_back({flat, last_w, lines, ovf, ctx.Col()});
		}
	else
		result.push_back(Candidate(flat, ctx));

	// Split after init operator.
	if ( result[0].Ovf() > 0 )
		{
		FmtContext cont = ctx.Indented().Reserve(suffix_w);
		auto val2_cs = FormatExpr(*d.init_val, cont);
		const auto& val2 = Best(val2_cs);

		std::string line1 = d.head + d.type_str + " " +
			d.assign_op;
		std::string pad = LinePrefix(cont.Indent(), cont.Col());
		std::string split = line1 + "\n" + pad +
					val2.Text() + d.suffix;
		int last_w = LastLineLen(split);
		int lines = CountLines(split);
		int ovf = OvfNoTrail(static_cast<int>(line1.size()), ctx)
			+ Ovf(last_w, ctx);
		result.push_back({split, last_w, lines, ovf, ctx.Col()});
		}
	}

// Flat candidate + type-on-continuation for declarations without initializers.
static void DeclNoInit(const DeclParts& d, Candidates& result,
                       const FmtContext& ctx)
	{
	std::string flat = d.head + d.type_str + d.suffix;
	result.push_back(Candidate(flat, ctx));

	// Split after ":" when head + type overflows.
	int head_type_w = static_cast<int>(
		(d.head + d.type_str).size());
	if ( head_type_w <= ctx.MaxCol() || d.type_str.empty() )
		return;

	FmtContext cont = ctx.Indented();
	std::string tv = Best(FormatExpr(*d.type_node, cont)).Text();

	std::string line1 = d.head + d.colon_node->Text();
	std::string pad = LinePrefix(cont.Indent(), cont.Col());

	// Try type + suffix on one continuation line.
	std::string oneline = tv + d.suffix;
	if ( cont.Col() + static_cast<int>(oneline.size()) <=
	     ctx.MaxCol() )
		{
		std::string split = line1 + "\n" + pad + oneline;
		int last_w = LastLineLen(split);
		result.push_back({split, last_w, CountLines(split),
			Ovf(last_w, ctx), ctx.Col()});
		return;
		}

	// Type alone, attrs on separate lines.
	std::string semi_str = d.semi_node ? d.semi_node->Text() : "";
	std::string type_suffix = d.attrs_node ? "" : d.suffix;
	std::string split = line1 + "\n" + pad + tv + type_suffix;

	if ( d.attrs_node )
		{
		std::string apad = LinePrefix(cont.Indent(),
			cont.Col() + 1);
		auto astrs = FormatAttrStrings(*d.attrs_node, ctx);
		for ( const auto& a : astrs )
			split += "\n" + apad + a;
		split += semi_str;
		}

	int last_w = LastLineLen(split);
	result.push_back({split, last_w, CountLines(split),
		Ovf(last_w, ctx), ctx.Col()});
	}

// Attrs on continuation lines, type stays on first line.
static void DeclWrappedAttrs(const DeclParts& d, Candidates& result,
                             const FmtContext& ctx)
	{
	if ( ! d.attrs_node || d.type_str.empty() )
		return;

	auto attr_strs = FormatAttrStrings(*d.attrs_node, ctx);
	if ( attr_strs.empty() )
		return;

	// First line: everything except attrs and semi.
	std::string line1 = d.head + d.type_str;

	if ( d.init_val )
		{
		FmtContext val_ctx = ctx.After(
			static_cast<int>(line1.size()) +
			static_cast<int>(d.assign_op.size()) + 2);
		auto vcs = FormatExpr(*d.init_val, val_ctx);
		line1 += " " + d.assign_op + " " + Best(vcs).Text();
		}

	// Attrs aligned one column past where the type starts.
	int attr_col = static_cast<int>(d.head.size()) + 3;
	std::string attr_pad = LinePrefix(ctx.Indent(), attr_col);
	int max_col = ctx.MaxCol();
	int semi_w = d.semi_node ? d.semi_node->Width() : 0;

	// Check if all attrs fit on one continuation line.
	std::string all_attrs;
	for ( size_t i = 0; i < attr_strs.size(); ++i )
		{
		if ( i > 0 )
			all_attrs += " ";
		all_attrs += attr_strs[i];
		}

	std::string wrapped = line1;
	int ovf = OvfNoTrail(static_cast<int>(line1.size()), ctx);

	if ( attr_col + static_cast<int>(all_attrs.size()) +
	     semi_w <= max_col )
		{
		wrapped += "\n" + attr_pad + all_attrs;
		int aw = attr_col +
			static_cast<int>(all_attrs.size()) + semi_w;
		if ( aw > max_col )
			ovf += aw - max_col;
		}
	else
		{
		for ( size_t i = 0; i < attr_strs.size(); ++i )
			{
			wrapped += "\n" + attr_pad + attr_strs[i];
			int aw = attr_col +
				static_cast<int>(attr_strs[i].size());
			if ( i + 1 == attr_strs.size() )
				aw += semi_w;
			if ( aw > max_col )
				ovf += aw - max_col;
			}
		}

	if ( d.semi_node )
		wrapped += d.semi_node->Text();

	int last_w = LastLineLen(wrapped);
	int lines = CountLines(wrapped);
	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});
	}

// Split after colon: type (and optional init) on indented continuation.
static void DeclTypeSplit(const DeclParts& d, Candidates& result,
                          const FmtContext& ctx)
	{
	if ( d.type_str.empty() )
		return;

	FmtContext cont = ctx.Indented();
	std::string bare_type = d.type_str.substr(2);

	std::string line1 = d.head + d.colon_node->Text();
	std::string pad = LinePrefix(cont.Indent(), cont.Col());
	std::string split = line1 + "\n" + pad + bare_type;

	if ( d.init_val )
		{
		int suffix_w = static_cast<int>(d.suffix.size());
		FmtContext val_ctx = cont.After(
			static_cast<int>(bare_type.size()) +
			static_cast<int>(d.assign_op.size()) + 2).Reserve(
			suffix_w);
		auto val_cs = FormatExpr(*d.init_val, val_ctx);
		split += " " + d.assign_op + " " + Best(val_cs).Text();
		}

	split += d.suffix;

	int last_w = LastLineLen(split);
	int lines = CountLines(split);
	int ovf = OvfNoTrail(static_cast<int>(line1.size()), ctx) +
				Ovf(last_w, ctx);
	result.push_back({split, last_w, lines, ovf, ctx.Col()});
	}

static Candidates FormatDecl(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* id_node = node.FindChild(Tag::Identifier);

	DeclParts d;
	d.head = kw_node->Text() + " " + id_node->Text();
	d.type_node = nullptr;
	d.colon_node = nullptr;
	d.init_val = nullptr;
	d.attrs_node = node.FindOptChild(Tag::AttrList);
	d.semi_node = node.FindOptChild(Tag::Semi);

	// Scan children sequentially: COLON precedes type, ASSIGN
	// precedes init value.  Skip the head keyword and name nodes.
	bool expect_type = false;
	bool expect_init = false;

	for ( const auto& c : node.Children() )
		{
		if ( c.get() == kw_node || c.get() == id_node )
			continue;

		Tag t = c->GetTag();

		if ( t == Tag::Colon )
			{
			d.colon_node = c.get();
			expect_type = true;
			continue;
			}

		if ( t == Tag::Assign )
			{
			d.assign_op = c->Arg();
			expect_init = true;
			continue;
			}

		if ( expect_type && ! is_token(t) && ! is_comment(t) )
			{
			d.type_node = c.get();
			expect_type = false;
			continue;
			}

		if ( expect_init && ! is_token(t) && ! is_comment(t) )
			{
			d.init_val = c.get();
			expect_init = false;
			continue;
			}
		}

	if ( d.type_node )
		{
		std::string ts = Best(FormatExpr(*d.type_node, ctx)).Text();
		if ( ! ts.empty() )
			d.type_str = d.colon_node->Text() + " " + ts;
		}

	d.suffix = DeclSuffix(d.attrs_node, d.semi_node, ctx);

	Candidates result;

	if ( d.init_val )
		DeclWithInit(d, result, ctx);
	else
		DeclNoInit(d, result, ctx);

	if ( result[0].Ovf() > 0 )
		{
		DeclWrappedAttrs(d, result, ctx);
		DeclTypeSplit(d, result, ctx);
		}

	return result;
	}

// ------------------------------------------------------------------
// Ternary: cond ? true_val : false_val
// ------------------------------------------------------------------

static Candidates FormatTernary(const Node& node, const FmtContext& ctx)
	{
	auto content = node.ContentChildren();
	if ( content.size() < 3 )
		throw FormatError("TERNARY node needs 3 children");

	const Node* q = node.FindChild(Tag::Question);
	const Node* col = node.FindChild(Tag::Colon);
	std::string qs = " " + q->Text() + " ";
	std::string cs = " " + col->Text() + " ";
	int qw = static_cast<int>(qs.size());
	int cw = static_cast<int>(cs.size());

	auto cond_cs = FormatExpr(*content[0], ctx);
	const auto& cond = Best(cond_cs);

	int tv_col = ctx.Col() + cond.Width() + qw;
	auto tv_cs = FormatExpr(*content[1],
		ctx.After(cond.Width() + qw));
	const auto& tv = Best(tv_cs);

	auto fv_cs = FormatExpr(*content[2],
		ctx.After(cond.Width() + qw + tv.Width() + cw));
	const auto& fv = Best(fv_cs);

	std::string flat = cond.Text() + qs + tv.Text() + cs +
				fv.Text();
	Candidate flat_c(flat, ctx);

	Candidates result;
	result.push_back(flat_c);

	if ( flat_c.Fits() )
		return result;

	// Split after ":" - false-value aligns under true-value.
	FmtContext fv_ctx = ctx.AtCol(tv_col);
	auto fv2_cs = FormatExpr(*content[2], fv_ctx);
	const auto& fv2 = Best(fv2_cs);

	std::string fv_prefix = LinePrefix(fv_ctx.Indent(), tv_col);
	std::string split_colon = cond.Text() + qs + tv.Text() +
				" " + col->Text() + "\n" +
				fv_prefix + fv2.Text();
	int last_w = fv2.Width();
	int lines = 1 + fv2.Lines();
	int ovf = Ovf(last_w, fv_ctx);

	result.push_back({split_colon, last_w, lines, ovf, ctx.Col()});

	// Split after "?" - true and false on continuation line,
	// aligned under the start of cond.
	FmtContext cont_ctx = ctx.AtCol(ctx.Col());
	auto tv2_cs = FormatExpr(*content[1], cont_ctx);
	const auto& tv2 = Best(tv2_cs);

	auto fv3_cs = FormatExpr(*content[2],
		cont_ctx.After(tv2.Width() + cw));
	const auto& fv3 = Best(fv3_cs);

	std::string cont_prefix = LinePrefix(cont_ctx.Indent(), ctx.Col());
	std::string split_q = cond.Text() + " " + q->Text() + "\n" +
				cont_prefix + tv2.Text() + cs +
				fv3.Text();
	int q_last_w = tv2.Width() + cw + fv3.Width();
	int q_ovf = Ovf(q_last_w, cont_ctx);

	result.push_back({split_q, q_last_w, 2, q_ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Simple keyword statements: return [expr], print expr, add expr,
// delete expr
// ------------------------------------------------------------------

// Format a keyword statement with an optional expression child.
// SEMI is handled by the caller (top-level or block).
static Candidates FormatEventStmt(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	std::string name = node.Arg();
	const Node* args_node = node.FindChild(Tag::Args);
	const Node* semi = node.FindOptChild(Tag::Semi);
	std::string semi_str = semi ? semi->Text() : "";

	std::string prefix = kw_node->Text() + " " + name;

	const Node* lp = args_node->FindChild(Tag::LParen);
	const Node* rp = args_node->FindChild(Tag::RParen);

	auto [items, has_comments] = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {Candidate(prefix + lp->Text() + rp->Text() +
			semi_str, ctx)};

	if ( ! args_node->TrailingComment().empty() )
		has_comments = true;

	int semi_w = semi ? semi->Width() : 0;
	FmtContext inner = semi ? ctx.Reserve(semi_w) : ctx;
	auto cs = FlatOrFill(prefix, lp->Text(), rp->Text(), "",
		items, has_comments, inner,
		args_node->TrailingComment());

	Candidates result;
	for ( auto& c : cs )
		{
		std::string text = c.Text() + semi_str;
		int w = c.Width() + semi_w;
		result.push_back({text, w, c.Lines(), c.Ovf(), ctx.Col()});
		}

	return result;
	}

static Candidates FormatKeywordStmt(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	std::string keyword = kw_node->Text();
	const Node* expr = nullptr;
	const Node* semi = nullptr;

	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			semi = c.get();
		else if ( ! is_comment(t) && ! is_token(t) && ! expr )
			expr = c.get();
		}

	std::string semi_str = semi ? semi->Text() : "";
	int semi_w = semi ? semi->Width() : 0;

	if ( ! expr )
		return {Candidate(keyword + semi_str, ctx)};

	std::string prefix = keyword + " ";
	int prefix_w = static_cast<int>(prefix.size());

	FmtContext expr_ctx = ctx.After(prefix_w).Reserve(semi_w);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = prefix + ec.Text() + semi_str;
		int w = prefix_w + ec.Width() + semi_w;

		int ovf = ec.Ovf();
		if ( ec.Lines() == 1 )
			ovf = Ovf(w, ctx);

		result.push_back({text, w, ec.Lines(), ovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Module declaration: module SomeName;
// ------------------------------------------------------------------

static Candidates FormatModuleDecl(const Node& node, const FmtContext& ctx)
	{
	const Node* kw = node.FindChild(Tag::Keyword);
	const Node* id = node.FindChild(Tag::Identifier);
	const Node* semi = node.FindChild(Tag::Semi);
	return {Candidate(kw->Text() + " " + id->Text() +
		semi->Text(), ctx)};
	}

// ------------------------------------------------------------------
// Block/body formatting: Whitesmith brace style
// ------------------------------------------------------------------

// Format a PREPROC directive.  Returns the text (always at column 0).
// Directives with conditions get "( arg )" spacing.
static std::string FormatPreproc(const Node& node)
	{
	const auto& directive = node.Arg(0);
	const auto& arg = node.Arg(1);

	if ( arg.empty() )
		return directive;

	// @if, @ifdef, @ifndef get "( arg )" spacing.
	if ( directive == "@if" || directive == "@ifdef" ||
	     directive == "@ifndef" )
		return directive + " ( " + arg + " )";

	// @load, @load-sigs, etc. use space.
	return directive + " " + arg;
	}

// Does this preproc directive increase indentation depth?
static bool preproc_opens(const std::string& directive)
	{
	return directive == "@if" || directive == "@ifdef" ||
		directive == "@ifndef" || directive == "@else";
	}

// Does this preproc directive decrease indentation depth?
static bool preproc_closes(const std::string& directive)
	{
	return directive == "@else" || directive == "@endif";
	}

// Format a sequence of statement nodes as a body at the given indent
// level.  Returns the formatted text without enclosing braces.
// This is the inner-body equivalent of the top-level Format() loop.
static std::string FormatStmtList(const Node::NodeVec& nodes,
                                  const FmtContext& ctx,
                                  bool skip_leading_blanks = false)
	{
	int max_col = ctx.MaxCol();
	int preproc_depth = 0;
	FmtContext cur_ctx = ctx;
	std::string pad = LinePrefix(cur_ctx.Indent(), cur_ctx.Col());

	std::string result;
	bool seen_content = false;

	for ( size_t i = 0; i < nodes.size(); ++i )
		{
		const auto& node = *nodes[i];
		Tag t = node.GetTag();


		if ( t == Tag::Blank )
			{
			if ( skip_leading_blanks && ! seen_content )
				continue;
			result += "\n";
			continue;
			}

		seen_content = true;

		result += EmitPreComments(node, pad);

		// PREPROC directives: flow-control (@if etc.) at column 0,
		// other directives (@load etc.) at current indentation.
		if ( t == Tag::Preproc )
			{
			const auto& directive = node.Arg(0);

			if ( preproc_closes(directive) )
				{
				--preproc_depth;
				int new_indent = preproc_depth;
				int new_col = new_indent * INDENT_WIDTH;
				cur_ctx = FmtContext(new_indent, new_col,
					max_col - new_col);
				pad = LinePrefix(new_indent, new_col);
				}

			// Flow-control directives at column 0; others
			// use current indentation.
			if ( preproc_opens(directive) || directive == "@endif" )
				result += FormatPreproc(node) + "\n";
			else
				result += pad + FormatPreproc(node) + "\n";

			if ( preproc_opens(directive) )
				{
				++preproc_depth;
				int new_indent = preproc_depth;
				int new_col = new_indent * INDENT_WIDTH;
				cur_ctx = FmtContext(new_indent, new_col,
					max_col - new_col);
				pad = LinePrefix(new_indent, new_col);
				}

			continue;
			}

		// COMMENT-TRAILING nodes are handled by the parser
		// (attached to preceding node) or by the parent
		// (e.g. after open brace).  Skip them here.
		if ( t == Tag::CommentTrailing )
			continue;

		if ( is_comment(t) )
			{
			result += pad + node.Arg() + "\n";
			continue;
			}

		// Consume a following SEMI sibling.
		const Node* sibling_semi = nullptr;
		if ( i + 1 < nodes.size() &&
		     nodes[i + 1]->GetTag() == Tag::Semi )
			{
			sibling_semi = nodes[i + 1].get();
			++i;
			}

		std::string semi_str = sibling_semi
			? sibling_semi->Text() : "";

		// Check for trailing comment on the node or its SEMI.
		std::string comment_text = node.TrailingComment();
		if ( comment_text.empty() && sibling_semi )
			comment_text = sibling_semi->TrailingComment();

		int comment_w = static_cast<int>(comment_text.size());
		int trail_w = static_cast<int>(semi_str.size()) + comment_w;

		std::string stmt_text;

		// Bare KEYWORD at statement level: break, next, etc.
		if ( t == Tag::Keyword )
			{
			stmt_text = node.Arg();
			}
		else
			{
			auto it = FormatDispatch().find(t);

			if ( it != FormatDispatch().end() )
				{
				FmtContext stmt_ctx = cur_ctx.Reserve(trail_w);
				auto cs = it->second(node, stmt_ctx);
				stmt_text = Best(cs).Text();
				}
			else
				{
				const char* s = TagToString(t);
				stmt_text = std::string("/* TODO: ") +
					s + " */";
				}
			}

		result += pad + stmt_text + semi_str + comment_text + "\n";
		}

	return result;
	}

// Format a BODY or BLOCK node as a Whitesmith-style braced block.
// Returns the full block including braces and newlines.
// The block starts on a new line at one deeper indent than ctx.
std::string FormatWhitesmithBlock(const Node* body,
                                         const FmtContext& ctx)
	{
	FmtContext block_ctx = ctx.Indented();
	std::string brace_pad = LinePrefix(block_ctx.Indent(), block_ctx.Col());

	if ( ! body )
		return "\n" + brace_pad + "{ }";

	// Extract the children between LBRACE and RBRACE, reading
	// trailing comments from the brace tokens themselves.
	const auto& kids = body->Children();
	std::string open_trail;
	std::string close_trail;
	Node::NodeVec inner;

	bool past_open = false;
	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBrace )
			{
			open_trail = c->TrailingComment();
			past_open = true;
			continue;
			}

		if ( t == Tag::RBrace )
			{
			close_trail = c->TrailingComment();
			continue;
			}

		if ( past_open )
			inner.push_back(c);
		}

	if ( inner.empty() && open_trail.empty() )
		return "\n" + brace_pad + "{ }" + close_trail;

	std::string body_text =
		FormatStmtList(inner, block_ctx, true);

	// If the closing brace has a trailing comment, it
	// belongs on the last statement line, not on the '}'.
	if ( ! close_trail.empty() && ! body_text.empty() &&
	     body_text.back() == '\n' )
		body_text = body_text.substr(0, body_text.size() - 1)
			+ close_trail + "\n";

	return "\n" + brace_pad + "{" + open_trail + "\n" +
		body_text + brace_pad + "}";
	}

// Format a single-statement body (no braces, indented one level).
// Used for if/for/while single-statement bodies.
// Returns text WITHOUT trailing newline (caller adds it).
static std::string FormatSingleStmtBody(const Node* body,
                                        const FmtContext& ctx)
	{
	if ( ! body || body->Children().empty() )
		return "";

	std::string text = FormatStmtList(body->Children(), ctx.Indented());

	// Strip trailing newline - the parent loop adds its own.
	if ( ! text.empty() && text.back() == '\n' )
		text.pop_back();

	return text;
	}

// Format a BODY node: Whitesmith block if first child is BLOCK,
// otherwise indented single-statement body.
std::string FormatBodyText(const Node* body, const FmtContext& ctx)
	{
	if ( ! body || body->Children().empty() )
		return "";

	const auto& first = body->Children()[0];
	if ( first->GetTag() == Tag::Block )
		return FormatWhitesmithBlock(first.get(), ctx);

	return "\n" + FormatSingleStmtBody(body, ctx);
	}

// ------------------------------------------------------------------
// Function/event/hook declarations
// ------------------------------------------------------------------

// Format individual parameters as strings.
struct ParamEntry
	{
	std::string text;
	const Node* comma = nullptr;	// COMMA before this param
	};

static std::vector<ParamEntry> FormatParamEntries(const Node* params,
                                                   const FmtContext& ctx)
	{
	std::vector<ParamEntry> result;

	if ( ! params )
		return result;

	const Node* pending_comma = nullptr;
	for ( const auto& p : params->Children() )
		{
		Tag t = p->GetTag();
		if ( t == Tag::Comma )
			{
			pending_comma = p.get();
			continue;
			}
		if ( is_comment(t) || is_token(t) )
			continue;
		if ( t != Tag::Param )
			continue;

		std::string text = p->Arg();

		const Node* ptype = FindTypeChild(*p);
		if ( ptype )
			{
			const Node* pcol = p->FindChild(Tag::Colon);
			text += pcol->Text() + " " +
				Best(FormatExpr(*ptype, ctx)).Text();
			}

		result.push_back({text, pending_comma});
		pending_comma = nullptr;
		}

	return result;
	}

static Candidates FormatFuncDecl(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* id_node = node.FindChild(Tag::Identifier);

	const Node* params = node.FindChild(Tag::Params);
	const Node* returns = node.FindOptChild(Tag::Returns);
	const Node* body = node.FindChild(Tag::Body);
	const Node* attrs = node.FindOptChild(Tag::AttrList);

	const Node* lp = params->FindChild(Tag::LParen);
	const Node* rp = params->FindChild(Tag::RParen);

	std::string prefix = kw_node->Text() + " " + id_node->Text() + lp->Text();
	int align_col = ctx.Col() + static_cast<int>(prefix.size());

	auto pentries = FormatParamEntries(params, ctx);

	// Build flat param list.
	std::string flat_params;
	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		if ( pe.comma )
			flat_params += pe.comma->Text() + " ";
		flat_params += pe.text;
		}

	// Return type suffix.
	std::string ret_str;
	if ( returns )
		{
		const Node* rcol = node.FindChild(Tag::Colon);
		const Node* rt = FindTypeChild(*returns);
		if ( rt )
			ret_str = rcol->Text() + " " +
				Best(FormatExpr(*rt, ctx)).Text();
		}

	// Attribute suffix.
	std::string attr_str;
	if ( attrs )
		{
		std::string as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			attr_str = " " + as;
		}

	// Trailing comment on the func decl (attached to body).
	std::string trail_str;
	if ( ! body->TrailingComment().empty() )
		trail_str = body->TrailingComment();

	std::string block = FormatWhitesmithBlock(body, ctx);

	// --- Candidate 1: flat signature ---
	std::string sig = prefix + flat_params + rp->Text() + ret_str +
		attr_str + trail_str;
	Candidates result;
	result.push_back(Candidate(sig + block, ctx));

	if ( result[0].Ovf() <= 0 )
		return result;

	// --- Candidate 2: greedy-fill params + attrs on continuation ---
	std::string pad = LinePrefix(ctx.Indent(), align_col);
	int max_col = ctx.MaxCol();
	std::string wrapped = prefix;
	int cur_col = ctx.Col() + static_cast<int>(prefix.size());

	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		int pw = static_cast<int>(pe.text.size());

		if ( i == 0 )
			{
			wrapped += pe.text;
			cur_col += pw;
			}
		else
			{
			std::string sep = pe.comma->Text();
			int need = static_cast<int>(sep.size()) + 1 + pw;
			if ( cur_col + need <= max_col )
				{
				wrapped += sep + " " + pe.text;
				cur_col += need;
				}
			else
				{
				int suffix = (i == pentries.size() - 1) ? 1 : 0;
				int pcol = FitCol(align_col, pw + suffix, max_col);
				std::string ppad = LinePrefix(ctx.Indent(), pcol);
				wrapped += sep + "\n" + ppad + pe.text;
				cur_col = pcol + pw;
				}
			}
		}

	wrapped += rp->Text() + ret_str;

	// Put attrs on their own continuation line if present.
	// Use param alignment column, but shift left if that overflows
	// so the line ends at column max_col - 1.
	if ( ! attr_str.empty() )
		{
		std::string bare_attr = attr_str.substr(1);
		int aw = static_cast<int>(bare_attr.size());
		int attr_col = FitCol(align_col, aw, max_col);
		std::string attr_pad = LinePrefix(ctx.Indent(), attr_col);
		wrapped += "\n" + attr_pad + bare_attr;
		}

	wrapped += trail_str + block;

	int last_w = LastLineLen(wrapped);
	int lines = CountLines(wrapped);
	int ovf = TextOverflow(wrapped, ctx.Col(), max_col);

	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});
	return result;
	}

// ------------------------------------------------------------------
// Condition-block dispatch (if, for, while)
// ------------------------------------------------------------------

static Candidates FormatCondBlock(const Node& node, const FmtContext& ctx)
	{
	return static_cast<const ConditionBlockNode&>(node).Format(ctx);
	}

// ------------------------------------------------------------------
// Export declaration: export { decls }
// ------------------------------------------------------------------

static Candidates FormatExport(const Node& node, const FmtContext& ctx)
	{
	const Node* kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	// Collect non-token children for the body.
	Node::NodeVec body;
	for ( const auto& c : node.Children() )
		if ( ! is_token(c->GetTag()) )
			body.push_back(c);

	std::string body_text = FormatStmtList(body, ctx.Indented());
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());
	std::string inner_pad = LinePrefix(ctx.Indent() + 1,
		(ctx.Indent() + 1) * INDENT_WIDTH);
	std::string close = EmitPreComments(*rb, inner_pad) +
		pad + rb->Text();

	return {Candidate(kw->Text() + " " + lb->Text() + "\n" +
		body_text + close, ctx)};
	}

// ------------------------------------------------------------------
// Switch statement: switch expr { case val: body ... }
// ------------------------------------------------------------------

static Candidates FormatSwitch(const Node& node, const FmtContext& ctx)
	{
	const Node* sw_kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	// Find the switch expression: first content child.
	const Node* switch_expr = nullptr;
	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( ! is_token(t) && ! is_comment(t) )
			{ switch_expr = c.get(); break; }
		}

	// Format the expression.  If the source used parens, unwrap
	// the PAREN node and apply Zeek-style ( expr ) spacing.
	std::string expr_text;
	if ( switch_expr )
		{
		if ( switch_expr->GetTag() == Tag::Paren )
			{
			auto paren_content = switch_expr->ContentChildren();
			if ( ! paren_content.empty() )
				{
				auto cs = FormatExpr(*paren_content[0], ctx);
				expr_text = "( " + Best(cs).Text() + " )";
				}
			}
		else
			expr_text = Best(FormatExpr(*switch_expr, ctx)).Text();
		}

	std::string head = sw_kw->Text() + " " + expr_text + " " + lb->Text();
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());

	std::string result = head;

	// Format each CASE/DEFAULT.
	for ( const auto& c : node.Children() )
		{
		if ( c->GetTag() != Tag::Case && c->GetTag() != Tag::Default )
			continue;

		if ( c->GetTag() == Tag::Default )
			{
			const Node* dkw = c->FindChild(Tag::Keyword);
			const Node* dcol = c->FindChild(Tag::Colon);
			result += "\n" + pad + dkw->Text() + dcol->Text();

			const Node* body = c->FindOptChild(Tag::Body);
			if ( body )
				{
				std::string body_text = FormatStmtList(
					body->Children(), ctx.Indented());
				if ( ! body_text.empty() &&
				     body_text.back() == '\n' )
					body_text.pop_back();
				result += "\n" + body_text;
				}
			continue;
			}

		// CASE: KEYWORD "case" VALUES {...} COLON BODY {...}
		const Node* ckw = c->FindChild(Tag::Keyword);
		const Node* ccol = c->FindChild(Tag::Colon);
		const Node* values = c->FindChild(Tag::Values);
		const Node* body = c->FindOptChild(Tag::Body);

		std::string case_text = ckw->Text() + " ";

		// Collect formatted values and commas.
		std::vector<std::string> vals;
		std::vector<const Node*> vcommas;
		const Node* vpending = nullptr;
		for ( const auto& vc : values->Children() )
			{
			Tag vt = vc->GetTag();
			if ( vt == Tag::Comma )
				vpending = vc.get();
			else if ( ! is_token(vt) && ! is_comment(vt) &&
			          ! is_marker(vt) )
				{
				vals.push_back(
					Best(FormatExpr(*vc, ctx)).Text());
				vcommas.push_back(vpending);
				vpending = nullptr;
				}
			}

		// Fill-pack values, wrapping at comma.
		int case_col = ctx.Col() +
			static_cast<int>(case_text.size());
		std::string vpad = LinePrefix(ctx.Indent(), case_col);
		int max_col = ctx.MaxCol();
		int cur_col = case_col;

		for ( size_t i = 0; i < vals.size(); ++i )
			{
			auto& vi = vals[i];
			int need = static_cast<int>(vi.size());
			if ( i > 0 )
				need += 2;

			if ( i > 0 && cur_col + need > max_col )
				{
				case_text += vcommas[i]->Text() + "\n" + vpad;
				cur_col = case_col;
				}
			else if ( i > 0 )
				{
				case_text += vcommas[i]->Text() + " ";
				cur_col += 2;
				}

			case_text += vi;
			cur_col += static_cast<int>(vi.size());
			}

		case_text += ccol->Text();

		result += "\n" + pad + case_text;

		if ( body )
			{
			std::string body_text = FormatStmtList(
				body->Children(), ctx.Indented());
			if ( ! body_text.empty() &&
			     body_text.back() == '\n' )
				body_text.pop_back();
			result += "\n" + body_text;
			}
		}

	result += "\n" + pad + rb->Text();

	return {Candidate(result, ctx)};
	}

// ------------------------------------------------------------------
// Type declarations: type name: enum/record/basetype ;
// ------------------------------------------------------------------

// Format a record field: "name: type attrs"
// Format a record field.  suffix includes ";" and any trailing
// comment so we can measure overflow and wrap attrs if needed.
static std::string FormatField(const Node& node, const std::string& suffix,
                               const FmtContext& ctx)
	{
	const Node* fcol = node.FindChild(Tag::Colon);
	std::string head = node.Arg() + fcol->Text() + " ";

	const Node* tc = FindTypeChild(node);
	std::string type_str;
	if ( tc )
		type_str = Best(FormatExpr(*tc, ctx)).Text();

	const Node* attrs = node.FindOptChild(Tag::AttrList);
	std::string attr_str;
	if ( attrs )
		{
		std::string as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			attr_str = " " + as;
		}

	// Try flat.
	std::string flat = head + type_str + attr_str + suffix;
	if ( ctx.Col() + static_cast<int>(flat.size()) <= ctx.MaxCol() )
		return flat;

	// Wrap attrs to continuation line aligned one past type start.
	if ( ! attr_str.empty() )
		{
		int attr_col = static_cast<int>(head.size()) + 1;
		std::string pad = LinePrefix(ctx.Indent(),
			ctx.Col() + attr_col);

		auto attr_strs = FormatAttrStrings(*attrs, ctx);
		std::string all_attrs;
		for ( size_t i = 0; i < attr_strs.size(); ++i )
			{
			if ( i > 0 )
				all_attrs += " ";
			all_attrs += attr_strs[i];
			}

		return head + type_str + "\n" + pad +
			all_attrs + suffix;
		}

	return flat;
	}

static Candidates FormatTypeDecl(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* id_node = node.FindChild(Tag::Identifier);
	const Node* colon = node.FindChild(Tag::Colon);
	const Node* semi = node.FindChild(Tag::Semi);
	std::string semi_str = semi->Text();

	std::string prefix = kw_node->Text() + " " + id_node->Text();

	// Simple type alias: type name: basetype;
	const Node* base_type = FindTypeChild(node);
	if ( base_type )
		{
		std::string text = prefix + colon->Text() + " " +
			Best(FormatExpr(*base_type, ctx)).Text() + semi_str;
		return {Candidate(text, ctx)};
		}

	// Enum type.
	const Node* enum_node = node.FindOptChild(Tag::TypeEnum);
	if ( enum_node )
		{
		const Node* ekw = enum_node->FindChild(Tag::Keyword);
		const Node* lb = enum_node->FindChild(Tag::LBrace);
		const Node* rb = enum_node->FindChild(Tag::RBrace);

		std::string head = prefix + colon->Text() + " " +
			ekw->Text() + " " + lb->Text();

		// Collect enum values and commas.
		std::vector<std::string> values;
		std::vector<const Node*> commas;
		bool has_trailing_comma = false;
		const Node* pending_comma = nullptr;
		for ( const auto& c : enum_node->Children() )
			{
			if ( c->GetTag() == Tag::EnumValue )
				{
				std::string v = c->Arg();
				if ( ! c->Arg(1).empty() )
					v += " " + c->Arg(1);
				values.push_back(v);
				commas.push_back(pending_comma);
				pending_comma = nullptr;
				}
			else if ( c->GetTag() == Tag::Comma )
				pending_comma = c.get();
			else if ( c->GetTag() == Tag::TrailingComma )
				has_trailing_comma = true;
			}

		// One per line.
		std::string pad = LinePrefix(ctx.Indent() + 1,
			(ctx.Indent() + 1) * INDENT_WIDTH);
		std::string body;
		for ( size_t i = 0; i < values.size(); ++i )
			{
			body += pad + values[i];
			const Node* nc = (i + 1 < commas.size())
				? commas[i + 1] : nullptr;
			if ( nc || has_trailing_comma )
				body += nc ? nc->Text() : ",";
			body += "\n";
			}

		std::string close_pad = LinePrefix(ctx.Indent(), ctx.Col());
		std::string text = head + "\n" + body + close_pad +
					rb->Text() + semi_str;
		return {Candidate(text, ctx)};
		}

	// Record type.
	const Node* rec_node = node.FindOptChild(Tag::TypeRecord);
	if ( rec_node )
		{
		const Node* rkw = rec_node->FindChild(Tag::Keyword);
		const Node* lb = rec_node->FindChild(Tag::LBrace);
		const Node* rb = rec_node->FindChild(Tag::RBrace);

		std::string head = prefix + colon->Text() + " " +
			rkw->Text() + " " + lb->Text();

		int field_indent = ctx.Indent() + 1;
		int field_col = field_indent * INDENT_WIDTH;
		FmtContext field_ctx(field_indent, field_col,
			ctx.MaxCol() - field_col);
		std::string field_pad = LinePrefix(field_indent, field_col);

		// Collect fields, comments, blanks.
		std::string body;
		const auto& kids = rec_node->Children();
		for ( size_t i = 0; i < kids.size(); ++i )
			{
			auto& ki = kids[i];
			Tag t = ki->GetTag();

			if ( t == Tag::Blank )
				{
				body += "\n";
				continue;
				}

			if ( is_comment(t) )
				{
				body += field_pad + ki->Arg() + "\n";
				continue;
				}

			if ( t == Tag::Field )
				{
				body += EmitPreComments(*ki, field_pad);

				const Node* fsemi =
					ki->FindChild(Tag::Semi);
				std::string suffix = fsemi->Text() +
					ki->TrailingComment();
				std::string field_text =
					FormatField(*ki, suffix, field_ctx);

				body += field_pad + field_text + "\n";
				}
			}

		std::string close_pad = LinePrefix(ctx.Indent(), ctx.Col());
		std::string text = head + "\n" + body + close_pad +
					rb->Text() + semi_str;
		return {Candidate(text, ctx)};
		}

	// Fallback.
	return {Candidate(prefix + semi_str, ctx)};
	}

// ------------------------------------------------------------------
// Dispatch table
// ------------------------------------------------------------------

static const std::unordered_map<Tag, FormatFunc>& FormatDispatch()
	{
	static const std::unordered_map<Tag, FormatFunc> table = {
		{Tag::Identifier, FormatAtom},
		{Tag::Constant, FormatAtom},
		{Tag::FieldAccess, FormatFieldAccess},
		{Tag::FieldAssign, FormatFieldAssign},
		{Tag::BinaryOp, FormatBinary},
		{Tag::UnaryOp, FormatUnary},
		{Tag::Call, FormatCall},
		{Tag::Constructor, FormatConstructor},
		{Tag::Schedule, FormatSchedule},
		{Tag::Index, FormatIndex},
		{Tag::IndexLiteral, FormatIndexLiteral},
		{Tag::Slice, FormatSlice},
		{Tag::Paren, FormatParen},
		{Tag::Interval, FormatInterval},
		{Tag::TypeAtom, FormatAtom},
		{Tag::TypeParameterized, FormatTypeParam},
		{Tag::TypeFunc, FormatTypeFunc},
		{Tag::Ternary, FormatTernary},
		{Tag::GlobalDecl, FormatDecl},
		{Tag::LocalDecl, FormatDecl},
		{Tag::ModuleDecl, FormatModuleDecl},
		{Tag::ExprStmt, FormatExprStmt},
		{Tag::Return, FormatKeywordStmt},
		{Tag::Print, FormatKeywordStmt},
		{Tag::Add, FormatKeywordStmt},
		{Tag::Delete, FormatKeywordStmt},
		{Tag::EventStmt, FormatEventStmt},
		{Tag::FuncDecl, FormatFuncDecl},
		{Tag::If, FormatCondBlock},
		{Tag::For, FormatCondBlock},
		{Tag::While, FormatCondBlock},
		{Tag::ExportDecl, FormatExport},
		{Tag::TypeDecl, FormatTypeDecl},
		{Tag::Switch, FormatSwitch},
	};

	return table;
	}

Candidates FormatExpr(const Node& node, const FmtContext& ctx)
	{
	auto it = FormatDispatch().find(node.GetTag());
	if ( it != FormatDispatch().end() )
		return it->second(node, ctx);

	std::string fallback = std::string("/* ") +
		TagToString(node.GetTag()) + " */";
	return {Candidate(fallback, ctx)};
	}

// ------------------------------------------------------------------
// Statements
// ------------------------------------------------------------------

static Candidates FormatExprStmt(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("EXPR-STMT node needs children");

	const Node* expr = nullptr;
	const Node* semi = nullptr;

	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			semi = c.get();

		else if ( ! is_comment(t) && ! expr )
			expr = c.get();
		}

	if ( ! expr )
		{
		std::string st = semi ? semi->Text() : "";
		return {Candidate(st, ctx)};
		}

	// Reserve trailing space for the semicolon.
	std::string semi_str = semi ? semi->Text() : "";
	int semi_w = semi ? semi->Width() : 0;
	FmtContext expr_ctx = ctx.Reserve(semi_w);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = ec.Text() + semi_str;
		int w = ec.Width() + semi_w;

		int ovf = ec.Ovf();
		if ( ec.Lines() == 1 )
			ovf = Ovf(w, ctx);

		result.push_back({text, w, ec.Lines(), ovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Top-level formatting
// ------------------------------------------------------------------

// Collect all trailing comments from node fields.
static void CollectTrailing(const Node& node,
                            std::vector<std::string>& out)
	{
	if ( ! node.TrailingComment().empty() )
		out.push_back(node.TrailingComment());
	for ( const auto& c : node.Children() )
		CollectTrailing(*c, out);
	}

// Check that every trailing comment appears on a line that has
// preceding content - never as a standalone line.
static void WarnStandaloneTrailing(const std::string& output,
                                   const Node::NodeVec& nodes)
	{
	std::vector<std::string> trailing;
	for ( const auto& n : nodes )
		CollectTrailing(*n, trailing);

	for ( const auto& text : trailing )
		{
		auto pos = output.find(text);
		if ( pos == std::string::npos )
			{
			fprintf(stderr, "warning: trailing comment dropped: "
			        "%s\n", text.c_str());
			continue;
			}

		// Find the start of this line.
		auto sol = output.rfind('\n', pos);
		sol = (sol == std::string::npos) ? 0 : sol + 1;

		// Check whether there is non-whitespace before the comment.
		bool has_content = false;
		for ( auto i = sol; i < pos; ++i )
			if ( output[i] != ' ' && output[i] != '\t' )
				{
				has_content = true;
				break;
				}

		if ( ! has_content )
			fprintf(stderr, "warning: trailing comment on its own "
			        "line: %s\n", text.c_str());
		}
	}

std::string Format(const Node::NodeVec& nodes)
	{
	static constexpr int MAX_WIDTH = 80;
	FmtContext ctx(0, 0, MAX_WIDTH);

	std::string result = FormatStmtList(nodes, ctx);
	WarnStandaloneTrailing(result, nodes);
	return result;
	}

Candidates FormatNode(const Node& node, const FmtContext& ctx)
	{
	return FormatExpr(node, ctx);
	}
