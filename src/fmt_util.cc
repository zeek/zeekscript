#include <algorithm>

#include "fmt_util.h"
#include "stmt.h"

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

int count_lines(const std::string& s)
	{
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

int last_line_len(const std::string& s)
	{
	auto n = s.size();
	auto pos = s.rfind('\n');
	if ( pos != std::string::npos )
		n -= (pos + 1);
	return static_cast<int>(n);
	}

int text_overflow(const std::string& text, int start_col, int max_col)
	{
	int ovf = 0;
	int pos = 0;
	int line_start_col = start_col;

	for ( size_t j = 0; j < text.size(); ++j )
		if ( text[j] == '\n' )
			{
			int line_w = static_cast<int>(j) - pos + line_start_col;
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

// Like text_overflow but returns the maximum overflow of any single
// line rather than the sum.  Handles tab indentation correctly
// (each tab = INDENT_WIDTH columns).
int max_line_overflow(const std::string& text, int start_col, int max_col)
	{
	int max_ovf = 0;
	int col = start_col;

	for ( char c : text )
		{
		if ( c == '\n' )
			{
			int ovf = std::max(0, col - max_col);
			if ( ovf > max_ovf )
				max_ovf = ovf;
			col = 0;
			}
		else if ( c == '\t' )
			col = (col / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++col;
		}

	int ovf = std::max(0, col - max_col);
	if ( ovf > max_ovf )
		max_ovf = ovf;

	return max_ovf;
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

ArgComments collect_args(const NodeVec& children)
	{
	ArgComments items;
	const Node* pending_comma = nullptr;

	for ( size_t i = 0; i < children.size(); ++i )
		{
		auto& c = children[i];

		if ( c->IsMarker() )
			continue;

		if ( c->IsToken() )
			{
			Tag t = c->GetTag();
			if ( t == Tag::Comma )
				pending_comma = c.get();
			continue;
			}

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

Candidate format_args_flat(const ArgComments& items, const FmtContext& ctx)
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

		auto bc = best(format_expr(*it.arg, ctx.After(w)));
		text += bc.Text();
		w += bc.Width();
		}

	return {text};
	}

// Append trailing material after an item in a fill layout.  Handles
// the item's own trailing comment and the next comma (which may carry
// a trailing comment that forces a wrap).
static void append_trailing(const ArgComment& it, const Node* next_comma,
                           std::string& text, int& cur_col, bool& force_wrap)
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
	if ( it.arg->IsLambda() )
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
static void format_fill_arg(const Node& arg, int indent, int max_col,
                           std::string& text, int& cur_col,
                           int& lines, int& total_overflow)
	{
	FmtContext sub(indent, cur_col, max_col - cur_col);
	auto bc = best(format_expr(arg, sub));
	text += bc.Text();

	if ( bc.Lines() > 1 )
		{
		lines += bc.Lines() - 1;
		cur_col = last_line_len(text);
		}
	else
		cur_col += bc.Width();

	total_overflow += bc.Ovf();
	}

Candidate format_args_fill(const ArgComments& items, int align_col, int indent,
                         const FmtContext& first_line_ctx)
	{
	auto pad = line_prefix(indent, align_col);
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
		auto nc = is_last ? nullptr : items[i + 1].comma;

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
				// Leading '\n' = blank line merged
				// from adjacent BLANK.
				size_t start = 0;
				while ( start < lc.size() &&
				        lc[start] == '\n' )
					{
					text += "\n";
					++lines;
					++start;
					}

				size_t end = lc.size();
				while ( end > start &&
				        lc[end - 1] == '\n' )
					--end;

				text += "\n" + pad +
					lc.substr(start, end - start);
				++lines;

				for ( size_t j = end; j < lc.size(); ++j )
					{
					text += "\n";
					++lines;
					}
				}

			text += "\n" + pad;
			++lines;
			cur_col = align_col;
			force_wrap = false;

			format_fill_arg(*it.arg, indent, max_col,
			              text, cur_col, lines, total_overflow);
			append_trailing(it, nc, text, cur_col, force_wrap);
			continue;
			}

		FmtContext sub(indent, cur_col, max_col - cur_col);
		auto bc = best(format_expr(*it.arg, sub));
		int aw = bc.Width();

		if ( i == 0 )
			{
			text += bc.Text();
			cur_col += aw;
			}

		else if ( force_wrap )
			{
			text += "\n" + pad;
			cur_col = align_col;
			force_wrap = false;

			format_fill_arg(*it.arg, indent, max_col,
			              text, cur_col, lines, total_overflow);
			++lines;
			append_trailing(it, nc, text, cur_col, force_wrap);
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
					auto nl = sb.Text().find('\n');
					int first_w = nl != std::string::npos
						? static_cast<int>(nl)
						: sb.Width();
					if ( same_col + first_w <= max_col &&
					     sb.Ovf() == 0 )
						{
						text += it.comma->Text() + " "
							+ sb.Text();
						if ( sb.Lines() > 1 )
							{
							lines += sb.Lines() - 1;
							cur_col = last_line_len(text);
							}
						else
							cur_col = same_col
								+ sb.Width();
						total_overflow += sb.Ovf();
						append_trailing(it, nc, text,
							cur_col, force_wrap);
						continue;
						}
					}
				}
			if ( cur_col + need <= max_col )
				{
				text += it.comma->Text() + " " + bc.Text();
				cur_col += need;
				}
			else
				{
				text += it.comma->Text() + "\n" + pad;
				cur_col = align_col;

				format_fill_arg(*it.arg, indent, max_col, text,
						cur_col, lines, total_overflow);
				++lines;
				append_trailing(it, nc, text, cur_col, force_wrap);
				continue;
				}
			}

		if ( bc.Lines() > 1 )
			{
			lines += bc.Lines() - 1;
			cur_col = last_line_len(text);
			}

		total_overflow += bc.Ovf();
		append_trailing(it, nc, text, cur_col, force_wrap);
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {text, cur_col, lines, total_overflow};
	}

// Try flat, then greedy-fill for a bracketed list of items.
Candidates flat_or_fill(const std::string& prefix, const std::string& open,
                      const std::string& close, const std::string& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment,
                      const std::string& close_prefix)
	{
	bool any_breaks = has_breaks(items) || ! open_comment.empty();
	int prefix_w = static_cast<int>(prefix.size());
	int open_w = static_cast<int>(open.size());
	int close_w = static_cast<int>(close.size());
	int close_extra = static_cast<int>(close_prefix.size());
	int suffix_w = static_cast<int>(suffix.size());
	int open_col = ctx.Col() + prefix_w + open_w;
	int inner_w =
		ctx.MaxCol() - open_col - close_extra - close_w - suffix_w;

	FmtContext inner_ctx(ctx.Indent(), open_col, inner_w);
	std::string cb = close_prefix + close;

	Candidates result;

	// Try flat (only when no comments - a trailing comment
	// forces a line break, so flat would lose it).
	if ( ! any_breaks )
		{
		auto flat_args = format_args_flat(items, inner_ctx);
		auto flat_text = prefix + open + flat_args.Text() + cb + suffix;
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
		auto pad = line_prefix(ctx.Indent(), open_col);
		fill_prefix = open_comment + "\n" + pad;
		}

	auto fill = format_args_fill(items, open_col, ctx.Indent(), inner_ctx);

	// When the last arg is a lambda, put the close bracket on
	// its own line at the alignment column.
	bool close_break = ! items.empty() &&
		items.back().arg->IsLambda();

	std::string fill_text;
	int flast_w;
	int fill_lines = fill.Lines() + (open_comment.empty() ? 0 : 1);

	if ( close_break )
		{
		auto close_pad = line_prefix(ctx.Indent(), open_col);
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

Candidate format_args_vertical(const std::string& open, const std::string& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma)
	{
	int body_indent = ctx.Indent() + 1;
	int body_col = body_indent * INDENT_WIDTH;
	FmtContext body_ctx(body_indent, body_col, ctx.MaxCol() - body_col);
	auto body_pad = line_prefix(body_indent, body_col);
	auto close_pad = line_prefix(ctx.Indent(), ctx.IndentCol());

	auto text = open;
	int lines = 1;
	int ovf = 0;

	for ( size_t i = 0; i < items.size(); ++i )
		{
		text += "\n" + body_pad;
		++lines;

		auto& it = items[i];
		auto bc = best(format_expr(*it.arg, body_ctx));
		text += bc.Text();

		int line_w = body_col + bc.Width();

		const Node* nc = (i + 1 < items.size()) ?
					items[i + 1].comma : nullptr;

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
// Statement list formatting
// ------------------------------------------------------------------

Formatting format_stmt_list(const NodeVec& nodes, const FmtContext& ctx,
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

		// Preprocessor directives.
		if ( t == Tag::Preproc || t == Tag::PreprocCond )
			{
			auto& pp = static_cast<const PreprocBaseNode&>(node);

			if ( pp.ClosesDepth() )
				{
				--preproc_depth;
				int new_indent = preproc_depth;
				int new_col = new_indent * INDENT_WIDTH;
				cur_ctx = FmtContext(new_indent, new_col,
						max_col - new_col);
				pad = line_prefix(new_indent, new_col);
				}

			if ( pp.AtColumnZero() )
				result += pp.FormatText() + "\n";
			else
				result += pad + pp.FormatText() + "\n";

			if ( pp.OpensDepth() )
				{
				++preproc_depth;
				int new_indent = preproc_depth;
				int new_col = new_indent * INDENT_WIDTH;
				cur_ctx = FmtContext(new_indent, new_col,
						max_col - new_col);
				pad = line_prefix(new_indent, new_col);
				}

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

		auto semi_str = sibling_semi ? sibling_semi->Text() : "";

		// Check for trailing comment on the node or its SEMI.
		auto comment_text = node.TrailingComment();
		if ( comment_text.empty() && sibling_semi )
			comment_text = sibling_semi->TrailingComment();

		int comment_w = static_cast<int>(comment_text.size());
		int trail_w = static_cast<int>(semi_str.size()) + comment_w;

		std::string stmt_text;

		// Bare KEYWORD at statement level: break, next, etc.
		if ( t == Tag::Keyword )
			stmt_text = node.Arg();
		else
			stmt_text = best(node.Format(
					cur_ctx.Reserve(trail_w))).Text();

		result += pad + stmt_text + semi_str + comment_text + "\n";
		}

	return result;
	}
