#include <algorithm>

#include "fmt_internal.h"

// ------------------------------------------------------------------
// Arg list collection
// ------------------------------------------------------------------

bool HasBreaks(const ArgComments& items)
	{
	for ( auto& it : items )
		if ( it.HasBreak() )
			return true;

	return false;
	}

ArgComments CollectArgs(const Node::NodeVec& children)
	{
	ArgComments items;
	const Node* pending_comma = nullptr;

	for ( size_t i = 0; i < children.size(); ++i )
		{
		auto& c = children[i];
		Tag t = c->GetTag();

		if ( is_marker(t) )
			continue;

		if ( is_token(t) )
			{
			if ( t == Tag::Comma )
				pending_comma = c.get();
			continue;
			}

		// Orphaned COMMENT-LEADING (end of block, no following
		// sibling) - still appears as a standalone child.
		if ( is_comment(t) )
			continue;

		std::vector<std::string> leading(c->PreComments().begin(),
		                                 c->PreComments().end());

		items.push_back({c.get(), c->TrailingComment(),
		                 std::move(leading), pending_comma});
		pending_comma = nullptr;
		}

	return items;
	}

// ------------------------------------------------------------------
// Arg list formatting
// ------------------------------------------------------------------

Candidate FormatArgsFlat(const ArgComments& items, const FmtContext& ctx)
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

	// Lambda body forces following args onto a new line.
	// Consume the next comma here so it attaches to the
	// closing brace rather than the following arg.
	if ( it.arg->GetTag() == Tag::Lambda )
		{
		if ( next_comma )
			{
			text += next_comma->Text();
			cur_col += next_comma->Width();
			}
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

Candidate FormatArgsFill(const ArgComments& items, int align_col, int indent,
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
Candidates FlatOrFill(const std::string& prefix, const std::string& open,
                      const std::string& close, const std::string& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment,
                      const std::string& close_prefix)
	{
	bool has_breaks = HasBreaks(items) || ! open_comment.empty();
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
	if ( ! has_breaks )
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

	// When the last arg is a lambda, put the close bracket on
	// its own line at the alignment column.
	bool close_break = ! items.empty() &&
		items.back().arg->GetTag() == Tag::Lambda;

	std::string fill_text;
	int flast_w;
	int fill_lines = fill.Lines() + (open_comment.empty() ? 0 : 1);

	if ( close_break )
		{
		std::string close_pad = LinePrefix(ctx.Indent(), open_col);
		fill_text = prefix + open + fill_prefix +
			fill.Text() + "\n" + close_pad + cb + suffix;
		flast_w = open_col + close_extra + close_w + suffix_w;
		++fill_lines;
		}
	else
		{
		fill_text = prefix + open + fill_prefix +
			fill.Text() + cb + suffix;
		flast_w = fill.Width() + close_extra + close_w + suffix_w;
		}

	result.push_back({fill_text, flast_w, fill_lines,
	                  fill.Ovf(), ctx.Col()});

	return result;
	}

Candidate FormatArgsVertical(const std::string& open, const std::string& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma)
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
