#include <algorithm>

#include "expr.h"
#include "fmt_util.h"

Candidates AtomNode::Format(const FmtContext& ctx) const
	{
	return {Candidate(Arg(), ctx)};
	}

// Field access: rec$field
// Children: [0]=expr [1]=DOLLAR [2]=IDENTIFIER
Candidates FieldAccessNode::Format(const FmtContext& ctx) const
	{
	auto lhs_cs = format_expr(*Child(0), ctx);
	const auto& lhs = best(lhs_cs);

	auto dollar = Child(1, Tag::Dollar);
	int dw = dollar->Width();
	auto rhs_cs = format_expr(*Child(2, Tag::Identifier), ctx.After(lhs.Width() + dw));
	auto rhs = best(rhs_cs);

	return {lhs.Cat(dollar).Cat(rhs).In(ctx)};
	}

// Field assign: $field=expr
// Children: [0]=DOLLAR [1]=ASSIGN [2]=expr
Candidates FieldAssignNode::Format(const FmtContext& ctx) const
	{
	auto prefix = Formatting(Child(0, Tag::Dollar)) + Arg() +
			Child(1, Tag::Assign);
	int pw = prefix.Size();

	auto val_cs = format_expr(*Child(2), ctx.After(pw));
	auto val = best(val_cs);

	return {Candidate(prefix + val.Fmt(), ctx)};
	}

static Candidates FormatConstructor_args(const Formatting& open,
	const Formatting& close, const ArgComments& items,
	const FmtContext& ctx);

// Call: func(args)
Candidates CallNode::Format(const FmtContext& ctx) const
	{
	auto content = ContentChildren("CALL", 1);
	auto func_cs = format_expr(*content[0], ctx);
	const auto& func = best(func_cs);

	auto args_node = Child(1, Tag::Args);

	auto lp = args_node->Child(0, Tag::LParen);
	const auto& rp = args_node->Children().back();
	auto items = collect_args(args_node->Children());

	if ( items.empty() )
		return {func.Cat(lp).Cat(rp).In(ctx)};

	// Trailing comma signals one-per-line intent.
	auto open = func.Fmt() + lp;
	if ( args_node->FindOptChild(Tag::TrailingComma) )
		return {format_args_vertical(open, rp, items, ctx, true)};

	auto result = flat_or_fill(func.Fmt(), lp, rp, "", items, ctx,
				args_node->TrailingComment());

	// When fill wraps every single-line item to its own line,
	// vertical layout with indent-based alignment is cleaner.
	if ( items.size() >= 3 && result.size() > 1 &&
	     result.back().Lines() == static_cast<int>(items.size()) )
		{
		int open_col = ctx.Col() + open.Size();
		int inner_w = ctx.MaxCol() - open_col - 1;
		FmtContext chk(ctx.Indent(), open_col, inner_w);

		for ( const auto& it : items )
			if ( best(format_expr(*it.arg, chk)).Lines() > 1 )
				return result;

		result.pop_back();
		result.push_back(format_args_vertical(open, rp,
							items, ctx, false));
		}

	return result;
	}

// Schedule: schedule interval { event() }
// Children: [0]=KEYWORD [1]=SP [2]=interval [3]=LBRACE [4]=event [5]=RBRACE
Candidates ScheduleNode::Format(const FmtContext& ctx) const
	{
	return build_layout({tok(Child(0, Tag::Keyword)), soft_sp, Child(2),
		soft_sp, tok(Child(3, Tag::LBrace)),
		soft_sp, Child(4), soft_sp,
		tok(Child(5, Tag::RBrace))}, ctx);
	}

// Lambda without captures: function(params): ret { body }
// Children: [0]=KEYWORD [1]=SP [2]=PARAMS ...
Formatting LambdaNode::BuildPrefix(const FmtContext& /*ctx*/) const
	{
	return Formatting(Child(0, Tag::Keyword));
	}

// Lambda with captures: function[captures](params): ret { body }
// Children: [0]=KEYWORD [1]=SP [2]=CAPTURES [3]=PARAMS ...
Formatting LambdaCapturesNode::BuildPrefix(const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	auto captures = Child(2, Tag::Captures);
	auto clb = captures->Child(0, Tag::LBracket);
	const auto& crb = captures->Children().back();
	auto cap_items = collect_args(captures->Children());

	if ( cap_items.empty() )
		return Formatting(kw) + clb + crb;

	auto cs = flat_or_fill(kw, clb, crb, "", cap_items, ctx);
	return best(cs).Fmt();
	}

Candidates LambdaNode::Format(const FmtContext& ctx) const
	{
	return FormatLambda(BuildPrefix(ctx), ctx);
	}


// LAMBDA:          [0]=KW [1]=SP [2]=PARAMS [opt COLON RETURNS] BODY(last)
// LAMBDA-CAPTURES: [0]=KW [1]=SP [2]=CAPTURES [3]=PARAMS [opt ...] BODY(last)
Candidates LambdaNode::FormatLambda(const Formatting& prefix,
                                    const FmtContext& ctx) const
	{
	int pp = ParamsPos();

	// Return type (COLON and RETURNS follow PARAMS when present).
	Formatting ret_fmt;
	auto after_params = Child(pp + 1);
	if ( after_params->GetTag() == Tag::Colon )
		{
		auto returns = Child(pp + 2, Tag::Returns);
		if ( auto rt = returns->FindTypeChild() )
			ret_fmt = Formatting(after_params) + " " +
				best(format_expr(*rt, ctx)).Fmt();
		}

	// Params.
	auto params = Child(pp, Tag::Params);
	auto lp = params->Child(0, Tag::LParen);
	const auto& rp = params->Children().back();
	auto items = collect_args(params->Children());
	Formatting sig;

	if ( items.empty() )
		sig = prefix + lp + rp + ret_fmt;
	else
		{
		auto cs = flat_or_fill(prefix, lp, rp, ret_fmt, items, ctx);
		sig = best(cs).Fmt();
		}

	// Format body with indent level based on the lambda's column
	// position, so the Whitesmith block aligns to the next tab stop.
	int lambda_indent = ctx.Col() / INDENT_WIDTH;
	FmtContext body_ctx(lambda_indent, ctx.Col(), ctx.MaxCol() - ctx.Col());
	const auto& body = Children().back();
	auto block = body->FormatWhitesmithBlock(body_ctx);

	auto text = (sig + block).Str();
	int last_w = last_line_len(text);
	int lines = count_lines(text);
	int ovf = text_overflow(text, ctx.Col(), ctx.MaxCol());

	return {{std::move(text), last_w, lines, ovf, ctx.Col()}};
	}

// Shared constructor layout: flat or vertical (one per line).
static Candidates FormatConstructor_args(const Formatting& open,
	const Formatting& close, const ArgComments& items,
	const FmtContext& ctx)
	{
	Candidates result;

	if ( ! has_breaks(items) )
		{
		int open_w = open.Size();
		int close_w = close.Size();
		FmtContext args_ctx(ctx.Indent(), ctx.Col() + open_w,
		                    ctx.Width() - open_w - close_w);
		auto flat_args = format_args_flat(items, args_ctx);
		auto flat_fmt = open + flat_args.Fmt() + close;
		Candidate flat_c(std::move(flat_fmt), ctx);
		result.push_back(flat_c);

		if ( flat_c.Ovf() == 0 )
			return result;
		}

	result.push_back(format_args_vertical(open, close, items, ctx));
	return result;
	}

// Constructor: table(...), set(...), vector(...), {1, 2, 3}
// Children: [0]=KEYWORD [1]=LPAREN ... [last]=RPAREN
Candidates ConstructorNode::Format(const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	auto lp = Child(1, Tag::LParen);
	const auto& rp = Children().back();
	auto open = Formatting(kw) + lp;

	auto items = collect_args(Children());
	if ( items.empty() )
		return {Candidate(open + rp, ctx)};

	// Detect trailing comma: N items have N-1 separators, so
	// N commas means the last one is trailing.
	int commas = 0;
	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Comma )
			++commas;

	bool trailing = commas >= static_cast<int>(items.size());
	if ( trailing )
		return {format_args_vertical(open, rp, items, ctx, true)};

	return FormatConstructor_args(open, rp, items, ctx);
	}

// Index: expr[subscripts]
// Children: [0]=expr [1]=SUBSCRIPTS
Candidates IndexNode::Format(const FmtContext& ctx) const
	{
	auto base_cs = format_expr(*Child(0), ctx);
	const auto& base = best(base_cs);

	auto subs_node = Child(1, Tag::Subscripts);

	auto lb = subs_node->Child(0, Tag::LBracket);
	const auto& rb = subs_node->Children().back();

	auto subs_content = subs_node->ContentChildren();
	if ( subs_content.empty() )
		return {base.Cat(lb).Cat(rb).In(ctx)};

	if ( subs_content.size() == 1 )
		{
		int lb_w = lb->Width();
		int rb_w = rb->Width();
		int sub_col = ctx.Col() + base.Width() + lb_w;
		FmtContext bracket_ctx(ctx.Indent(), sub_col,
					ctx.Width() - base.Width() -
						lb_w - rb_w);
		auto sub_cs = format_expr(*subs_content[0], bracket_ctx);
		auto sub = best(sub_cs);
		return {base.Cat(lb).Cat(sub).Cat(rb).In(ctx)};
		}

	// Multiple subscripts: format as comma-separated list.
	auto items = collect_args(subs_node->Children());
	return flat_or_fill(base.Fmt(), lb, rb, "", items, ctx);
	}

// Index literal: [$field=expr, ...]
// Children: [0]=LBRACKET ... [last]=RBRACKET
Candidates IndexLiteralNode::Format(const FmtContext& ctx) const
	{
	auto lb = Child(0, Tag::LBracket);
	const auto& rb = Children().back();

	auto items = collect_args(Children());
	if ( items.empty() )
		return {Candidate(Formatting(lb) + rb, ctx)};

	bool has_trailing_comma =
		FindOptChild(Tag::TrailingComma) != nullptr;
	std::string close_pfx = has_trailing_comma ? ", " : "";

	// When every item has a trailing comment, use vertical indented
	// layout (each item on its own line).  Otherwise use fill, which
	// packs items and wraps after any trailing comment.
	if ( has_breaks(items) )
		{
		bool all_trailing = true;
		for ( size_t i = 0; i < items.size(); ++i )
			{
			auto& it = items[i];
			auto nc = (i + 1 < items.size()) ?
					items[i + 1].comma : nullptr;
			bool has_trail = ! it.comment.empty() ||
						(nc && nc->MustBreakAfter());
			if ( ! has_trail )
				all_trailing = false;
			}

		if ( all_trailing )
			return {format_args_vertical(lb, rb, items,
				ctx, has_trailing_comma)};
		}

	return flat_or_fill("", lb, rb, "", items, ctx, "", close_pfx);
	}

// Slice: expr[lo:hi], where either lo or hi may be missing
// Children: [0]=expr [1]=LBRACKET [2]=lo [3]=COLON [4]=hi [5]=RBRACKET
Candidates SliceNode::Format(const FmtContext& ctx) const
	{
	auto lo = best(format_expr(*Child(2), ctx)).Text();
	auto hi = best(format_expr(*Child(4), ctx)).Text();

	auto lb = Child(1, Tag::LBracket);
	auto rb = Child(5, Tag::RBracket);
	auto base_cs = format_expr(*Child(0), ctx);
	const auto& base = best(base_cs);

	auto colon = Child(3, Tag::Colon)->Text();
	auto sep = colon;
	if ( ! lo.empty() && ! hi.empty() )
		sep = " " + sep + " ";

	Candidate flat = base.Cat(lb).Cat(lo + sep + hi).Cat(rb).In(ctx);

	if ( flat.Fits() || lo.empty() || hi.empty() )
		return {flat};

	// Split after ":" - continuation aligns after "[".
	int bracket_col = ctx.Col() + base.Width() + lb->Width();
	FmtContext hi_ctx = ctx.AtCol(bracket_col);
	auto hi2 = best(format_expr(*Child(4), hi_ctx)).Text();

	auto prefix = line_prefix(hi_ctx.Indent(), bracket_col);
	auto split = base.Text() + lb->Text() + lo + " " + colon +
			"\n" + prefix + hi2 + rb->Text();
	int last_w = static_cast<int>(hi2.size()) + rb->Width();
	int split_ovf = ovf(last_w, hi_ctx);
	int lines = 1 + count_lines(hi2);

	return {flat, {split, last_w, lines, split_ovf, ctx.Col()}};
	}

// Paren: (expr)
// Format child 1 bracketed by children 0 and 2.
// Used by PAREN and CARDINALITY.
Candidates PrefixExprNode::FormatBracketed(const FmtContext& ctx) const
	{
	auto lp = Child(0);
	auto rp = Child(2);
	auto inner = best(format_expr(*Child(1), ctx.After(lp->Width())));
	return {Candidate(Formatting(lp), ctx).Cat(inner).Cat(rp).In(ctx)};
	}

// Children: [0]=LPAREN [1]=expr [2]=RPAREN
Candidates ParenNode::Format(const FmtContext& ctx) const
	{
	return FormatBracketed(ctx);
	}

// Cardinality/absolute value: |expr|
// Children: [0]=OP("|") [1]=expr [2]=OP("|")
Candidates CardinalityNode::Format(const FmtContext& ctx) const
	{
	return FormatBracketed(ctx);
	}

// Format "op[sep]child(1)" - shared by Negation (space) and Unary (no space).
Candidates PrefixExprNode::FormatPrefix(const FmtContext& ctx,
                                        const std::string& sep) const
	{
	auto op = Formatting(Child(0, Tag::Op)) + sep;
	Candidate prefix(std::move(op), ctx);
	auto operand = best(format_expr(*Child(1), ctx.After(prefix.Width())));
	return {prefix.Cat(operand).In(ctx)};
	}

// Negation: ! expr (Zeek style: space after !)
// Children: [0]=OP("!") [1]=expr
Candidates NegationNode::Format(const FmtContext& ctx) const
	{
	return FormatPrefix(ctx, " ");
	}

// Unary prefix: -expr, ~expr
// Children: [0]=OP [1]=expr
Candidates UnaryNode::Format(const FmtContext& ctx) const
	{
	return FormatPrefix(ctx, "");
	}

// Boolean chain: operands are direct children (flattened by emitter),
// pack with fill layout breaking at the boolean operator.
Candidates BoolChainNode::Format(const FmtContext& ctx) const
	{
	auto operands = ContentChildren();
	const auto& op = Arg();
	auto sep = " " + op + " ";
	int sep_w = static_cast<int>(sep.size());

	// Try flat.  Only use when every operand fits on one line.
	std::string flat;
	int flat_w = 0;
	bool any_multiline = false;

	for ( size_t i = 0; i < operands.size(); ++i )
		{
		auto cs = format_expr(*operands[i], ctx.After(flat_w));
		auto bc = best(cs);

		if ( bc.Lines() > 1 )
			any_multiline = true;

		if ( i > 0 )
			{
			flat += sep;
			flat_w += sep_w;
			}

		flat += bc.Text();
		flat_w += bc.Width();
		}

	int flat_ovf = ovf(flat_w, ctx);
	if ( flat_ovf == 0 && ! any_multiline )
		return {Candidate(flat, ctx)};

	// Fill: pack operands with " op " separator, wrap at operator.
	FmtContext cont_ctx = ctx.Col() == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(ctx.Col());
	auto pad = line_prefix(cont_ctx.Indent(), cont_ctx.Col());
	int max_col = ctx.MaxCol() - ctx.Trail();

	std::string text;
	int cur_col = ctx.Col();
	int lines = 1;
	int total_overflow = 0;

	for ( size_t i = 0; i < operands.size(); ++i )
		{
		FmtContext sub(cont_ctx.Indent(), cur_col, max_col - cur_col);
		auto bc = best(format_expr(*operands[i], sub));
		int w = bc.Width();

		if ( i == 0 )
			{
			text += bc.Text();
			cur_col += w;
			}

		else
			{
			int need = bc.Lines() > 1 ? max_col + 1 : sep_w + w;

			if ( cur_col + need <= max_col )
				{
				text += sep + bc.Text();
				cur_col += need;
				}

			else
				{
				text += " " + op + "\n" + pad;
				cur_col = cont_ctx.Col();
				++lines;

				FmtContext wrap_sub(cont_ctx.Indent(),
						cur_col, max_col - cur_col);

				auto wcs = format_expr(*operands[i], wrap_sub);
				auto wb = best(wcs);
				text += wb.Text();
				cur_col += wb.Width();
				total_overflow += wb.Ovf();
				}
			}

		if ( bc.Lines() > 1 )
			{
			lines += bc.Lines() - 1;
			cur_col = last_line_len(text);
			}

		total_overflow += bc.Ovf();
		}

	int end_ovf = std::max(0, cur_col - max_col);
	total_overflow += end_ovf;

	return {{text, cur_col, lines, total_overflow, ctx.Col()}};
	}

// Has-field: lhs?$rhs (tight binding, no spaces)
// Children: [0]=expr [1]=OP("?$") [2]=IDENTIFIER
Candidates HasFieldNode::Format(const FmtContext& ctx) const
	{
	auto op_node = Child(1, Tag::Op);
	auto rhs_text = best(format_expr(*Child(2), ctx)).Text();
	int suffix_w = op_node->Width() +
		static_cast<int>(rhs_text.size());

	auto lhs_cs = format_expr(*Child(0), ctx.Reserve(suffix_w));
	auto lhs = best(lhs_cs);

	auto fmt = lhs.Fmt() + op_node + rhs_text;

	if ( lhs.Lines() <= 1 )
		return {Candidate(std::move(fmt), ctx)};

	int last_w = lhs.Width() + suffix_w;
	return {{std::move(fmt), last_w, lhs.Lines(), lhs.Ovf(), ctx.Col()}};
	}

// Shared binary-op formatter.  sep is "" for tight (Div) or " "
// for spaced (Binary).  split_multiline controls whether we offer
// a split candidate when either side is already multi-line.
Candidates BinaryExprNode::FormatBinaryOp(const FmtContext& ctx,
                                          const std::string& sep,
                                          bool split_multiline) const
	{
	auto op = Child(1);
	int sep_w = static_cast<int>(sep.size());
	int op_w = op->Width() + 2 * sep_w;

	auto lhs = best(format_expr(*Child(0), ctx));
	auto rhs = best(format_expr(*Child(2), ctx.After(lhs.Width() + op_w)));

	auto flat = lhs.Text() + sep + op->Text() + sep + rhs.Text();
	int flat_w = lhs.Width() + op_w + rhs.Width();
	int flat_ovf = ovf(flat_w, ctx);
	bool need_split = flat_ovf > 0;
	bool multiline = lhs.Lines() > 1 || rhs.Lines() > 1;

	Candidates result;

	if ( multiline )
		{
		int last_w = last_line_len(flat);
		int lines = count_lines(flat);
		int ovf = text_overflow(flat, ctx.Col(), ctx.MaxCol());
		result.push_back({flat, last_w, lines, ovf, ctx.Col()});

		if ( ! split_multiline )
			return result;

		if ( ovf > 0 )
			need_split = true;
		}
	else
		result.push_back({flat, flat_w, 1, flat_ovf});

	if ( ! need_split )
		return result;

	// Split after operator.  Continuation column: indent one
	// more level when at indent column, otherwise align to
	// expression start.
	FmtContext cont_ctx = ctx.Col() == ctx.IndentCol() ?
				ctx.Indented() : ctx.AtCol(ctx.Col());

	auto rhs2 = best(format_expr(*Child(2), cont_ctx));
	auto cont_prefix = line_prefix(cont_ctx.Indent(), cont_ctx.Col());
	auto split = lhs.Text() + sep + op->Text() + "\n" +
			cont_prefix + rhs2.Text();
	int line1_w = lhs.Width() + sep_w + op->Width();
	int split_lines = 1 + rhs2.Lines();

	int last_w;
	int line2_ovf;

	if ( rhs2.Lines() > 1 )
		{
		last_w = last_line_len(split);
		line2_ovf = rhs2.Ovf();
		}
	else
		{
		last_w = cont_ctx.Col() +
			static_cast<int>(rhs2.Text().size());
		line2_ovf = std::max(0, last_w + cont_ctx.Trail() -
					cont_ctx.MaxCol());
		}

	int split_ovf = ovf_no_trail(line1_w, ctx) + line2_ovf;
	result.push_back({split, last_w, split_lines, split_ovf, ctx.Col()});

	return result;
	}

// Division with atomic RHS: no spaces (subnet masking notation)
// Children: [0]=left [1]=OP("/") [2]=right
Candidates DivNode::Format(const FmtContext& ctx) const
	{
	return FormatBinaryOp(ctx, "", false);
	}

// Binary: lhs op rhs
// Children: [0]=left [1]=OP [2]=right
Candidates BinaryNode::Format(const FmtContext& ctx) const
	{
	return FormatBinaryOp(ctx, " ", true);
	}

// Interval constant: always a space before the unit
Candidates IntervalNode::Format(const FmtContext& ctx) const
	{
	return {Candidate(Arg(0) + " " + Arg(1), ctx)};
	}

// Ternary: cond ? true_val : false_val
// Children: [0]=cond [1]=QUESTION [2]=true_expr [3]=COLON [4]=false_expr
Candidates TernaryNode::Format(const FmtContext& ctx) const
	{
	auto q = Child(1, Tag::Question);
	auto col = Child(3, Tag::Colon);
	int qw = q->Width() + 2;	// " ? "
	int cw = col->Width() + 2;	// " : "

	auto cond = best(format_expr(*Child(0), ctx));
	auto tv_cs = format_expr(*Child(2), ctx.After(cond.Width() + qw));
	auto tv = best(tv_cs);
	auto fv_cs = format_expr(*Child(4),
			ctx.After(cond.Width() + qw + tv.Width() + cw));
	auto fv = best(fv_cs);

	auto flat = cond.Text() + " " + q->Text() + " " + tv.Text() +
			" " + col->Text() + " " + fv.Text();
	Candidate flat_c(flat, ctx);
	if ( flat_c.Fits() )
		return {flat_c};

	Candidates result;
	result.push_back(flat_c);

	// Split after ":" - false-value aligns under true-value.
	int tv_col = ctx.Col() + cond.Width() + qw;
	FmtContext fv_ctx = ctx.AtCol(tv_col);
	fv = best(format_expr(*Child(4), fv_ctx));

	auto fv_prefix = line_prefix(fv_ctx.Indent(), tv_col);
	auto split_colon = cond.Text() + " " + q->Text() + " " +
				tv.Text() + " " + col->Text() + "\n" +
				fv_prefix + fv.Text();
	int last_w = fv.Width();
	int lines = 1 + fv.Lines();
	int split_ovf = ovf(last_w, fv_ctx);

	result.push_back({split_colon, last_w, lines, split_ovf, ctx.Col()});

	// Split after "?" - true and false on continuation line,
	// aligned under the start of cond.
	FmtContext cont_ctx = ctx.AtCol(ctx.Col());
	tv = best(format_expr(*Child(2), cont_ctx));
	fv_cs = format_expr(*Child(4), cont_ctx.After(tv.Width() + cw));
	fv = best(fv_cs);

	auto cont_prefix = line_prefix(cont_ctx.Indent(), ctx.Col());
	auto split_q = cond.Text() + " " + q->Text() + "\n" +
			cont_prefix + tv.Text() + " " + col->Text() +
			" " + fv.Text();
	int q_last_w = tv.Width() + cw + fv.Width();
	int q_ovf = ovf(q_last_w, cont_ctx);

	result.push_back({split_q, q_last_w, 2, q_ovf, ctx.Col()});

	return result;
	}
