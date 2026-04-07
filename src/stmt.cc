#include "fmt_util.h"

// Switch statement: switch expr { case val: body ... }
static void append_case_body(const NodePtr& body, Formatting& result,
                           const FmtContext& ctx)
	{
	if ( ! body )
		return;

	auto text = format_stmt_list(body->Children(), ctx.Indented());
	if ( ! text.Empty() && text.Back() == '\n' )
		text.PopBack();

	result += "\n" + text;
	}

// Switch expression: unwrap parens for Zeek-style ( expr ) spacing.
LayoutItem Node::ComputeSwitchExpr(ComputeCtx& /*cctx*/,
                                   const FmtContext& ctx) const
	{
	auto switch_expr = Child(2);

	if ( switch_expr->GetTag() == Tag::Paren )
		{
		auto pc = switch_expr->ContentChildren();
		if ( ! pc.empty() )
			return Formatting(switch_expr->Child(0, Tag::LParen)) +
				" " + best(format_expr(*pc[0], ctx)).Fmt() +
				" " + switch_expr->Child(2, Tag::RParen);
		}

	return best(format_expr(*switch_expr, ctx)).Fmt();
	}

// Switch cases: format each CASE/DEFAULT with fill-packed values
// and indented bodies.
LayoutItem Node::ComputeSwitchCases(ComputeCtx& /*cctx*/,
                                    const FmtContext& ctx) const
	{
	auto pad = line_prefix(ctx.Indent(), ctx.Col());
	Formatting result;

	for ( const auto& c : Children() )
		{
		if ( c->GetTag() != Tag::Case && c->GetTag() != Tag::Default )
			continue;

		// DEFAULT: [0]=KEYWORD [1]=COLON [optional BODY]
		if ( c->GetTag() == Tag::Default )
			{
			result += "\n" + pad + c->Child(0, Tag::Keyword) +
					c->Child(1, Tag::Colon);
			append_case_body(c->FindOptChild(Tag::Body), result, ctx);
			continue;
			}

		// CASE: [0]=KEYWORD [1]=SP [2]=VALUES [3]=COLON [4]=BODY
		auto values = c->Child(2, Tag::Values);
		auto case_text = Formatting(c->Child(0, Tag::Keyword)) + " ";

		// Collect formatted values and commas.
		std::vector<Formatting> vals;
		NodeVec vcommas;
		NodePtr vpending;

		for ( const auto& vc : values->Children() )
			{
			Tag vt = vc->GetTag();

			if ( vt == Tag::Comma )
				{
				vpending = vc;
				continue;
				}

			if ( vc->IsToken() || vc->IsMarker() )
				continue;

			vals.push_back(best(format_expr(*vc, ctx)).Fmt());
			vcommas.push_back(vpending);
			vpending = nullptr;
			}

		// Fill-pack values, wrapping at comma.
		int case_col = ctx.Col() + case_text.Size();
		auto vpad = line_prefix(ctx.Indent(), case_col);
		int max_col = ctx.MaxCol();
		int cur_col = case_col;

		for ( size_t i = 0; i < vals.size(); ++i )
			{
			auto& vi = vals[i];
			int need = vi.Size();
			if ( i > 0 )
				need += 2;

			if ( i > 0 && cur_col + need > max_col )
				{
				case_text += Formatting(vcommas[i]) + "\n" + vpad;
				cur_col = case_col;
				}

			else if ( i > 0 )
				{
				case_text += Formatting(vcommas[i]) + " ";
				cur_col += 2;
				}

			case_text += vi;
			cur_col += vi.Size();
			}

		case_text += c->Child(3, Tag::Colon);

		result += "\n" + pad + case_text;
		append_case_body(c->FindOptChild(Tag::Body), result, ctx);
		}

	return result;
	}

// Preprocessor directive formatting.  PREPROC-COND has children
// [0]=LPAREN [1]=RPAREN; plain PREPROC has only args.
FmtPtr Node::FormatText() const
	{
	const auto& directive = Arg(0);
	const auto& a = Arg(1);

	if ( GetTag() == Tag::PreprocCond )
		{
		auto result = Formatting(directive + " ") +
				Child(0, Tag::LParen) + " " + a + " " +
				Child(1, Tag::RParen);
		return std::make_shared<Formatting>(std::move(result));
		}

	if ( a.empty() )
		return std::make_shared<Formatting>(directive);

	return std::make_shared<Formatting>(directive + " " + a);
	}

bool Node::OpensDepth() const
	{
	return GetTag() == Tag::PreprocCond || Arg(0) == "@else";
	}

bool Node::ClosesDepth() const
	{
	if ( GetTag() == Tag::PreprocCond )
		return false;
	const auto& d = Arg(0);
	return d == "@else" || d == "@endif";
	}

bool Node::AtColumnZero() const
	{
	if ( GetTag() == Tag::PreprocCond )
		return true;
	const auto& d = Arg(0);
	return d == "@else" || d == "@endif";
	}

// Format a BODY or BLOCK node as a Whitesmith-style braced block.
Formatting Node::FormatWhitesmithBlock(const FmtContext& ctx) const
	{
	auto block_ctx = ctx.Indented();
	auto brace_pad = line_prefix(block_ctx.Indent(), block_ctx.Col());

	// Extract the children between LBRACE and RBRACE, reading
	// trailing comments from the brace tokens themselves.
	auto lb = FindChild(Tag::LBrace);
	auto rb = FindChild(Tag::RBrace);
	auto close_trail = rb->TrailingComment();
	NodeVec inner;

	bool past_open = false;
	for ( const auto& c : Children() )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBrace )
			{
			past_open = true;
			continue;
			}

		if ( t != Tag::RBrace && past_open )
			inner.push_back(c);
		}

	if ( inner.empty() && ! lb->MustBreakAfter() &&
	     ! rb->MustBreakBefore() )
		return "\n" + brace_pad + lb + " " + rb;

	auto body_text = format_stmt_list(inner, block_ctx, true);

	// If the closing brace has a trailing comment, move it
	// to the last statement line, not the '}' itself.
	Formatting rb_fmt(rb);
	if ( ! close_trail.empty() && ! body_text.Empty() &&
	     body_text.Back() == '\n' )
		{
		body_text = body_text.Substr(0, body_text.Size() - 1) +
				close_trail + "\n";
		// Already relocated - use bare brace.
		rb_fmt = rb_fmt.Substr(0,
			rb_fmt.Size() - static_cast<int>(close_trail.size()));
		}

	auto rb_comments = rb->EmitPreComments(brace_pad);
	return "\n" + brace_pad + lb + "\n" +
		body_text + rb_comments + brace_pad + rb_fmt;
	}

// Format a single-statement body (no braces, indented one level).
static Formatting format_single_stmt_body(const Node& body, const FmtContext& ctx)
	{

	auto text = format_stmt_list(body.Children(), ctx.Indented());

	// Strip trailing newline - the parent loop adds its own.
	if ( ! text.Empty() && text.Back() == '\n' )
		text.PopBack();

	return text;
	}

// Format a BODY node: Whitesmith block if first child is BLOCK,
// otherwise indented single-statement body.
FmtPtr Node::FormatBodyText(const FmtContext& ctx) const
	{
	auto content = ContentChildren();
	if ( content.empty() || content[0]->GetTag() != Tag::Block )
		return std::make_shared<Formatting>("\n" +
					format_single_stmt_body(*this, ctx));

	return std::make_shared<Formatting>(
			content[0]->FormatWhitesmithBlock(ctx));
	}
