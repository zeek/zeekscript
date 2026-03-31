#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <unordered_map>

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

// Find a child node by tag.  Returns nullptr if not found.
static const Node* FindChild(const Node& node, Tag tag)
	{
	for ( const auto& c : node.Children() )
		if ( c->GetTag() == tag )
			return c.get();
	return nullptr;
	}

// Forward declarations for mutual recursion.
static Candidates FormatExpr(const Node& node, const FmtContext& ctx);
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
	const auto& kids = node.Children();
	if ( kids.size() < 2 )
		throw FormatError("FIELD-ACCESS node needs 2 children");

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	auto rhs_cs = FormatExpr(*kids[1], ctx.After(lhs.Width() + 1));
	const auto& rhs = Best(rhs_cs);

	return {lhs.Cat("$").Cat(rhs).In(ctx)};
	}

// ------------------------------------------------------------------
// Field assign: $field=expr
// ------------------------------------------------------------------

static Candidates FormatFieldAssign(const Node& node, const FmtContext& ctx)
	{
	Candidate prefix("$" + node.Arg() + "=", ctx);

	if ( node.Children().empty() )
		throw FormatError("FIELD-ASSIGN node needs a value child");

	auto val_cs = FormatExpr(*node.Children()[0], ctx.After(prefix.Width()));
	const auto& val = Best(val_cs);

	return {prefix.Cat(val).In(ctx)};
	}

// ------------------------------------------------------------------
// Arg lists with trailing comments
// ------------------------------------------------------------------

// An argument paired with its trailing comment (if any).
struct ArgComment
	{
	const Node* arg;
	std::string comment;	// trailing: empty or " # ..."
	std::vector<std::string> leading;	// leading comments before item
	};

// Scan a node's children, pairing each non-comment child with
// any trailing COMMENT-TRAILING/COMMENT-PREV that follows it
// and any COMMENT-LEADING that precedes it.
// Returns the items and whether any comments were found.
static std::pair<std::vector<ArgComment>, bool>
CollectArgs(const Node::NodeVec& children)
	{
	std::vector<ArgComment> items;
	bool has_comments = false;
	std::vector<std::string> pending_leading;

	for ( size_t i = 0; i < children.size(); ++i )
		{
		auto& c = children[i];
		Tag t = c->GetTag();

		if ( is_comment(t) )
			{
			if ( (t == Tag::CommentTrailing ||
			      t == Tag::CommentPrev) && ! items.empty() )
				items.back().comment = " " + c->Arg();

			else if ( t == Tag::CommentLeading )
				pending_leading.push_back(c->Arg());

			has_comments = true;
			continue;
			}

		items.push_back({c.get(), {}, std::move(pending_leading)});
		pending_leading.clear();
		}

	return {items, has_comments};
	}

// Extract just the Node pointers from an ArgComment vector.
static std::vector<const Node*> ArgNodes(const std::vector<ArgComment>& items)
	{
	std::vector<const Node*> nodes;
	nodes.reserve(items.size());
	for ( const auto& ac : items )
		nodes.push_back(ac.arg);
	return nodes;
	}

// ------------------------------------------------------------------
// Call: func(args)
// ------------------------------------------------------------------

// Format a comma-separated arg list on one line.
static Candidate FormatArgsFlat(const std::vector<const Node*>& args,
				const FmtContext& ctx)
	{
	std::string text;
	int w = 0;

	for ( size_t i = 0; i < args.size(); ++i )
		{
		if ( i > 0 )
			{
			text += ", ";
			w += 2;
			}

		auto cs = FormatExpr(*args[i], ctx.After(w));
		const auto& best = Best(cs);
		text += best.Text();
		w += best.Width();
		}

	return {text, static_cast<int>(text.size()), 1, 0};
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
		bool has_cmt = ! it.comment.empty();
		bool has_leading = ! it.leading.empty();
		bool is_last = (i + 1 == items.size());

		// Leading comments force a wrap and appear on their
		// own lines before the item.
		if ( has_leading )
			{
			if ( i > 0 )
				{
				text += ",";
				++cur_col;
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

			FmtContext nsub(indent, cur_col, max_col - cur_col);
			auto ncs = FormatExpr(*it.arg, nsub);
			const auto& nbest = Best(ncs);
			text += nbest.Text();

			if ( nbest.Lines() > 1 )
				{
				lines += nbest.Lines() - 1;
				cur_col = LastLineLen(text);
				}
			else
				cur_col += nbest.Width();

			total_overflow += nbest.Ovf();

			if ( has_cmt )
				{
				if ( ! is_last )
					{
					text += ",";
					++cur_col;
					}

				text += it.comment;
				cur_col += static_cast<int>(it.comment.size());
				force_wrap = true;
				}

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
			// Previous item had a trailing comment and already
			// emitted the comma - just wrap.
			text += "\n" + pad;
			cur_col = align_col;
			force_wrap = false;

			// Re-format in the new context.
			FmtContext nsub(indent, cur_col, max_col - cur_col);
			auto ncs = FormatExpr(*it.arg, nsub);
			const auto& nbest = Best(ncs);
			text += nbest.Text();

			if ( nbest.Lines() > 1 )
				{
				lines += nbest.Lines() - 1;
				cur_col = LastLineLen(text);
				}
			else
				cur_col += nbest.Width();

			total_overflow += nbest.Ovf();
			++lines;

			if ( has_cmt )
				{
				if ( ! is_last )
					{
					text += ",";
					++cur_col;
					}

				text += it.comment;
				cur_col += static_cast<int>(it.comment.size());
				force_wrap = true;
				}

			continue;
			}
		else
			{
			// Would ", arg" fit on this line?
			int need = 2 + aw;
			if ( cur_col + need <= max_col )
				{
				text += ", " + best.Text();
				cur_col += need;
				}
			else
				{
				text += ",\n" + pad;
				cur_col = align_col;

				// Re-format in the new context.
				FmtContext nsub(indent, cur_col,
				                max_col - cur_col);
				auto ncs = FormatExpr(*it.arg, nsub);
				const auto& nbest = Best(ncs);
				text += nbest.Text();

				if ( nbest.Lines() > 1 )
					{
					lines += nbest.Lines() - 1;
					cur_col = LastLineLen(text);
					}
				else
					cur_col += nbest.Width();

				total_overflow += nbest.Ovf();
				++lines;

				if ( has_cmt )
					{
					if ( ! is_last )
						{
						text += ",";
						++cur_col;
						}

					text += it.comment;
					cur_col += static_cast<int>(
						it.comment.size());
					force_wrap = true;
					}

				continue;
				}
			}

		if ( best.Lines() > 1 )
			{
			lines += best.Lines() - 1;
			cur_col = LastLineLen(text);
			}

		total_overflow += best.Ovf();

		// Trailing comment: emit comma before comment, force
		// next item to wrap.
		if ( has_cmt )
			{
			if ( ! is_last )
				{
				text += ",";
				++cur_col;
				}

			text += it.comment;
			cur_col += static_cast<int>(it.comment.size());
			force_wrap = true;
			}
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
                             char open, char close,
                             const std::string& suffix,
                             const std::vector<ArgComment>& items,
                             bool has_comments,
                             const FmtContext& ctx,
                             const std::string& open_comment = "")
	{
	int prefix_w = static_cast<int>(prefix.size());
	int suffix_w = static_cast<int>(suffix.size());
	int open_col = ctx.Col() + prefix_w + 1;
	int inner_w = ctx.MaxCol() - open_col - 1 - suffix_w;
	FmtContext inner_ctx(ctx.Indent(), open_col, inner_w);

	std::string ob(1, open);
	std::string cb(1, close);

	Candidates result;

	// Try flat (only when no comments - a trailing comment
	// forces a line break, so flat would lose it).
	if ( ! has_comments )
		{
		auto nodes = ArgNodes(items);
		auto flat_args = FormatArgsFlat(nodes, inner_ctx);
		std::string flat_text = prefix + ob + flat_args.Text() +
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
	std::string fill_text = prefix + ob + fill_prefix +
		fill.Text() + cb + suffix;
	int flast_w = fill.Width() + 1 + suffix_w;
	int fill_lines = fill.Lines() + (open_comment.empty() ? 0 : 1);
	result.push_back({fill_text, flast_w, fill_lines,
	                  fill.Ovf(), ctx.Col()});

	return result;
	}

static Candidates FormatCall(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("CALL node needs children");

	auto func_cs = FormatExpr(*kids[0], ctx);
	const auto& func = Best(func_cs);

	const Node* args_node = nullptr;
	std::string call_comment;
	for ( const auto& c : kids )
		{
		if ( c->GetTag() == Tag::Args )
			args_node = c.get();
		else if ( is_comment(c->GetTag()) && c.get() != kids[0].get() )
			call_comment = " " + c->Arg();
		}

	if ( ! args_node )
		return {func.Cat("()").In(ctx)};

	auto [items, has_comments] = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {func.Cat("()").In(ctx)};

	if ( ! call_comment.empty() )
		has_comments = true;

	return FlatOrFill(func.Text(), '(', ')', "", items,
		has_comments, ctx, call_comment);
	}

// ------------------------------------------------------------------
// Schedule: schedule interval { event() }
// ------------------------------------------------------------------

static Candidates FormatSchedule(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.size() < 2 )
		throw FormatError("SCHEDULE node needs 2 children");

	// First child: interval expression.
	auto interval_cs = FormatExpr(*kids[0], ctx.After(9));	// "schedule "
	const auto& interval = Best(interval_cs);

	// Second child: event call.
	int after_brace = 9 + interval.Width() + 3;	// "schedule interval { "
	auto event_cs = FormatExpr(*kids[1], ctx.After(after_brace + 2));
	const auto& event = Best(event_cs);

	std::string text = "schedule " + interval.Text() + " { " +
		event.Text() + " }";
	return {Candidate(text, ctx)};
	}

// ------------------------------------------------------------------
// Constructor: table(...), set(...), vector(...)
// ------------------------------------------------------------------

static Candidates FormatConstructor(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "table", "set", "vector"

	auto [items, has_comments] = CollectArgs(node.Children());
	auto elems = ArgNodes(items);

	if ( elems.empty() )
		return {Candidate(keyword + "()", ctx)};

	Candidates result;

	// Candidate 1: flat (only when no comments).
	if ( ! has_comments )
		{
		int kw_len = static_cast<int>(keyword.size());
		FmtContext args_ctx(ctx.Indent(), ctx.Col() + kw_len + 1,
		                    ctx.Width() - kw_len - 2);
		auto flat_args = FormatArgsFlat(elems, args_ctx);
		auto flat_c = Candidate(keyword + "(" + flat_args.Text() + ")",
		                        ctx);
		result.push_back(flat_c);

		if ( flat_c.Ovf() == 0 )
			return result;
		}

	// Candidate 2: one element per line, indented body.
	int body_indent = ctx.Indent() + 1;
	int body_col = body_indent * INDENT_WIDTH;
	FmtContext body_ctx(body_indent, body_col, ctx.MaxCol() - body_col);
	std::string body_pad = LinePrefix(body_indent, body_col);
	std::string close_pad = LinePrefix(ctx.Indent(), ctx.IndentCol());

	std::string text = keyword + "(";
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

		if ( i + 1 < items.size() )
			{
			text += ",";
			++line_w;
			}

		text += it.comment;
		line_w += static_cast<int>(it.comment.size());

		if ( line_w > ctx.MaxCol() )
			ovf += line_w - ctx.MaxCol();
		}

	text += "\n" + close_pad + ")";
	++lines;

	int last_w = ctx.IndentCol() + 1;
	result.push_back({text, last_w, lines, ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Index: expr[subscripts]
// ------------------------------------------------------------------

static Candidates FormatIndex(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("INDEX node needs children");

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	const Node* subs_node = FindChild(node, Tag::Subscripts);
	if ( ! subs_node || subs_node->Children().empty() )
		return {base.Cat("[]").In(ctx)};

	int sub_col = ctx.Col() + base.Width() + 1;
	FmtContext bracket_ctx(ctx.Indent(), sub_col,
	                       ctx.Width() - base.Width() - 2);
	auto sub_cs = FormatExpr(*subs_node->Children()[0], bracket_ctx);
	const auto& sub = Best(sub_cs);

	return {base.Cat("[").Cat(sub).Cat("]").In(ctx)};
	}

// ------------------------------------------------------------------
// Index literal: [$field=expr, ...]
// ------------------------------------------------------------------

static Candidates FormatIndexLiteral(const Node& node, const FmtContext& ctx)
	{
	auto [items, has_comments] = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate("[]", ctx)};

	return FlatOrFill("", '[', ']', "", items, has_comments, ctx);
	}

// ------------------------------------------------------------------
// Slice: expr[lo:hi]
// ------------------------------------------------------------------

static Candidates FormatSlice(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.size() < 3 )
		throw FormatError("SLICE node needs 3 children");

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	std::string lo = Best(FormatExpr(*kids[1], ctx)).Text();
	std::string hi = Best(FormatExpr(*kids[2], ctx)).Text();

	std::string sep = (! lo.empty() && ! hi.empty()) ? " : " : ":";
	Candidate flat = base.Cat("[" + lo + sep + hi + "]").In(ctx);

	if ( flat.Fits() || lo.empty() || hi.empty() )
		return {flat};

	// Split after ":" - continuation aligns after "[".
	int bracket_col = ctx.Col() + base.Width() + 1;
	FmtContext hi_ctx = ctx.AtCol(bracket_col);
	std::string hi2 = Best(FormatExpr(*kids[2], hi_ctx)).Text();

	std::string prefix = LinePrefix(hi_ctx.Indent(), bracket_col);
	std::string split = base.Text() + "[" + lo + " :\n" +
				prefix + hi2 + "]";
	int last_w = static_cast<int>(hi2.size()) + 1;  // +1 for "]"
	int split_ovf = Ovf(last_w, hi_ctx);
	int lines = 1 + CountLines(hi2);

	return {flat, {split, last_w, lines, split_ovf, ctx.Col()}};
	}

// ------------------------------------------------------------------
// Paren: (expr)
// ------------------------------------------------------------------

static Candidates FormatParen(const Node& node, const FmtContext& ctx)
	{
	if ( node.Children().empty() )
		throw FormatError("PAREN node needs a child");

	auto inner_cs = FormatExpr(*node.Children()[0], ctx.After(2));
	const auto& inner = Best(inner_cs);

	return {Candidate("( ", ctx).Cat(inner).Cat(" )").In(ctx)};
	}

// ------------------------------------------------------------------
// Unary: ! expr, -expr, ~expr
// ------------------------------------------------------------------

static Candidates FormatUnary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.empty() )
		throw FormatError("UNARY-OP node needs a child");

	// Cardinality/absolute value: |expr|
	if ( op == "|...|" )
		{
		auto operand_cs = FormatExpr(*kids[0], ctx.After(2));
		const auto& operand = Best(operand_cs);
		return {Candidate("|", ctx).Cat(operand).Cat("|").In(ctx)};
		}

	// Zeek style: space after "!".
	std::string ps = op;
	if ( op == "!" )
		ps += " ";

	Candidate prefix(ps, ctx);
	auto operand_cs = FormatExpr(*kids[0], ctx.After(prefix.Width()));
	const auto& operand = Best(operand_cs);

	return {prefix.Cat(operand).In(ctx)};
	}

// ------------------------------------------------------------------
// Binary: lhs op rhs
// ------------------------------------------------------------------

static Candidates FormatBinary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.size() < 2 )
		throw FormatError("BINARY-OP node needs 2 children");

	// ?$ binds without spaces, like field access.
	// Reserve trail space so the LHS splits to leave room.
	if ( op == "?$" )
		{
		std::string rhs_text =
			Best(FormatExpr(*kids[1], ctx)).Text();
		int suffix_w = 2 + static_cast<int>(rhs_text.size());

		auto lhs_cs = FormatExpr(*kids[0], ctx.Reserve(suffix_w));
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

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	// " op " costs op.size() + 2.
	int op_w = static_cast<int>(op.size()) + 2;

	auto rhs_cs = FormatExpr(*kids[1], ctx.After(lhs.Width() + op_w));
	const auto& rhs = Best(rhs_cs);

	// Candidate 1: flat - lhs op rhs
	std::string flat = lhs.Text() + " " + op + " " + rhs.Text();
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

	auto rhs2_cs = FormatExpr(*kids[1], cont_ctx);
	const auto& rhs2 = Best(rhs2_cs);

	std::string cont_prefix = LinePrefix(cont_ctx.Indent(), cont_ctx.Col());

	std::string split = lhs.Text() + " " + op + "\n" +
				cont_prefix + rhs2.Text();
	int line1_w = lhs.Width() + 1 + static_cast<int>(op.size());
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
// Children are type args plus optional OF marker.
static Candidates FormatTypeParam(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "table", "set", "vector"
	const auto& kids = node.Children();

	// Collect bracketed type args and "of" type.
	std::vector<const Node*> bracket_types;
	const Node* of_type = nullptr;
	bool past_of = false;

	for ( const auto& c : kids )
		{
		if ( c->GetTag() == Tag::Of )
			past_of = true;
		else if ( past_of )
			of_type = c.get();
		else
			bracket_types.push_back(c.get());
		}

	std::string suffix;

	if ( of_type )
		suffix = " of " + Best(FormatExpr(*of_type, ctx)).Text();

	if ( bracket_types.empty() )
		return {Candidate(keyword + suffix, ctx)};

	std::vector<ArgComment> bt_items;
	for ( const auto* n : bracket_types )
		bt_items.push_back({n, "", {}});

	return FlatOrFill(keyword, '[', ']', suffix, bt_items, false, ctx);
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
static Candidates FormatTypeFunc(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "event", "function", "hook"
	std::string text = keyword + "(";

	const Node* params = FindChild(node, Tag::Params);
	const Node* returns = FindChild(node, Tag::Returns);

	if ( params )
		{
		bool first = true;
		for ( const auto& p : params->Children() )
			{
			if ( is_comment(p->GetTag()) )
				continue;

			if ( ! first )
				text += ", ";
			first = false;

			text += p->Arg();
			const Node* ptype = FindTypeChild(*p);
			if ( ptype )
				text += ": " + Best(FormatExpr(*ptype,
					ctx)).Text();
			}
		}

	text += ")";

	if ( returns )
		{
		const Node* rt = FindTypeChild(*returns);
		if ( rt )
			text += ": " + Best(FormatExpr(*rt, ctx)).Text();
		}

	return {Candidate(text, ctx)};
	}

// TYPE wrapper node: contains a type child.
static std::string FormatType(const Node& node, const FmtContext& ctx)
	{
	const Node* tc = FindTypeChild(node);
	return tc ? Best(FormatExpr(*tc, ctx)).Text() : "";
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
		if ( attr->Children().empty() )
			continue;

		auto val_cs = FormatExpr(*attr->Children()[0], ctx);
		const auto& val = Best(val_cs);
		if ( val.Text().find(' ') != std::string::npos )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static std::string FormatOneAttr(const Node& attr, bool spaced,
                                 const FmtContext& ctx)
	{
	std::string text = attr.Arg();

	if ( ! attr.Children().empty() )
		{
		auto val_cs = FormatExpr(*attr.Children()[0], ctx);
		std::string eq = spaced ? " = " : "=";
		text += eq + Best(val_cs).Text();
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
static std::string DeclSuffix(const Node* attrs_node, bool has_semi,
                              const FmtContext& ctx)
	{
	std::string suffix;

	if ( attrs_node )
		{
		std::string as = FormatAttrList(*attrs_node, ctx);
		if ( ! as.empty() )
			suffix += " " + as;
		}

	if ( has_semi )
		suffix += ";";

	return suffix;
	}

// Shared state for declaration candidate generation.
struct DeclParts {
	std::string head;	// "global foo", "local bar", etc.
	std::string type_str;	// ": type" or ""
	std::string suffix;	// " &attr1 &attr2;" or ";" or ""
	const Node* type_node;
	const Node* init_node;
	const Node* attrs_node;
	bool has_semi;
};

// Flat candidate + split-after-init for declarations with initializers.
static void DeclWithInit(const DeclParts& d, Candidates& result,
                         const FmtContext& ctx)
	{
	const auto& op = d.init_node->Arg();	// "=", "+="

	if ( d.init_node->Children().empty() )
		throw FormatError("INIT node needs a value child");

	std::string before_val = d.head + d.type_str + " " + op + " ";
	int before_w = static_cast<int>(before_val.size());
	int suffix_w = static_cast<int>(d.suffix.size());

	FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
	auto val_cs = FormatExpr(*d.init_node->Children()[0], val_ctx);
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
		auto val2_cs = FormatExpr(*d.init_node->Children()[0], cont);
		const auto& val2 = Best(val2_cs);

		std::string line1 = d.head + d.type_str + " " + op;
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
	auto type_cs = FormatExpr(*d.type_node->Children()[0], cont);
	std::string tv = Best(type_cs).Text();

	std::string line1 = d.head + ":";
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
	std::string semi_str = d.has_semi ? ";" : "";
	std::string type_suffix = d.attrs_node ? "" : d.suffix;
	std::string split = line1 + "\n" + pad + tv + type_suffix;

	if ( d.attrs_node )
		{
		auto astrs = FormatAttrStrings(*d.attrs_node, ctx);
		for ( const auto& a : astrs )
			split += "\n" + pad + a;
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

	if ( d.init_node )
		{
		const auto& op = d.init_node->Arg();
		FmtContext val_ctx = ctx.After(
			static_cast<int>(line1.size()) +
			static_cast<int>(op.size()) + 2);
		auto vcs = FormatExpr(*d.init_node->Children()[0], val_ctx);
		line1 += " " + op + " " + Best(vcs).Text();
		}

	// Attrs aligned one column past where the type starts.
	int attr_col = static_cast<int>(d.head.size()) + 3;
	std::string attr_pad = LinePrefix(ctx.Indent(), attr_col);
	int max_col = ctx.MaxCol();
	int semi_w = d.has_semi ? 1 : 0;

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

	if ( d.has_semi )
		wrapped += ";";

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

	std::string line1 = d.head + ":";
	std::string pad = LinePrefix(cont.Indent(), cont.Col());
	std::string split = line1 + "\n" + pad + bare_type;

	if ( d.init_node )
		{
		const auto& op = d.init_node->Arg();
		int suffix_w = static_cast<int>(d.suffix.size());
		FmtContext val_ctx = cont.After(
			static_cast<int>(bare_type.size()) +
			static_cast<int>(op.size()) + 2).Reserve(
			suffix_w);
		auto val_cs = FormatExpr(*d.init_node->Children()[0],
					val_ctx);
		split += " " + op + " " + Best(val_cs).Text();
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
	const auto& keyword = node.Arg(0);
	const auto& name = node.Arg(1);

	DeclParts d;
	d.head = keyword + " " + name;
	d.type_node = FindChild(node, Tag::Type);
	d.init_node = FindChild(node, Tag::Init);
	d.attrs_node = FindChild(node, Tag::AttrList);
	d.has_semi = FindChild(node, Tag::Semi) != nullptr;

	if ( d.type_node )
		{
		d.type_str = FormatType(*d.type_node, ctx);
		if ( ! d.type_str.empty() )
			d.type_str = ": " + d.type_str;
		}

	d.suffix = DeclSuffix(d.attrs_node, d.has_semi, ctx);

	Candidates result;

	if ( d.init_node )
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
	const auto& kids = node.Children();
	if ( kids.size() < 3 )
		throw FormatError("TERNARY node needs 3 children");

	auto cond_cs = FormatExpr(*kids[0], ctx);
	const auto& cond = Best(cond_cs);

	int tv_col = ctx.Col() + cond.Width() + 3;  // after "cond ? "
	auto tv_cs = FormatExpr(*kids[1], ctx.After(cond.Width() + 3));
	const auto& tv = Best(tv_cs);

	auto fv_cs = FormatExpr(*kids[2],
		ctx.After(cond.Width() + 3 + tv.Width() + 3));
	const auto& fv = Best(fv_cs);

	std::string flat = cond.Text() + " ? " + tv.Text() +
				" : " + fv.Text();
	Candidate flat_c(flat, ctx);

	Candidates result;
	result.push_back(flat_c);

	if ( flat_c.Fits() )
		return result;

	// Split after ":" - false-value aligns under true-value.
	FmtContext fv_ctx = ctx.AtCol(tv_col);
	auto fv2_cs = FormatExpr(*kids[2], fv_ctx);
	const auto& fv2 = Best(fv2_cs);

	std::string fv_prefix = LinePrefix(fv_ctx.Indent(), tv_col);
	std::string split_colon = cond.Text() + " ? " + tv.Text() +
				" :\n" + fv_prefix + fv2.Text();
	int last_w = fv2.Width();
	int lines = 1 + fv2.Lines();
	int ovf = Ovf(last_w, fv_ctx);

	result.push_back({split_colon, last_w, lines, ovf, ctx.Col()});

	// Split after "?" - true and false on continuation line,
	// aligned under the start of cond.
	FmtContext cont_ctx = ctx.AtCol(ctx.Col());
	auto tv2_cs = FormatExpr(*kids[1], cont_ctx);
	const auto& tv2 = Best(tv2_cs);

	auto fv3_cs = FormatExpr(*kids[2], cont_ctx.After(tv2.Width() + 3));
	const auto& fv3 = Best(fv3_cs);

	std::string cont_prefix = LinePrefix(cont_ctx.Indent(), ctx.Col());
	std::string split_q = cond.Text() + " ?\n" + cont_prefix +
				tv2.Text() + " : " + fv3.Text();
	int q_last_w = tv2.Width() + 3 + fv3.Width();
	int q_ovf = Ovf(q_last_w, cont_ctx);

	result.push_back({split_q, q_last_w, 2, q_ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Simple keyword statements: return [expr], print expr, add expr,
// delete expr, next, break, fallthrough
// ------------------------------------------------------------------

static const std::unordered_map<Tag, const char*> keyword_for_tag = {
	{Tag::Return, "return"},
	{Tag::Print, "print"},
	{Tag::Add, "add"},
	{Tag::Delete, "delete"},
	{Tag::EventStmt, "event"},
	{Tag::Next, "next"},
	{Tag::Break, "break"},
	{Tag::Fallthrough, "fallthrough"},
};

// Format a keyword statement with an optional expression child.
// SEMI is handled by the caller (top-level or block).
static Candidates FormatKeywordStmt(const Node& node, const FmtContext& ctx)
	{
	const char* keyword = keyword_for_tag.at(node.GetTag());

	// Find expression child and SEMI.
	const Node* expr = nullptr;
	bool has_semi = false;

	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			has_semi = true;
		else if ( ! is_comment(t) && ! expr )
			expr = c.get();
		}

	if ( ! expr )
		{
		std::string text = keyword;
		if ( has_semi )
			text += ";";
		return {Candidate(text, ctx)};
		}

	std::string prefix = std::string(keyword) + " ";
	int prefix_w = static_cast<int>(prefix.size());
	int semi_cost = has_semi ? 1 : 0;

	FmtContext expr_ctx = ctx.After(prefix_w).Reserve(semi_cost);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = prefix + ec.Text();
		int w = prefix_w + ec.Width();

		if ( has_semi )
			{
			text += ";";
			++w;
			}

		int ovf = ec.Ovf();
		if ( ec.Lines() == 1 )
			ovf = Ovf(w, ctx);

		result.push_back({text, w, ec.Lines(), ovf, ctx.Col()});
		}

	return result;
	}

// Bare keyword statements with no expression and no children.
static Candidates FormatBareKeyword(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(keyword_for_tag.at(node.GetTag()), ctx)};
	}

// ------------------------------------------------------------------
// Module declaration: module SomeName;
// ------------------------------------------------------------------

static Candidates FormatModuleDecl(const Node& node, const FmtContext& ctx)
	{
	std::string text = "module " + node.Arg() + ";";
	return {Candidate(text, ctx)};
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

		if ( is_comment(t) )
			{
			result += pad + node.Arg() + "\n";
			continue;
			}

		// Consume a following SEMI sibling.
		bool sibling_semi = false;
		if ( i + 1 < nodes.size() &&
		     nodes[i + 1]->GetTag() == Tag::Semi )
			{
			sibling_semi = true;
			++i;
			}

		std::string semi_str = sibling_semi ? ";" : "";

		// Peek ahead for trailing comment.
		Tag next_tag = (i + 1 < nodes.size()) ?
			nodes[i + 1]->GetTag() : Tag::Unknown;
		bool maybe_trailing = next_tag == Tag::CommentTrailing ||
			next_tag == Tag::CommentPrev;

		// Reserve trailing space so the formatter can account
		// for the comment width on the last line.
		std::string comment_text;
		int comment_w = 0;

		if ( maybe_trailing )
			{
			comment_text = " " + nodes[i + 1]->Arg();
			comment_w = static_cast<int>(comment_text.size());
			}

		int trail_w = static_cast<int>(semi_str.size()) +
			comment_w;

		auto it = FormatDispatch().find(t);
		std::string stmt_text;

		if ( it != FormatDispatch().end() )
			{
			FmtContext stmt_ctx = cur_ctx.Reserve(trail_w);
			auto cs = it->second(node, stmt_ctx);
			stmt_text = Best(cs).Text();
			}
		else
			{
			const char* s = TagToString(t);
			stmt_text = std::string("/* TODO: ") + s + " */";
			}

		// Attach the comment to the statement.
		// COMMENT-TRAILING always attaches.  COMMENT-PREV
		// attaches unless the statement is a compound block
		// (ends with '}') where the comment belongs on its
		// own line.
		std::string trailing;
		if ( maybe_trailing )
			{
			bool is_block = ! stmt_text.empty() &&
				stmt_text.back() == '}';
			bool attach = next_tag == Tag::CommentTrailing ||
				! is_block;

			if ( attach )
				{
				trailing = comment_text;
				++i;
				}
			}

		result += pad + stmt_text + semi_str + trailing + "\n";
		}

	return result;
	}

// Format a BODY node as a Whitesmith-style braced block.
// Returns the full block including braces and newlines.
// The block starts on a new line at one deeper indent than ctx.
static std::string FormatWhitesmithBlock(const Node* body,
                                         const FmtContext& ctx)
	{
	FmtContext block_ctx = ctx.Indented();
	std::string brace_pad = LinePrefix(block_ctx.Indent(), block_ctx.Col());

	if ( ! body || body->Children().empty() )
		return "\n" + brace_pad + "{ }";

	std::string body_text =
		FormatStmtList(body->Children(), block_ctx, true);

	return "\n" + brace_pad + "{\n" + body_text + brace_pad + "}";
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
static std::string FormatBodyText(const Node* body, const FmtContext& ctx)
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
static std::vector<std::string> FormatParamStrings(const Node* params,
                                                   const FmtContext& ctx)
	{
	std::vector<std::string> result;

	if ( ! params )
		return result;

	for ( const auto& p : params->Children() )
		{
		if ( is_comment(p->GetTag()) )
			continue;

		std::string text = p->Arg();

		const Node* ptype = FindTypeChild(*p);
		if ( ptype )
			text += ": " + Best(FormatExpr(*ptype, ctx)).Text();

		result.push_back(text);
		}

	return result;
	}

static Candidates FormatFuncDecl(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg(0);	// "event", "function", "hook"
	const auto& name = node.Arg(1);

	const Node* params = FindChild(node, Tag::Params);
	const Node* returns = FindChild(node, Tag::Returns);
	const Node* body = FindChild(node, Tag::Body);
	const Node* attrs = FindChild(node, Tag::AttrList);

	std::string prefix = keyword + " " + name + "(";
	int align_col = ctx.Col() + static_cast<int>(prefix.size());

	auto param_strs = FormatParamStrings(params, ctx);

	// Build flat param list.
	std::string flat_params;
	for ( size_t i = 0; i < param_strs.size(); ++i )
		{
		if ( i > 0 )
			flat_params += ", ";
		flat_params += param_strs[i];
		}

	// Return type suffix.
	std::string ret_str;
	if ( returns )
		{
		const Node* rt = FindTypeChild(*returns);
		if ( rt )
			ret_str = ": " + Best(FormatExpr(*rt, ctx)).Text();
		}

	// Attribute suffix.
	std::string attr_str;
	if ( attrs )
		{
		std::string as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			attr_str = " " + as;
		}

	// Trailing comments.
	std::string trail_str;
	for ( const auto& c : node.Children() )
		if ( c->GetTag() == Tag::CommentPrev )
			trail_str += " " + c->Arg();

	std::string block = FormatWhitesmithBlock(body, ctx);

	// --- Candidate 1: flat signature ---
	std::string sig = prefix + flat_params + ")" + ret_str +
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

	for ( size_t i = 0; i < param_strs.size(); ++i )
		{
		int pw = static_cast<int>(param_strs[i].size());

		if ( i == 0 )
			{
			wrapped += param_strs[i];
			cur_col += pw;
			}
		else
			{
			// Would ", param" fit on this line?
			int need = 2 + pw;	// ", " + param
			if ( cur_col + need <= max_col )
				{
				wrapped += ", " + param_strs[i];
				cur_col += need;
				}
			else
				{
				int suffix = (i == param_strs.size() - 1) ? 1 : 0;
				int pcol = FitCol(align_col, pw + suffix, max_col);
				std::string ppad = LinePrefix(ctx.Indent(), pcol);
				wrapped += ",\n" + ppad + param_strs[i];
				cur_col = pcol + pw;
				}
			}
		}

	wrapped += ")" + ret_str;

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
// If statement: if (cond) body [else body]
// ------------------------------------------------------------------

static Candidates FormatIf(const Node& node, const FmtContext& ctx)
	{
	const Node* cond_node = FindChild(node, Tag::Cond);
	const Node* body_node = FindChild(node, Tag::Body);
	const Node* else_node = FindChild(node, Tag::Else);

	// Format condition.
	std::string cond_text;
	if ( cond_node && ! cond_node->Children().empty() )
		{
		auto cond_cs = FormatExpr(*cond_node->Children()[0], ctx.After(5));
		cond_text = Best(cond_cs).Text();
		}

	std::string head = "if ( " + cond_text + " )";
	std::string result = head + FormatBodyText(body_node, ctx);

	// Collect comment and blank children of the IF node.
	// Comments before ELSE become leading comments; a BLANK before
	// the else (or its leading comments) produces a blank line.
	std::string if_comments;
	bool blank_before_else = false;
	std::string stmt_pad = LinePrefix(ctx.Indent(), ctx.Col());
	for ( const auto& c : node.Children() )
		{
		Tag ct = c->GetTag();

		if ( ct == Tag::Blank )
			{
			blank_before_else = true;
			continue;
			}

		if ( ct == Tag::CommentTrailing )
			{
			// Append to the last line of result.
			result += " " + c->Arg();
			}
		else if ( is_comment(ct) )
			{
			if ( blank_before_else && if_comments.empty() )
				if_comments += "\n";
			if_comments += "\n" + stmt_pad + c->Arg();
			blank_before_else = false;
			}
		}

	// Handle else clause.
	if ( else_node && ! else_node->Children().empty() )
		{
		const auto& else_child = else_node->Children()[0];

		if ( blank_before_else || ! if_comments.empty() )
			result += "\n";

		result += if_comments;

		std::string else_pad = LinePrefix(ctx.Indent(), ctx.Col());

		if ( else_child->GetTag() == Tag::If )
			{
			// else if - format the nested if
			auto inner_cs = FormatIf(*else_child, ctx);
			result += "\n" + else_pad + "else " + Best(inner_cs).Text();
			}
		else if ( else_child->GetTag() == Tag::Block )
			{
			// else { ... }
			result += "\n" + else_pad + "else" +
				FormatWhitesmithBlock(else_child.get(), ctx);
			}
		else
			{
			// else single-stmt - format the else body
			std::string else_body = FormatStmtList(
				else_node->Children(), ctx.Indented());
			if ( ! else_body.empty() && else_body.back() == '\n' )
				else_body.pop_back();
			result += "\n" + else_pad + "else\n" + else_body;
			}
		}

	return {Candidate(result, ctx)};
	}

// ------------------------------------------------------------------
// For statement: for ( var in iterable ) body
// ------------------------------------------------------------------

static Candidates FormatFor(const Node& node, const FmtContext& ctx)
	{
	const Node* vars_node = FindChild(node, Tag::Vars);
	const Node* iter_node = FindChild(node, Tag::Iterable);
	const Node* body_node = FindChild(node, Tag::Body);

	// Format vars (comma-separated identifiers).
	std::string vars_text;
	if ( vars_node )
		{
		bool first = true;
		for ( const auto& v : vars_node->Children() )
			{
			if ( ! first )
				vars_text += ", ";
			first = false;
			vars_text += Best(FormatExpr(*v, ctx)).Text();
			}
		}

	// Format iterable.
	std::string iter_text;
	if ( iter_node && ! iter_node->Children().empty() )
		iter_text = Best(FormatExpr(*iter_node->Children()[0], ctx)).Text();

	std::string head = "for ( " + vars_text + " in " + iter_text + " )";

	return {Candidate(head + FormatBodyText(body_node, ctx), ctx)};
	}

// ------------------------------------------------------------------
// While statement: while ( cond ) body
// ------------------------------------------------------------------

static Candidates FormatWhile(const Node& node, const FmtContext& ctx)
	{
	const Node* cond_node = FindChild(node, Tag::Cond);
	const Node* body_node = FindChild(node, Tag::Body);

	std::string cond_text;
	if ( cond_node && ! cond_node->Children().empty() )
		{
		auto cond_cs = FormatExpr(*cond_node->Children()[0], ctx.After(8));
		cond_text = Best(cond_cs).Text();
		}

	std::string head = "while ( " + cond_text + " )";

	return {Candidate(head + FormatBodyText(body_node, ctx), ctx)};
	}

// ------------------------------------------------------------------
// Export declaration: export { decls }
// ------------------------------------------------------------------

static Candidates FormatExport(const Node& node, const FmtContext& ctx)
	{
	std::string body_text = FormatStmtList(node.Children(), ctx.Indented());
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());

	return {Candidate("export {\n" + body_text + pad + "}", ctx)};
	}

// ------------------------------------------------------------------
// Switch statement: switch expr { case val: body ... }
// ------------------------------------------------------------------

static Candidates FormatSwitch(const Node& node, const FmtContext& ctx)
	{
	// Format the expression.
	const Node* expr_node = FindChild(node, Tag::Expr);
	std::string expr_text;
	if ( expr_node && ! expr_node->Children().empty() )
		expr_text = Best(FormatExpr(*expr_node->Children()[0], ctx)).Text();

	std::string head = "switch " + expr_text + " {";
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());

	std::string result = head;

	// Format each CASE.
	for ( const auto& c : node.Children() )
		{
		if ( c->GetTag() != Tag::Case && c->GetTag() != Tag::Default )
			continue;

		if ( c->GetTag() == Tag::Default )
			{
			result += "\n" + pad + "default:";

			const Node* body = FindChild(*c, Tag::Body);
			if ( body )
				{
				std::string body_text = FormatStmtList(
					body->Children(), ctx.Indented());
				if ( ! body_text.empty() && body_text.back() == '\n' )
					body_text.pop_back();
				result += "\n" + body_text;
				}
			continue;
			}

		// CASE node: VALUES { exprs } BODY { stmts }
		const Node* values = FindChild(*c, Tag::Values);
		const Node* body = FindChild(*c, Tag::Body);

		std::string case_text = "case ";
		if ( values )
			{
			bool first = true;
			for ( const auto& v : values->Children() )
				{
				if ( is_comment(v->GetTag()) )
					continue;

				if ( ! first )
					case_text += ", ";
				first = false;
				case_text += Best(FormatExpr(*v, ctx)).Text();
				}
			}
		case_text += ":";

		result += "\n" + pad + case_text;

		if ( body )
			{
			std::string body_text = FormatStmtList(
				body->Children(), ctx.Indented());
			if ( ! body_text.empty() && body_text.back() == '\n' )
				body_text.pop_back();
			result += "\n" + body_text;
			}
		}

	result += "\n" + pad + "}";

	return {Candidate(result, ctx)};
	}

// ------------------------------------------------------------------
// Type declarations: type name: enum/record/basetype ;
// ------------------------------------------------------------------

// Format a record field: "name: type attrs"
static std::string FormatField(const Node& node, const FmtContext& ctx)
	{
	std::string text = node.Arg() + ": ";

	const Node* tc = FindTypeChild(node);
	if ( tc )
		text += Best(FormatExpr(*tc, ctx)).Text();

	// Find attr-list.
	const Node* attrs = FindChild(node, Tag::AttrList);
	if ( attrs )
		{
		std::string as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			text += " " + as;
		}

	return text;
	}

static Candidates FormatTypeDecl(const Node& node, const FmtContext& ctx)
	{
	const auto& name = node.Arg();
	bool has_semi = FindChild(node, Tag::Semi) != nullptr;
	std::string semi_str = has_semi ? ";" : "";

	// Simple type alias: type name: basetype;
	const Node* base_type = FindTypeChild(node);
	if ( base_type )
		{
		std::string text = "type " + name + ": " +
			Best(FormatExpr(*base_type, ctx)).Text() + semi_str;
		return {Candidate(text, ctx)};
		}

	// Enum type.
	const Node* enum_node = FindChild(node, Tag::TypeEnum);
	if ( enum_node )
		{
		std::string head = "type " + name + ": enum {";

		// Collect enum values.
		std::vector<std::string> values;
		for ( const auto& c : enum_node->Children() )
			if ( c->GetTag() == Tag::EnumValue )
				values.push_back(c->Arg());

		// One per line.
		std::string pad = LinePrefix(ctx.Indent() + 1,
			(ctx.Indent() + 1) * INDENT_WIDTH);
		std::string body;
		for ( size_t i = 0; i < values.size(); ++i )
			{
			body += pad + values[i];
			if ( i + 1 < values.size() )
				body += ",";
			body += "\n";
			}

		std::string close_pad = LinePrefix(ctx.Indent(), ctx.Col());
		std::string text = head + "\n" + body + close_pad + "}" +
					semi_str;
		return {Candidate(text, ctx)};
		}

	// Record type.
	const Node* rec_node = FindChild(node, Tag::TypeRecord);
	if ( rec_node )
		{
		std::string head = "type " + name + ": record {";

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
			Tag t = kids[i]->GetTag();

			if ( t == Tag::Blank )
				{
				body += "\n";
				continue;
				}

			if ( is_comment(t) )
				{
				body += field_pad + kids[i]->Arg() + "\n";
				continue;
				}

			if ( t == Tag::Field )
				{
				std::string field_text =
					FormatField(*kids[i], field_ctx);

				// Check for trailing comment.
				std::string trailing;
				if ( i + 1 < kids.size() &&
				     kids[i + 1]->GetTag() == Tag::CommentTrailing )
					{
					trailing = " " + kids[i + 1]->Arg();
					++i;
					}

				body += field_pad + field_text + ";" +
					trailing + "\n";
				}
			}

		std::string close_pad = LinePrefix(ctx.Indent(), ctx.Col());
		std::string text = head + "\n" + body + close_pad + "}" +
					semi_str;
		return {Candidate(text, ctx)};
		}

	// Fallback.
	return {Candidate("type " + name + semi_str, ctx)};
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
		{Tag::EventStmt, FormatKeywordStmt},
		{Tag::Next, FormatBareKeyword},
		{Tag::Break, FormatBareKeyword},
		{Tag::Fallthrough, FormatBareKeyword},
		{Tag::FuncDecl, FormatFuncDecl},
		{Tag::If, FormatIf},
		{Tag::For, FormatFor},
		{Tag::While, FormatWhile},
		{Tag::ExportDecl, FormatExport},
		{Tag::TypeDecl, FormatTypeDecl},
		{Tag::Switch, FormatSwitch},
	};

	return table;
	}

static Candidates FormatExpr(const Node& node, const FmtContext& ctx)
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
	bool has_semi = false;

	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			has_semi = true;

		else if ( ! is_comment(t) && ! expr )
			expr = c.get();
		}

	if ( ! expr )
		return {Candidate(";", ctx)};

	// Reserve trailing space for the semicolon.
	int semi_cost = has_semi ? 1 : 0;
	FmtContext expr_ctx = ctx.Reserve(semi_cost);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = ec.Text();
		int w = ec.Width();

		if ( has_semi )
			{
			text += ";";
			++w;
			}

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

// Collect all COMMENT-TRAILING text from a node tree.
static void CollectTrailing(const Node& node,
                            std::vector<std::string>& out)
	{
	if ( node.GetTag() == Tag::CommentTrailing )
		out.push_back(node.Arg());
	for ( const auto& c : node.Children() )
		CollectTrailing(*c, out);
	}

// Check that every COMMENT-TRAILING text appears on a line that has
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
			fprintf(stderr, "warning: COMMENT-TRAILING dropped: %s\n",
			        text.c_str());
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
			fprintf(stderr, "warning: COMMENT-TRAILING on its own "
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
