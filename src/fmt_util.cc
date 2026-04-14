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

bool has_breaks(const ArgItems& items)
	{
	for ( auto& it : items )
		if ( it.HasBreak() )
			return true;

	return false;
	}

ArgItems collect_args(const LayoutVec& children, LayoutPtr* dangling_comma)
	{
	ArgItems items;
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

		items.push_back({c, std::move(leading), pending_comma});
		pending_comma = nullptr;
		}

	if ( dangling_comma )
		*dangling_comma = pending_comma;

	return items;
	}

// ------------------------------------------------------------------
// Arg list formatting
// ------------------------------------------------------------------

Candidate format_args_flat(const ArgItems& items, const FmtContext& ctx)
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

		auto sub = ctx.After(w);
		if ( ctx.InSubExpr() )
			sub.SetInSubExpr();
		auto bc = best(format_expr(*it.arg, sub));
		fmt += bc.Fmt();
		w += bc.Width();
		}

	return {fmt};
	}

// Append trailing material after an item in a fill layout.  Handles
// the item's own trailing comment and the next comma (which may carry
// a trailing comment that forces a wrap).
static void append_trailing(const ArgItem& it, const LayoutPtr& next_comma,
                           Formatting& fmt, int& cur_col, bool& force_wrap,
                           bool& next_comma_consumed)
	{
	if ( it.arg->MustBreakAfter() )
		force_wrap = true;

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
                           int& lines, int& total_overflow,
                           int trail = 0)
	{
	FmtContext sub(indent, cur_col, max_col - cur_col, trail);
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

// Try placing a multi-line bracketed arg (call, index-literal) on
// the current line instead of wrapping.  Only suitable when the arg
// is genuinely multi-line at the alignment column (not just squeezed
// at the tight current position), the first line fits, and the
// tighter position doesn't cost extra inner lines.
static Candidates try_same_line_arg(const Layout& arg, int cur_col,
                                    int align_col, int indent,
                                    int max_col, int trail,
                                    bool skip_align_check = false)
	{
	auto tag = arg.GetTag();
	if ( tag != Tag::Call && tag != Tag::IndexLiteral )
		return {};

	if ( ! skip_align_check )
		{
		FmtContext ac(indent, align_col, max_col - align_col, trail);
		auto wc = best(format_expr(arg, ac));
		if ( wc.Lines() <= 1 )
			return {};
		}

	int same_col = cur_col + 2;
	int effective_max = max_col - trail;
	FmtContext sc(indent, same_col, effective_max - same_col, trail);
	auto sb = best(format_expr(arg, sc));
	int nl = sb.Fmt().Find('\n');
	int first_w = nl < 0 ? sb.Width() : nl;

	// Reject vertical layout (close bracket on its own line) -
	// same-line placement only helps for fill-packed args.
	if ( nl >= 0 )
		{
		auto& s = sb.Fmt().Str();
		auto last_nl = s.rfind('\n');
		if ( last_nl != std::string::npos )
			{
			bool only_close = true;
			for ( size_t k = last_nl + 1; k < s.size(); ++k )
				if ( s[k] != ' ' && s[k] != '\t' &&
				     s[k] != ')' && s[k] != ']' )
					{
					only_close = false;
					break;
					}
			if ( only_close )
				return {};
			}
		}

	int sb_ovf = sb.Fmt().MaxLineOverflow(same_col, effective_max);
	if ( same_col + first_w <= effective_max && sb_ovf == 0 )
		return {std::move(sb)};

	return {};
	}

// Mutable state threaded through the fill-layout loop.
struct FillState
	{
	Formatting fmt;
	std::string pad;
	int cur_col;
	int lines = 1;
	int total_overflow = 0;
	bool force_wrap = false;
	bool comma_consumed = false;
	int align_col;
	int indent;
	int max_col;
	};

// Wrap to the alignment column and format the arg there.
// Sets force_wrap if the result is multi-line FieldAssign.
static void wrap_and_format(FillState& s, const ArgItem& it,
				const LayoutPtr& nc, int trail)
	{
	s.fmt += "\n" + s.pad;
	s.cur_col = s.align_col;

	int prev_lines = s.lines;
	format_fill_arg(*it.arg, s.indent, s.max_col, s.fmt,
			s.cur_col, s.lines, s.total_overflow, trail);
	++s.lines;

	if ( s.lines - prev_lines > 1 &&
	     it.arg->GetTag() == Tag::FieldAssign )
		s.force_wrap = true;

	append_trailing(it, nc, s.fmt, s.cur_col, s.force_wrap,
			s.comma_consumed);
	}

// Emit a same-line candidate (from try_same_line_arg).
// Returns true on success.
static bool emit_same_line(FillState& s, const ArgItem& it,
				const LayoutPtr& nc, const Candidates& scs)
	{
	if ( scs.empty() )
		return false;

	auto& sb = scs[0];
	s.fmt += Formatting(it.comma) + " " + sb.Fmt();
	if ( sb.Lines() > 1 )
		{
		s.lines += sb.Lines() - 1;
		s.cur_col = s.fmt.LastLineLen();
		}
	else
		s.cur_col = s.cur_col + 2 + sb.Width();

	s.total_overflow += sb.Ovf();

	append_trailing(it, nc, s.fmt, s.cur_col, s.force_wrap,
			s.comma_consumed);
	return true;
	}

Candidate format_args_fill(const ArgItems& items, int align_col, int indent,
                         const FmtContext& first_line_ctx, int trail,
                         const LayoutPtr& dangling_comma,
                         int close_room)
	{
	FillState s;
	s.pad = line_prefix(indent, align_col);
	s.cur_col = first_line_ctx.Col();
	s.align_col = align_col;
	s.indent = indent;
	s.max_col = first_line_ctx.MaxCol();

	for ( size_t i = 0; i < items.size(); ++i )
		{
		auto& it = items[i];
		bool is_last = (i + 1 == items.size());
		auto nc = is_last ? dangling_comma : items[i + 1].comma;
		int t = is_last ? trail : 0;

		// Leading comments force a wrap and appear on their
		// own lines before the item.
		if ( ! it.leading.empty() )
			{
			if ( it.comma )
				{
				s.fmt += it.comma;
				s.cur_col += it.comma->Width();
				}

			emit_fill_leading(it.leading, s.pad, s.fmt, s.lines);
			s.force_wrap = false;
			wrap_and_format(s, it, nc, t);
			continue;
			}

		FmtContext sub(indent, s.cur_col, s.max_col - s.cur_col, t);
		auto bc = best(format_expr(*it.arg, sub));
		int aw = bc.Width();

		// First item: emit and fall through to trailing handling.
		if ( i == 0 )
			{
			s.fmt += bc.Fmt();
			s.cur_col += aw;
			}

		else if ( s.force_wrap )
			{
			if ( it.comma && ! s.comma_consumed )
				s.fmt += it.comma;
			s.force_wrap = false;
			s.comma_consumed = false;
			wrap_and_format(s, it, nc, t);
			continue;
			}

		// Non-first lambda: wrap to give it maximum width.
		else if ( it.arg->IsLambda() )
			{
			s.fmt += Formatting(it.comma);
			int prev = s.lines;
			wrap_and_format(s, it, nc, t);
			if ( s.lines - prev > 1 )
				s.force_wrap = true;
			continue;
			}

		// Multi-line item: try keeping a bracketed arg on the
		// current line; FieldAssign items always wrap.
		else if ( bc.Lines() > 1 &&
		          (bc.Ovf() == 0 ||
		           it.arg->GetTag() == Tag::FieldAssign) )
			{
			auto scs = try_same_line_arg(*it.arg, s.cur_col,
				align_col, indent, s.max_col, t);
			if ( emit_same_line(s, it, nc, scs) )
				continue;
			s.fmt += Formatting(it.comma);
			wrap_and_format(s, it, nc, t);
			continue;
			}

		else
			{
			// Inline if it fits.
			int limit = s.max_col;
			if ( is_last )
				limit -= trail;
			else
				limit += close_room;

			if ( s.cur_col + 2 + aw <= limit )
				{
				s.fmt += Formatting(it.comma) + " " + bc.Fmt();
				s.cur_col += 2 + aw;
				}
			else
				{
				// Try splitting an IndexLiteral on same line.
				if ( it.arg->GetTag() == Tag::IndexLiteral )
					{
					auto scs = try_same_line_arg(*it.arg,
						s.cur_col, align_col, indent,
						s.max_col, t, true);
					if ( emit_same_line(s, it, nc, scs) )
						continue;
					}
				s.fmt += Formatting(it.comma);
				wrap_and_format(s, it, nc, t);
				continue;
				}
			}

		// Fall-through for first item and inline-fits.
		if ( bc.Lines() > 1 )
			{
			s.lines += bc.Lines() - 1;
			s.cur_col = s.fmt.LastLineLen();
			if ( it.arg->GetTag() == Tag::FieldAssign )
				s.force_wrap = true;
			}

		s.total_overflow += bc.Ovf();
		append_trailing(it, nc, s.fmt, s.cur_col, s.force_wrap,
				s.comma_consumed);
		}

	int end_ovf = std::max(0, s.cur_col + trail - s.max_col);
	s.total_overflow += end_ovf;

	return {s.fmt, s.cur_col, s.lines, s.total_overflow};
	}

// State for brute-force break-point search.
struct FillSearch
	{
	const std::vector<int>& arg_widths;	// arg only (no comma)
	const std::vector<int>& comma_widths;	// preceding ", "
	int n;
	int first_avail;
	int cont_avail;
	int min_width;
	int greedy_lines;
	int last_trail;	// extra width on last line (close + comment)

	std::vector<int> best_breaks;
	int best_score = INT_MAX;
	int best_lines = INT_MAX;

	std::vector<int> cur_breaks;
	};

// Width of items from..to-1 on a single line.  The first item on a
// line does not contribute its comma (it goes on the previous line).
static int line_width(const FillSearch& fs, int from, int to)
	{
	int w = 0;
	for ( int j = from; j < to; ++j )
		{
		if ( j > from )
			w += fs.comma_widths[j];
		w += fs.arg_widths[j];
		}
	return w;
	}

// Score a complete layout: max line width - min line width.
static void score_layout(FillSearch& fs, int line_num)
	{
	if ( line_num > fs.greedy_lines )
		return;

	int min_w = INT_MAX, max_w = 0;
	int prev = 0;

	for ( int bp : fs.cur_breaks )
		{
		int w = line_width(fs, prev, bp);
		min_w = std::min(min_w, w);
		max_w = std::max(max_w, w);
		prev = bp;
		}

	int w = line_width(fs, prev, fs.n);
	min_w = std::min(min_w, w);
	max_w = std::max(max_w, w);

	int score = max_w - min_w;
	if ( line_num < fs.best_lines ||
	     (line_num == fs.best_lines && score < fs.best_score) )
		{
		fs.best_breaks = fs.cur_breaks;
		fs.best_score = score;
		fs.best_lines = line_num;
		}
	}

// Recursively try all break-point placements from item line_start.
static void fill_search(FillSearch& fs, int line_start, int cur_w,
                        int line_num)
	{
	for ( int i = line_start; i < fs.n; ++i )
		{
		if ( i == line_start )
			cur_w += fs.arg_widths[i];
		else
			cur_w += fs.comma_widths[i] + fs.arg_widths[i];

		int avail = (line_num == 1) ? fs.first_avail : fs.cont_avail;

		// Last item: account for trailing content (close + comment).
		if ( i == fs.n - 1 )
			{
			if ( cur_w + fs.last_trail > avail && i > line_start )
				return;
			break;
			}

		if ( cur_w > avail && i > line_start )
			return;

		// Prune: remaining items too narrow for next line.
		int remaining = line_width(fs, i + 1, fs.n);
		if ( remaining < fs.min_width && line_num > 1 )
			continue;

		// Prune: current line too narrow.
		if ( cur_w < fs.min_width && line_num > 1 )
			continue;

		fs.cur_breaks.push_back(i + 1);
		fill_search(fs, i + 1, 0, line_num + 1);
		fs.cur_breaks.pop_back();
		}

	score_layout(fs, line_num);
	}

// Brute-force search over break points to find the most balanced
// fill layout.  Only works for simple items (all single-line, no
// comments).  Prunes on overflow and narrow orphan lines.
static Candidates try_best_fill(const ArgItems& items, int align_col,
                                int indent, const FmtContext& first_ctx,
                                int trail = 0)
	{
	int n = static_cast<int>(items.size());
	if ( n < 3 || n > 16 )
		return {};

	std::vector<int> arg_widths(n);
	std::vector<int> comma_widths(n);

	for ( int i = 0; i < n; ++i )
		{
		auto& it = items[i];
		if ( it.arg->MustBreakAfter() || ! it.leading.empty() )
			return {};

		auto bc = best(format_expr(*it.arg, first_ctx));
		if ( bc.Lines() > 1 )
			return {};

		arg_widths[i] = bc.Width();
		comma_widths[i] = it.comma ? it.comma->Width() + 1 : 0;
		}

	int max_col = first_ctx.MaxCol();
	int first_avail = max_col - first_ctx.Col();
	int cont_avail = max_col - align_col;
	int min_width = cont_avail / 3;

	// Count greedy lines as baseline.  Account for trailing
	// content (close bracket, semicolon) on the last item.
	int greedy_lines = 1;
	int cur = 0;
	for ( int i = 0; i < n; ++i )
		{
		int w = (i == 0 || cur == 0) ? arg_widths[i]
		        : comma_widths[i] + arg_widths[i];
		if ( i == n - 1 )
			w += trail;
		int avail = (greedy_lines == 1) ? first_avail : cont_avail;
		if ( cur + w > avail && cur > 0 )
			{
			++greedy_lines;
			cur = arg_widths[i];
			}
		else
			cur += w;
		}

	FillSearch fs = {arg_widths, comma_widths, n,
	                 first_avail, cont_avail, min_width,
	                 greedy_lines, trail, {}, INT_MAX, INT_MAX, {}};
	fill_search(fs, 0, 0, 1);

	if ( fs.best_lines == INT_MAX )
		return {};

	// Reconstruct greedy breaks; skip if best matches greedy.
	std::vector<int> greedy_breaks;
	cur = 0;
	for ( int i = 0; i < n; ++i )
		{
		int w = (i == 0 || cur == 0) ? arg_widths[i]
		        : comma_widths[i] + arg_widths[i];
		if ( i == n - 1 )
			w += trail;
		int avail = greedy_breaks.empty() ? first_avail : cont_avail;
		if ( cur + w > avail && cur > 0 )
			{
			greedy_breaks.push_back(i);
			cur = arg_widths[i];
			}
		else
			cur += w;
		}

	if ( fs.best_breaks == greedy_breaks )
		return {};

	// Build the result using the chosen break points.
	auto pad = line_prefix(indent, align_col);
	Formatting fmt;
	int col = first_ctx.Col();
	int lines = 1;
	size_t bp = 0;

	for ( int i = 0; i < n; ++i )
		{
		auto& it = items[i];

		if ( i == 0 )
			{
			auto bc = best(format_expr(*it.arg, first_ctx));
			fmt += bc.Fmt();
			col += bc.Width();
			}
		else if ( bp < fs.best_breaks.size() &&
		          i == fs.best_breaks[bp] )
			{
			fmt += Formatting(it.comma) + "\n" + pad;
			col = align_col;
			++lines;
			++bp;

			FmtContext sub(indent, col, max_col - col);
			auto bc = best(format_expr(*it.arg, sub));
			fmt += bc.Fmt();
			col += bc.Width();
			}
		else
			{
			FmtContext sub(indent, col + 2, max_col - col - 2);
			auto bc = best(format_expr(*it.arg, sub));
			fmt += Formatting(it.comma) + " " + bc.Fmt();
			col += 2 + bc.Width();
			}

		if ( it.comma )
			col += it.comma->Width();
		}

	int end_ovf = std::max(0, col - max_col);
	return {{std::move(fmt), col, lines, end_ovf, first_ctx.Col()}};
	}

// Try flat, then greedy-fill for a bracketed list of items.
Candidates flat_or_fill(const Formatting& prefix, const Formatting& open,
                      const Formatting& close, const Formatting& suffix,
                      const ArgItems& items, const FmtContext& ctx,
                      const std::string& open_comment,
                      const std::string& close_prefix,
                      const LayoutPtr& dangling_comma)
	{
	bool any_breaks =
		has_breaks(items) || ! open_comment.empty() ||
		(dangling_comma && dangling_comma->MustBreakAfter());
	int prefix_w = prefix.Size();
	int open_w = open.Size();
	int close_w = close.Size();
	int close_extra = static_cast<int>(close_prefix.size());
	int suffix_w = suffix.Size();
	int open_col = ctx.Col() + prefix_w + open_w;
	int close_room = close_extra + close_w + suffix_w;
	int inner_w = ctx.MaxCol() - open_col - close_room;

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
			// For single-arg lists, suppress fill_break
			// in sub-expressions.  Fill_break's 0-overflow
			// lie wastes a line in the only candidate.
			if ( flat_c.Lines() > 1 )
				{
				auto no_fb = inner_ctx;
				no_fb.SetInSubExpr();
				auto alt = format_args_flat(items, no_fb);
				auto af = prefix + open + alt.Fmt() +
					  cb + suffix;
				Candidate ac(std::move(af), ctx);
				if ( ac.Lines() < flat_c.Lines() )
					flat_c = ac;
				}

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

	auto fill = format_args_fill(items, open_col, ctx.Indent(),
				inner_ctx, 0, dangling_comma, close_room);

	// If the assembled output + hard trail overflows, re-fill
	// with hard trail so the fill can wrap to stay within the
	// limit.  Soft trail (e.g. &group=...) can break to its
	// own line, so it should not force tighter wrapping.
	int hard = ctx.HardTrail();
	if ( fill.Lines() == 1 && hard > 0 &&
	     ! result.empty() && result[0].Ovf() > 0 )
		fill = format_args_fill(items, open_col, ctx.Indent(),
					inner_ctx, hard, dangling_comma,
					close_room);

	else if ( fill.Lines() > 1 && hard > 0 )
		{
		auto assembled = prefix + open + fill_prefix +
					fill.Fmt() + cb + suffix;
		int ovf = assembled.MaxLineOverflow(ctx.Col(),
					ctx.MaxCol() - hard);
		if ( ovf > 0 )
			fill = format_args_fill(items, open_col, ctx.Indent(),
						inner_ctx, hard,
						dangling_comma, close_room);
		}

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

	if ( fill.Lines() < 2 || close_break || ! open_comment.empty() )
		return result;

	// Try balanced fill.  For parameter lists, replace greedy
	// (the beam can't distinguish them when the body dominates).
	// For expression lists, offer both and let the beam choose.
	auto bcs = try_best_fill(items, open_col, ctx.Indent(), inner_ctx,
	                         ctx.HardTrail());
	if ( bcs.empty() )
		return result;

	auto& bal = bcs[0];
	int bl = bal.Lines() + (open_comment.empty() ? 0 : 1);
	int bw = bal.Width() + close_extra + close_w + suffix_w;

	auto bal_fmt = prefix + open + bal.Fmt() + cb + suffix;
	Candidate c{std::move(bal_fmt), bw, bl, bal.Ovf(), ctx.Col()};
	if ( ctx.IsParamList() )
		result.back() = c;
	else
		result.push_back(c);

	return result;
	}

// Format one item in vertical layout: expr + optional comma +
// trailing comment.  Returns the total line width.
static int format_vert_item(const ArgItem& it, const LayoutPtr& next_comma,
                            bool trailing_comma, int body_col,
                            const FmtContext& body_ctx, Formatting& fmt)
	{
	// Reserve trailing comma width so the expression can split
	// if the assembled line would overflow.
	int suffix_w = 0;
	if ( next_comma )
		suffix_w += next_comma->Width();
	else if ( trailing_comma )
		suffix_w += 1;

	FmtContext item_ctx = suffix_w > 0 ?
		body_ctx.Reserve(suffix_w) : body_ctx;
	auto bc = best(format_expr(*it.arg, item_ctx));
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

	return line_w;
	}

Candidate format_args_fill_break(const Formatting& prefix,
		const Formatting& open, const Formatting& close,
		const Formatting& suffix, const ArgItems& items,
		const FmtContext& ctx, int start_col)
	{
	int ac = start_col + prefix.Size() + open.Size();
	int avail = ctx.MaxCol() - ac;
	auto pad = line_prefix(ctx.Indent(), ac);
	FmtContext fc(ctx.Indent(), ac, avail, ctx.HardTrail());

	int cr = close.Size() + suffix.Size();
	auto bf = format_args_fill(items, ac, ctx.Indent(), fc,
					ctx.HardTrail(), nullptr, cr);

	auto fmt = prefix + open + "\n" + pad + bf.Fmt() + close + suffix;
	int w = bf.Width() + close.Size() + suffix.Size();

	return {std::move(fmt), w, bf.Lines() + 1, 0, ctx.Col()};
	}

Candidate format_args_vertical(const Formatting& open, const Formatting& close,
                             const ArgItems& items, const FmtContext& ctx,
                             bool trailing_comma,
                             const LayoutPtr& dangling_comma)
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
		auto& it = items[i];

		for ( const auto& lc : it.leading )
			{
			// Leading '\n' = blank line from adjacent BLANK.
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

			fmt += "\n" + body_pad + lc.substr(start, end - start);
			++lines;

			for ( size_t j = end; j < lc.size(); ++j )
				{
				fmt += "\n";
				++lines;
				}
			}

		fmt += "\n" + body_pad;
		++lines;

		auto nc = (i + 1 < items.size()) ?
				items[i + 1].comma : dangling_comma;
		int line_w = format_vert_item(it, nc, trailing_comma,
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
static void update_preproc_indent(int base_indent, int depth,
                                  int max_col, FmtContext& ctx,
                                  std::string& pad)
	{
	int indent = std::max(base_indent, depth);
	int col = indent * INDENT_WIDTH;
	ctx = FmtContext(indent, col, max_col - col);
	pad = line_prefix(indent, col);
	}

// Format a preprocessor directive, adjusting preproc_depth.
static void format_preproc(const Layout& node, int& preproc_depth,
                           int base_indent, int max_col,
                           FmtContext& ctx,
                           std::string& pad, Formatting& result)
	{
	if ( node.ClosesDepth() )
		{
		--preproc_depth;
		update_preproc_indent(base_indent, preproc_depth, max_col,
		                      ctx, pad);
		}

	if ( node.AtColumnZero() )
		result += node.Text();
	else
		result += pad + node.Text();
	result += "\n";

	if ( node.OpensDepth() )
		{
		++preproc_depth;
		update_preproc_indent(base_indent, preproc_depth, max_col,
		                      ctx, pad);
		}
	}

// Check whether a node qualifies for inline-if formatting and
// compute the formatted inline text.  Requirements: IF-NO-ELSE
// with SAME-LINE marker, single simple non-block body, no
// pre-comments on the body child.
// On success sets inline_text and returns true.
static bool try_inline_if(const LayoutVec& nodes, size_t i,
                          const FmtContext& ctx,
                          Formatting& inline_text,
                          bool skip_break_check = false)
	{
	auto& node = *nodes[i];
	if ( node.GetTag() != Tag::IfNoElse )
		return false;
	if ( ! skip_break_check && node.MustBreakBefore() )
		return false;

	// Child 5 is the BODY.
	auto& body = *node.Child(5);
	if ( ! body.FindOptChild(Tag::SameLine) )
		return false;
	auto content = body.ContentChildren();
	if ( content.size() != 1 )
		return false;
	auto& c0 = content[0];
	if ( c0->GetTag() == Tag::Block || c0->MustBreakBefore() )
		return false;

	LayoutPtr sibling_semi;
	++i; // move to trailing node, if any
	if ( ! node.FindOptChild(Tag::Semi) && i < nodes.size() &&
	     nodes[i]->GetTag() == Tag::Semi )
		sibling_semi = nodes[i];

	int semi_w = sibling_semi ? sibling_semi->Width() : 0;

	auto cands = node.Format(ctx.Reserve(semi_w));
	auto& cand = best(cands);
	const auto fmt_str = cand.Fmt().Str();
	auto nl = fmt_str.find('\n');
	if ( nl == std::string::npos )
		return false;

	// Format body at indent 0 / col 0 so there's no pad.
	FmtContext body_ctx(0, 0, ctx.MaxCol(), 0);
	auto body_str = format_stmt_list(content, body_ctx).Str();
	while ( ! body_str.empty() && body_str.back() == '\n' )
		body_str.pop_back();

	std::string cond = fmt_str.substr(0, nl);
	inline_text = Formatting(cond + " " + body_str);
	if ( sibling_semi )
		inline_text += sibling_semi;

	return true;
	}

// Try to format a run of 3+ consecutive IF-NO-ELSE statements
// starting at position i as inline single-line statements.
// Returns the formatted lines on success, empty vector on failure.
static std::vector<Formatting> try_inline_if_run(
	const LayoutVec& nodes, size_t i, size_t& run_end,
	const FmtContext& ctx)
	{
	// Find the end of the IF-NO-ELSE run, skipping sibling SEMIs.
	run_end = i;
	size_t if_count = 0;
	while ( run_end < nodes.size() &&
	        nodes[run_end]->GetTag() == Tag::IfNoElse )
		{
		++if_count;
		++run_end;
		if ( run_end < nodes.size() &&
		     nodes[run_end]->GetTag() == Tag::Semi )
			++run_end;
		}

	if ( if_count < 3 )
		return {};

	std::vector<Formatting> result;
	for ( size_t j = i; j < run_end; ++j )
		{
		if ( nodes[j]->GetTag() != Tag::IfNoElse )
			continue;
		Formatting fmt;
		if ( ! try_inline_if(nodes, j, ctx, fmt, j == i) )
			return {};
		if ( fmt.MaxLineOverflow(ctx.Col(), ctx.MaxCol()) > 0 )
			return {};
		result.push_back(std::move(fmt));
		}

	return result;
	}

// Format a statement node, consuming a following SEMI sibling.
static void format_stmt(const Layout& node, const LayoutVec& nodes,
                        size_t& i, const FmtContext& ctx,
                        const std::string& pad, Formatting& result)
	{
	// Consume a following SEMI sibling when the node doesn't
	// already contain its own (e.g. bare "break" keyword).
	LayoutPtr sibling_semi;
	if ( ! node.FindOptChild(Tag::Semi) && i + 1 < nodes.size() &&
	     nodes[i + 1]->GetTag() == Tag::Semi )
		sibling_semi = nodes[++i];

	// SEMI Width() includes any trailing comment via Text().
	// Format() includes the node's own trailing comment.
	int semi_w = sibling_semi ? sibling_semi->Width() : 0;

	Formatting stmt_fmt;
	if ( node.GetTag() == Tag::Keyword )
		stmt_fmt = Formatting(node.Text());
	else
		stmt_fmt = best_overall(node.Format(ctx.Reserve(semi_w))).Fmt();

	result += pad + stmt_fmt;
	if ( sibling_semi )
		result += sibling_semi;
	result += "\n";
	}

Formatting format_stmt_list(const LayoutVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks)
	{
	const int max_col = ctx.MaxCol();
	const int base_indent = ctx.Indent();
	int preproc_depth = 0;
	FmtContext cur_ctx = ctx;
	auto pad = line_prefix(cur_ctx.Indent(), cur_ctx.Col());

	Formatting result;
	bool seen_content = false;

	for ( size_t i = 0; i < nodes.size(); ++i )
		{
		const auto& node = *nodes[i];
		Tag t = node.GetTag();

		if ( t == Tag::SameLine )
			continue;

		if ( t == Tag::Blank )
			{
			if ( skip_leading_blanks && ! seen_content )
				continue;
			result += "\n";
			continue;
			}

		if ( t == Tag::Semi )
			{
			result += pad + Formatting(nodes[i]) + "\n";
			continue;
			}

		seen_content = true;

		result += node.EmitPreComments(pad);

		if ( t == Tag::Preproc || t == Tag::PreprocCond )
			{
			format_preproc(node, preproc_depth, base_indent,
			               max_col, cur_ctx, pad, result);
			continue;
			}

		if ( t == Tag::IfNoElse )
			{
			size_t run_end;
			auto inlines = try_inline_if_run(nodes, i,
							 run_end, cur_ctx);
			if ( ! inlines.empty() )
				{
				for ( auto& fmt : inlines )
					result += pad + fmt + "\n";
				i = run_end - 1;
				continue;
				}
			}

		format_stmt(node, nodes, i, cur_ctx, pad, result);
		}

	return result;
	}
