#include <algorithm>

#include "fmt_internal.h"

// ------------------------------------------------------------------
// Atoms
// ------------------------------------------------------------------

Candidates FormatAtom(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(), ctx)};
	}

// ------------------------------------------------------------------
// Field access: rec$field
// ------------------------------------------------------------------

Candidates FormatFieldAccess(const Node& node, const FmtContext& ctx)
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

Candidates FormatFieldAssign(const Node& node, const FmtContext& ctx)
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
// Call: func(args)
// ------------------------------------------------------------------

Candidates FormatCall(const Node& node, const FmtContext& ctx)
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

	auto items = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {func.Cat(lp->Text() + rp->Text()).In(ctx)};

	return FlatOrFill(func.Text(), lp->Text(), rp->Text(), "",
		items, ctx, args_node->TrailingComment());
	}

// ------------------------------------------------------------------
// Schedule: schedule interval { event() }
// ------------------------------------------------------------------

Candidates FormatSchedule(const Node& node, const FmtContext& ctx)
	{
	const Node* kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	auto content = node.ContentChildren();
	if ( content.size() < 2 )
		throw FormatError("SCHEDULE node needs 2 content children");

	return BuildLayout({kw->Text(), SoftSp, content[0],
		SoftSp, lb->Text(), SoftSp, content[1],
		SoftSp, rb->Text()}, ctx);
	}

// ------------------------------------------------------------------
// Constructor: table(...), set(...), vector(...)
// ------------------------------------------------------------------

Candidates FormatConstructor(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* lp_node = node.FindChild(Tag::LParen);
	const Node* rp_node = node.FindChild(Tag::RParen);
	std::string kw = kw_node->Text();
	std::string lp = lp_node->Text();
	std::string rp = rp_node->Text();

	auto items = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate(kw + lp + rp, ctx)};

	Candidates result;

	// Candidate 1: flat (only when no forced breaks).
	if ( ! HasBreaks(items) )
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

Candidates FormatIndex(const Node& node, const FmtContext& ctx)
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
	auto items = CollectArgs(subs_node->Children());
	return FlatOrFill(base.Text(), lb->Text(), rb->Text(), "",
		items, ctx);
	}

// ------------------------------------------------------------------
// Index literal: [$field=expr, ...]
// ------------------------------------------------------------------

Candidates FormatIndexLiteral(const Node& node, const FmtContext& ctx)
	{
	const Node* lb = node.FindChild(Tag::LBracket);
	const Node* rb = node.FindChild(Tag::RBracket);
	std::string lbt = lb->Text();
	std::string rbt = rb->Text();

	auto items = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate(lbt + rbt, ctx)};

	bool has_trailing_comma =
		node.FindOptChild(Tag::TrailingComma) != nullptr;
	std::string close_pfx = has_trailing_comma ? ", " : "";

	// When every item has a trailing comment, use vertical indented
	// layout (each item on its own line).  Otherwise use fill, which
	// packs items and wraps after any trailing comment.
	if ( HasBreaks(items) )
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

	return FlatOrFill("", lbt, rbt, "", items, ctx, "", close_pfx);
	}

// ------------------------------------------------------------------
// Slice: expr[lo:hi]
// ------------------------------------------------------------------

Candidates FormatSlice(const Node& node, const FmtContext& ctx)
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

Candidates FormatParen(const Node& node, const FmtContext& ctx)
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

Candidates FormatUnary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	auto content = node.ContentChildren();

	if ( content.empty() )
		throw FormatError("UNARY-OP node needs a child");

	// Cardinality/absolute value: |expr|
	if ( op == "|...|" )
		{
		const Node* lp = node.FindChild(Tag::Op);
		const Node* rp = node.FindChild(Tag::Op, lp);
		auto operand_cs = FormatExpr(*content[0],
			ctx.After(lp->Width()));
		const auto& operand = Best(operand_cs);
		return {Candidate(lp->Text(), ctx).Cat(operand)
			.Cat(rp->Text()).In(ctx)};
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

Candidates FormatBinary(const Node& node, const FmtContext& ctx)
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
		const Node* op_node = node.FindChild(Tag::Op);
		std::string rhs_text =
			Best(FormatExpr(*content[1], ctx)).Text();
		int suffix_w = op_node->Width() +
			static_cast<int>(rhs_text.size());

		auto lhs_cs = FormatExpr(*content[0], ctx.Reserve(suffix_w));
		const auto& lhs = Best(lhs_cs);

		std::string text = lhs.Text() + op_node->Text() + rhs_text;

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

Candidates FormatInterval(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(0) + " " + node.Arg(1), ctx)};
	}

// ------------------------------------------------------------------
// Ternary: cond ? true_val : false_val
// ------------------------------------------------------------------

Candidates FormatTernary(const Node& node, const FmtContext& ctx)
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
