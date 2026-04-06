#include <cstdio>

#include "fmt_util.h"
#include "stmt.h"

// Standalone comment at statement level.
Candidates CommentNode::Format(const FmtContext& ctx) const
	{
	return {Candidate(Arg(), ctx)};
	}

// Append a suffix (semicolon) to each candidate.  When ovf_ctx is
// provided, single-line candidates recompute overflow against it
// (needed when the outer context has its own trail).
static Candidates append_suffix(Candidates& cs, const NodePtr& suffix, int col,
                               const FmtContext* ovf_ctx = nullptr)
	{
	int suffix_w = suffix->Width();
	Candidates result;
	for ( auto& c : cs )
		{
		int w = c.Width() + suffix_w;
		int overflow = (ovf_ctx && c.Lines() == 1) ?
				ovf(w, *ovf_ctx) : c.Ovf();
		result.push_back({c.Fmt() + suffix, w, c.Lines(), overflow, col});
		}
	return result;
	}

// Keyword statements with expression list:
//   return [expr], add expr, delete expr, assert expr[, msg],
//   print expr, ...
// Children: [0]=KEYWORD [1]=SP ... [last]=SEMI
Candidates KeywordStmtNode::Format(const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	const auto& semi = Children().back();
	auto items = collect_args(Children());

	if ( items.empty() )
		return BuildLayout({0U, last()}, ctx);

	if ( items.size() == 1 )
		return BuildLayout({0U, soft_sp, items[0].arg, last()}, ctx);

	int semi_w = semi->Width();
	FmtContext inner = ctx.Reserve(semi_w);
	auto cs = flat_or_fill(Formatting(kw) + " ", "", "", "", items, inner);

	return append_suffix(cs, semi, ctx.Col());
	}

// Event statement: event name(args);
// Children: [0]=KEYWORD [1]=SP [2]=ARGS [3]=SEMI
Candidates EventStmtNode::Format(const FmtContext& ctx) const
	{
	auto args_node = Child(2, Tag::Args);
	auto semi = Child(3, Tag::Semi);
	auto prefix = Formatting(Child(0, Tag::Keyword)) + " " + Arg();
	auto lp = args_node->Child(0, Tag::LParen);
	const auto& rp = args_node->Children().back();
	auto items = collect_args(args_node->Children());

	if ( items.empty() )
		return {Candidate(prefix + lp + rp + semi, ctx)};

	int semi_w = semi->Width();
	FmtContext inner = ctx.Reserve(semi_w);
	auto cs = flat_or_fill(prefix, lp, rp, "", items, inner,
				args_node->TrailingComment());

	return append_suffix(cs, semi, ctx.Col());
	}


// Expression statement: expr ;
// Children: [0]=expr ... [last]=SEMI
Candidates ExprStmtNode::Format(const FmtContext& ctx) const
	{
	return BuildLayout({expr(0), last()}, ctx);
	}

// Export declaration: export { decls }
// Children: [0]=KEYWORD [1]=SP [2]=LBRACE ... [last]=RBRACE
Candidates ExportNode::Format(const FmtContext& ctx) const
	{
	// Collect non-token children for the body.
	NodeVec body;
	for ( const auto& c : Children() )
		if ( ! c->IsToken() )
			body.push_back(c);

	auto body_text = format_stmt_list(body, ctx.Indented());
	auto pad = line_prefix(ctx.Indent(), ctx.Col());

	int up_indent = ctx.Indent() + 1;
	auto inner_pad = line_prefix(up_indent, up_indent * INDENT_WIDTH);

	const auto& rb = Children().back();
	auto close = rb->EmitPreComments(inner_pad) + pad + rb;
	auto head = best(BuildLayout({0U, soft_sp, 2}, ctx)).Fmt();

	auto fmt = head + "\n" + body_text + close;
	return {Candidate(std::move(fmt), ctx)};
	}

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

// Children: [0]=KEYWORD [1]=SP [2]=expr [3]=LBRACE ... [last]=RBRACE
Candidates SwitchNode::Format(const FmtContext& ctx) const
	{
	auto switch_expr = Child(2);

	// Format the expression.  If the source used parens, unwrap
	// the PAREN node and apply Zeek-style ( expr ) spacing.
	Formatting expr_text;
	if ( switch_expr->GetTag() == Tag::Paren )
		{
		// PAREN: [0]=LPAREN [1]=expr [2]=RPAREN
		auto pc = switch_expr->ContentChildren();
		if ( ! pc.empty() )
			{
			expr_text = Formatting(switch_expr->Child(0, Tag::LParen)) +
				" " + best(format_expr(*pc[0], ctx)).Fmt() +
				" " + switch_expr->Child(2, Tag::RParen);
			}
		}
	else
		expr_text += best(format_expr(*switch_expr, ctx)).Fmt();

	auto pad = line_prefix(ctx.Indent(), ctx.Col());
	auto result = Formatting(Child(0, Tag::Keyword)) +
			" " + expr_text + " " + Child(3, Tag::LBrace);

	// Format each CASE/DEFAULT.
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

	result += "\n" + pad;
	result += Children().back();

	return {Candidate(std::move(result), ctx)};
	}

// PreprocBaseNode methods.  PREPROC-COND (@if/@ifdef/@ifndef) always
// opens depth and sits at column 0.  Plain PREPROC checks the
// directive string for @else/@endif.

FmtPtr PreprocNode::FormatText() const
	{
	const auto& directive = Arg(0);
	const auto& arg = Arg(1);

	if ( arg.empty() )
		return std::make_shared<Formatting>(directive);

	return std::make_shared<Formatting>(directive + " " + arg);
	}

// Children: [0]=LPAREN [1]=RPAREN
FmtPtr PreprocCondNode::FormatText() const
	{
	const auto& directive = Arg(0);
	const auto& arg = Arg(1);
	auto result = Formatting(directive + " ") + Child(0, Tag::LParen) +
			" " + arg + " " + Child(1, Tag::RParen);
	return std::make_shared<Formatting>(std::move(result));
	}

bool PreprocBaseNode::OpensDepth() const
	{
	return GetTag() == Tag::PreprocCond || Arg(0) == "@else";
	}

bool PreprocBaseNode::ClosesDepth() const
	{
	if ( GetTag() == Tag::PreprocCond )
		return false;
	const auto& d = Arg(0);
	return d == "@else" || d == "@endif";
	}

bool PreprocBaseNode::AtColumnZero() const
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
