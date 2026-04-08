#include <algorithm>

#include "fmt_util.h"

// ------------------------------------------------------------------
// Overflow and text measurement helpers
// ------------------------------------------------------------------

int ovf(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width() + ctx.Trail());
	}

int ovf_no_trail(int candidate_w, const FmtContext& ctx)
	{
	return std::max(0, candidate_w - ctx.Width());
	}

int fit_col(int align_col, int w, int max_col)
	{
	if ( align_col + w <= max_col - 1 )
		return align_col;
	return max_col - 1 - w;
	}

// ------------------------------------------------------------------
// Arg list collection
// ------------------------------------------------------------------

bool has_breaks(const ArgComments& items)
	{
	for ( auto& it : items )
		if ( it.HasBreak() )
			return true;

	return false;
	}

ArgComments collect_args(const LayoutVec& children)
	{
	ArgComments items;
	LayoutPtr pending_comma;

	for ( size_t i = 0; i < children.size(); ++i )
		{
		auto& c = children[i];

		if ( c->IsMarker() )
			continue;

		if ( c->IsToken() )
			{
			Tag t = c->GetTag();
			if ( t == Tag::Comma )
				pending_comma = c;
			continue;
			}

		std::vector<std::string> leading(c->PreComments().begin(),
		                                 c->PreComments().end());

		items.push_back({c, c->TrailingComment(),
		                 std::move(leading), pending_comma});
		pending_comma = nullptr;
		}

	return items;
	}

// ------------------------------------------------------------------
// Arg list formatting
// ------------------------------------------------------------------

Candidate format_args_flat(const ArgComments& items, const FmtContext& ctx)
	{
	Formatting fmt;
	int w = 0;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& it = items[i];

		if ( it.comma )
			{
			fmt += Formatting(it.comma) + " ";
			w += it.comma->Width() + 1;
			}

		auto bc = best(format_expr(*it.arg, ctx.After(w)));
		fmt += bc.Fmt();
		w += bc.Width();
		}

	return {fmt};
	}

// Append trailing material after an item in a fill layout.  Handles
// the item's own trailing comment and the next comma (which may carry
// a trailing comment that forces a wrap).
static void append_trailing(const ArgComment& it, const LayoutPtr& next_comma,
                           Formatting& fmt, int& cur_col, bool& force_wrap,
                           bool& next_comma_consumed)
	{
	// The item's own trailing comment (rare for non-last items).
	if ( ! it.comment.empty() )
		{
		fmt += it.comment;
		cur_col += static_cast<int>(it.comment.size());
		force_wrap = true;
		}

	// The comma between this item and the next may carry a
	// trailing comment (e.g., "x, # note").
	if ( next_comma && next_comma->MustBreakAfter() )
		{
		fmt += next_comma;
		cur_col += next_comma->Width();
		force_wrap = true;
		next_comma_consumed = true;
		}

	// Lambda body forces following args onto a new line.
	// Consume the next comma here so it attaches to the
	// closing brace rather than the following arg.
	if ( it.arg->IsLambda() )
		{
		if ( next_comma )
			{
			fmt += next_comma;
			cur_col += next_comma->Width();
			next_comma_consumed = true;
			}
		force_wrap = true;
		}
	}

// Format an arg at the current column, appending to fmt and
// updating position/lines/overflow.
static void format_fill_arg(const Layout& arg, int indent, int max_col,
                           Formatting& fmt, int& cur_col,
                           int& lines, int& total_overflow)
	{
	FmtContext sub(indent, cur_col, max_col - cur_col);
	auto bc = best(format_expr(arg, sub));
	fmt += bc.Fmt();

	if ( bc.Lines() > 1 )
		{
		lines += bc.Lines() - 1;
		cur_col = fmt.LastLineLen();
		}
	else
		cur_col += bc.Width();

	total_overflow += bc.Ovf();
	}

// Emit leading comments for a fill-layout arg onto fresh lines.
static void emit_fill_leading(const std::vector<std::string>& leading,
                              const std::string& pad,
                              Formatting& fmt, int& lines)
	{
	for ( const auto& lc : leading )
		{
		// Leading '\n' = blank line merged from adjacent BLANK.
		size_t start = 0;
		while ( start < lc.size() && lc[start] == '\n' )
			{
			fmt += "\n";
			++lines;
			++start;
			}

		size_t end = lc.size();
		while ( end > start && lc[end - 1] == '\n' )
			--end;

		fmt += "\n" + pad + lc.substr(start, end - start);
		++lines;

		for ( size_t j = end; j < lc.size(); ++j )
			{
			fmt += "\n";
			++lines;
			}
		}
	}

Candidate format_args_fill(const ArgComments& items, int align_col, int indent,
                         const FmtContext& first_line_ctx)
	{
	auto pad = line_prefix(indent, align_col);
	Formatting fmt;

	int max_col = first_line_ctx.MaxCol();
	int cur_col = first_line_ctx.Col();
	int lines = 1;
	int total_overflow = 0;
	bool force_wrap = false;
	bool comma_consumed = false;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& it = items[i];
		bool is_last = (i + 1 == items.size());
		auto nc = is_last ? nullptr : items[i + 1].comma;

		// Leading comments force a wrap and appear on their
		// own lines before the item.
		if ( ! it.leading.empty() )
			{
			if ( it.comma )
				{
				fmt += it.comma;
				cur_col += it.comma->Width();
				}

			emit_fill_leading(it.leading, pad, fmt, lines);

			fmt += "\n" + pad;
			++lines;
			cur_col = align_col;
			force_wrap = false;

			format_fill_arg(*it.arg, indent, max_col,
			              fmt, cur_col, lines, total_overflow);
			append_trailing(it, nc, fmt, cur_col, force_wrap,
			              comma_consumed);
			continue;
			}

		FmtContext sub(indent, cur_col, max_col - cur_col);
		auto bc = best(format_expr(*it.arg, sub));
		int aw = bc.Width();

		if ( i == 0 )
			{
			fmt += bc.Fmt();
			cur_col += aw;
			}

		else if ( force_wrap )
			{
			if ( it.comma && ! comma_consumed )
				fmt += it.comma;
			fmt += "\n" + pad;
			cur_col = align_col;
			force_wrap = false;
			comma_consumed = false;

			format_fill_arg(*it.arg, indent, max_col,
			              fmt, cur_col, lines, total_overflow);
			++lines;
			append_trailing(it, nc, fmt, cur_col, force_wrap,
			              comma_consumed);
			continue;
			}

		else
			{
			int need = 2 + aw;
			if ( bc.Lines() > 1 )
				{
				// Bracketed list args (INDEX-LITERAL) can
				// start on the current line when the first
				// line fits and there's no overflow.  We
				// must re-format at the actual position
				// (after ", ") so internal alignment is
				// correct.
				need = max_col + 1;
				if ( it.arg->GetTag() == Tag::IndexLiteral &&
				     bc.Ovf() == 0 )
					{
					int same_col = cur_col + 2;
					FmtContext sc(indent, same_col,
						max_col - same_col);
					auto sb = best(format_expr(*it.arg, sc));
					int nl = sb.Fmt().Find('\n');
					int first_w = (nl < 0)
						? sb.Width() : nl;
					if ( same_col + first_w <= max_col &&
					     sb.Ovf() == 0 )
						{
						fmt += Formatting(it.comma) +
							" " + sb.Fmt();
						if ( sb.Lines() > 1 )
							{
							lines += sb.Lines() - 1;
							cur_col = fmt.LastLineLen();
							}
						else
							cur_col = same_col
								+ sb.Width();
						total_overflow += sb.Ovf();
						append_trailing(it, nc, fmt,
							cur_col, force_wrap,
							comma_consumed);
						continue;
						}
					}
				}
			if ( cur_col + need <= max_col )
				{
				fmt += Formatting(it.comma) + " " + bc.Fmt();
				cur_col += need;
				}
			else
				{
				fmt += Formatting(it.comma) + "\n" + pad;
				cur_col = align_col;

				int prev_lines = lines;
				format_fill_arg(*it.arg, indent, max_col, fmt,
						cur_col, lines, total_overflow);
				++lines;

				// Multi-line field-assign: next item
				// starts on a fresh line.
				if ( lines - prev_lines > 1 &&
				     it.arg->GetTag() == Tag::FieldAssign )
					force_wrap = true;

				append_trailing(it, nc, fmt, cur_col, force_wrap,
						comma_consumed);
				continue;
				}
			}

		if ( bc.Lines() > 1 )
			{
			lines += bc.Lines() - 1;
			cur_col = fmt.LastLineLen();
			if ( it.arg->GetTag() == Tag::FieldAssign )
				force_wrap = true;
			}

		total_overflow += bc.Ovf();
		append_trailing(it, nc, fmt, cur_col, force_wrap,
				comma_consumed);
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {fmt, cur_col, lines, total_overflow};
	}

// For a 2-line greedy fill, try breaking one item earlier to balance
// line lengths.  Returns the balanced candidate, or empty if no
// improvement.  Only works for simple items (no comments, single-line).
static Candidates try_balanced_fill(const ArgComments& items, int align_col,
                                    int indent, const FmtContext& first_ctx)
	{
	int n = static_cast<int>(items.size());
	std::vector<int> widths(n);

	for ( int i = 0; i < n; ++i )
		{
		auto& it = items[i];
		if ( ! it.comment.empty() || ! it.leading.empty() )
			return {};

		auto bc = best(format_expr(*it.arg, first_ctx));
		if ( bc.Lines() > 1 )
			return {};

		widths[i] = bc.Width();
		if ( it.comma )
			widths[i] += it.comma->Width() + 1;
		}

	// Find greedy break: first item that doesn't fit on line 1.
	int avail = first_ctx.Width();
	int line1_w = 0;
	int greedy_break = n;

	for ( int i = 0; i < n; ++i )
		{
		if ( line1_w + widths[i] > avail )
			{
			greedy_break = i;
			break;
			}

		line1_w += widths[i];
		}

	if ( greedy_break <= 1 || greedy_break >= n )
		return {};

	// Try breaking one item earlier.
	int total = 0;
	for ( int i = 0; i < n; ++i )
		total += widths[i];

	int w1 = 0;
	for ( int i = 0; i < greedy_break - 1; ++i )
		w1 += widths[i];

	int greedy_w2 = align_col + (total - line1_w);
	int greedy_spread = std::abs((first_ctx.Col() + line1_w) - greedy_w2);

	int try_w2 = align_col + (total - w1);
	int try_spread = std::abs((first_ctx.Col() + w1) - try_w2);

	if ( try_spread >= greedy_spread )
		return {};

	// Re-fill with a narrower first line to force the earlier break.
	auto bal = format_args_fill(items, align_col, indent,
				FmtContext(indent, first_ctx.Col(), w1));

	if ( bal.Lines() != 2 )
		return {};

	return {std::move(bal)};
	}

// Try flat, then greedy-fill for a bracketed list of items.
Candidates flat_or_fill(const Formatting& prefix, const Formatting& open,
                      const Formatting& close, const Formatting& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment,
                      const std::string& close_prefix)
	{
	bool any_breaks = has_breaks(items) || ! open_comment.empty();
	int prefix_w = prefix.Size();
	int open_w = open.Size();
	int close_w = close.Size();
	int close_extra = static_cast<int>(close_prefix.size());
	int suffix_w = suffix.Size();
	int open_col = ctx.Col() + prefix_w + open_w;
	int inner_w =
		ctx.MaxCol() - open_col - close_extra - close_w - suffix_w;

	FmtContext inner_ctx(ctx.Indent(), open_col, inner_w);
	auto cb = close_prefix + close;

	Candidates result;

	// Try flat (only when no comments - a trailing comment
	// forces a line break, so flat would lose it).
	if ( ! any_breaks )
		{
		auto flat_args = format_args_flat(items, inner_ctx);
		auto flat_fmt = prefix + open + flat_args.Fmt() + cb + suffix;
		Candidate flat_c(std::move(flat_fmt), ctx);

		if ( flat_c.Fits() )
			return {flat_c};

		if ( items.size() <= 1 )
			{
			result.push_back(flat_c);
			return result;
			}

		if ( flat_c.Lines() == 1 )
			result.push_back(flat_c);
		}

	// Greedy-fill: pack as many items per line as fit.
	// When there's a comment after the open bracket, it occupies
	// the first line and args start on the next line.
	std::string fill_prefix;
	if ( ! open_comment.empty() )
		{
		auto pad = line_prefix(ctx.Indent(), open_col);
		fill_prefix = open_comment + "\n" + pad;
		}

	auto fill = format_args_fill(items, open_col, ctx.Indent(), inner_ctx);

	// When the last arg is a lambda, put the close bracket on
	// its own line at the alignment column.
	bool close_break = ! items.empty() && items.back().arg->IsLambda();

	int flast_w;
	int fill_lines = fill.Lines() + (open_comment.empty() ? 0 : 1);
	Formatting fill_fmt;

	if ( close_break )
		{
		auto close_pad = line_prefix(ctx.Indent(), open_col);
		fill_fmt = prefix + open + fill_prefix +
				fill.Fmt() + "\n" + close_pad + cb + suffix;
		flast_w = open_col + close_extra + close_w + suffix_w;
		++fill_lines;
		}
	else
		{
		fill_fmt = prefix + open + fill_prefix +
				fill.Fmt() + cb + suffix;
		flast_w = fill.Width() + close_extra + close_w + suffix_w;
		}

	result.push_back({std::move(fill_fmt), flast_w, fill_lines,
	                  fill.Ovf(), ctx.Col()});

	// For 2-line fills, try a balanced break (one item earlier)
	// to reduce spread between the two lines.
	if ( fill.Lines() == 2 && ! close_break &&
	     open_comment.empty() )
		{
		auto bcs = try_balanced_fill(items, open_col,
						ctx.Indent(), inner_ctx);
		if ( ! bcs.empty() )
			{
			auto& bal = bcs[0];
			int bw = bal.Width() + close_extra + close_w + suffix_w;

			auto bal_fmt = prefix + open + bal.Fmt() + cb + suffix;
			result.push_back({std::move(bal_fmt), bw, 2,
			                  bal.Ovf(), ctx.Col()});
			}
		}

	return result;
	}

// Format one item in vertical layout: expr + optional comma +
// trailing comment.  Returns the total line width.
static int format_vert_item(const ArgComment& it, const LayoutPtr& next_comma,
                            bool trailing_comma, int body_col,
                            const FmtContext& body_ctx, Formatting& fmt)
	{
	auto bc = best(format_expr(*it.arg, body_ctx));
	fmt += bc.Fmt();
	int line_w = body_col + bc.Width();

	if ( next_comma || trailing_comma )
		{
		if ( next_comma )
			{
			fmt += next_comma;
			line_w += next_comma->Width();
			}
		else
			{
			fmt += ",";
			line_w += 1;
			}
		}

	fmt += it.comment;
	line_w += static_cast<int>(it.comment.size());
	return line_w;
	}

Candidate format_args_vertical(const Formatting& open, const Formatting& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma)
	{
	int body_indent = ctx.Indent() + 1;
	int body_col = body_indent * INDENT_WIDTH;
	FmtContext body_ctx(body_indent, body_col, ctx.MaxCol() - body_col);
	auto body_pad = line_prefix(body_indent, body_col);
	auto close_pad = line_prefix(ctx.Indent(), ctx.IndentCol());

	Formatting fmt(open);
	int lines = 1;
	int ovf = 0;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		fmt += "\n" + body_pad;
		++lines;

		auto nc = (i + 1 < items.size()) ?
					items[i + 1].comma : nullptr;
		int line_w = format_vert_item(items[i], nc, trailing_comma,
		                              body_col, body_ctx, fmt);
		if ( line_w > ctx.MaxCol() )
			ovf += line_w - ctx.MaxCol();
		}

	fmt += "\n" + close_pad + close;
	++lines;

	int last_w = ctx.IndentCol() + close.Size();
	return {std::move(fmt), last_w, lines, ovf, ctx.Col()};
	}

// ------------------------------------------------------------------
// Statement list formatting
// ------------------------------------------------------------------

// Recompute preproc context/pad after a depth change.
static void update_preproc_indent(int depth, int max_col,
                                  FmtContext& ctx, std::string& pad)
	{
	int col = depth * INDENT_WIDTH;
	ctx = FmtContext(depth, col, max_col - col);
	pad = line_prefix(depth, col);
	}

// Format a preprocessor directive, adjusting preproc_depth.
static void format_preproc(const Layout& node, int& preproc_depth,
                           int max_col, FmtContext& ctx,
                           std::string& pad, Formatting& result)
	{
	if ( node.ClosesDepth() )
		{
		--preproc_depth;
		update_preproc_indent(preproc_depth, max_col, ctx, pad);
		}

	auto text = node.FormatText();
	if ( node.AtColumnZero() )
		result += text;
	else
		result += pad + text;
	result += node.TrailingComment() + "\n";

	if ( node.OpensDepth() )
		{
		++preproc_depth;
		update_preproc_indent(preproc_depth, max_col, ctx, pad);
		}
	}

// Format a statement node, consuming a following SEMI sibling.
static void format_stmt(const Layout& node, const LayoutVec& nodes,
                        size_t& i, const FmtContext& ctx,
                        const std::string& pad, Formatting& result)
	{
	// Consume a following SEMI sibling.
	LayoutPtr sibling_semi;
	if ( i + 1 < nodes.size() &&
	     nodes[i + 1]->GetTag() == Tag::Semi )
		sibling_semi = nodes[++i];

	int semi_w = sibling_semi ? sibling_semi->Width() : 0;

	// Check for trailing comment on the node or its SEMI.
	auto comment_text = node.TrailingComment();
	if ( comment_text.empty() && sibling_semi )
		comment_text = sibling_semi->TrailingComment();

	int comment_w = static_cast<int>(comment_text.size());
	int trail_w = semi_w + comment_w;

	// Bare KEYWORD at statement level: break, next, etc.
	Formatting stmt_fmt;
	if ( node.GetTag() == Tag::Keyword )
		stmt_fmt = node.Arg();
	else
		stmt_fmt = best(node.Format(ctx.Reserve(trail_w))).Fmt();

	result += pad + stmt_fmt;
	if ( sibling_semi )
		result += sibling_semi;
	result += comment_text + "\n";
	}

Formatting format_stmt_list(const LayoutVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks)
	{
	const int max_col = ctx.MaxCol();
	int preproc_depth = 0;
	FmtContext cur_ctx = ctx;
	auto pad = line_prefix(cur_ctx.Indent(), cur_ctx.Col());

	Formatting result;
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

		result += node.EmitPreComments(pad);

		if ( t == Tag::Preproc || t == Tag::PreprocCond )
			{
			format_preproc(node, preproc_depth, max_col,
			               cur_ctx, pad, result);
			continue;
			}

		format_stmt(node, nodes, i, cur_ctx, pad, result);
		}

	return result;
	}
