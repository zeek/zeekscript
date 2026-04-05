#include <cstdio>

#include "fmt_internal.h"
#include "stmt_nodes.h"

// Standalone comment at statement level.
Candidates CommentNode::Format(const FmtContext& ctx) const
	{
	return {Candidate(Arg(), ctx)};
	}

// Append a suffix (semicolon) to each candidate.  When ovf_ctx is
// provided, single-line candidates recompute overflow against it
// (needed when the outer context has its own trail).
static Candidates AppendSuffix(Candidates& cs, const std::string& suffix,
                               int suffix_w, int col,
                               const FmtContext* ovf_ctx = nullptr)
	{
	Candidates result;
	for ( auto& c : cs )
		{
		int w = c.Width() + suffix_w;
		int ovf = (ovf_ctx && c.Lines() == 1) ?
				Ovf(w, *ovf_ctx) : c.Ovf();
		result.push_back({c.Text() + suffix, w, c.Lines(), ovf, col});
		}
	return result;
	}

// Keyword statements with expression list:
//   return [expr], add expr, delete expr, assert expr[, msg],
//   print expr, ...
// Children: [0]=KEYWORD [1]=SP ... [last]=SEMI
Candidates KeywordStmtNode::Format(const FmtContext& ctx) const
	{
	auto keyword = Child(0, Tag::Keyword)->Text();
	auto semi = Children().back().get();
	auto semi_str = semi->Text();
	auto items = CollectArgs(Children());

	if ( items.empty() )
		return {Candidate(keyword + semi_str, ctx)};

	if ( items.size() == 1 )
		return BuildLayout({keyword, SoftSp, items[0].arg,
					semi_str}, ctx);

	int semi_w = semi->Width();
	FmtContext inner = ctx.Reserve(semi_w);
	auto cs = FlatOrFill(keyword + " ", "", "", "", items, inner);

	return AppendSuffix(cs, semi_str, semi_w, ctx.Col());
	}

// Event statement: event name(args);
// Children: [0]=KEYWORD [1]=SP [2]=ARGS [3]=SEMI
Candidates EventStmtNode::Format(const FmtContext& ctx) const
	{
	auto args_node = Child(2, Tag::Args);
	auto semi = Child(3, Tag::Semi);
	auto semi_str = semi->Text();
	auto prefix = Child(0, Tag::Keyword)->Text() + " " + Arg();
	auto lp = args_node->Child(0, Tag::LParen)->Text();
	auto rp = args_node->Children().back()->Text();
	auto items = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {Candidate(prefix + lp + rp + semi_str, ctx)};

	int semi_w = semi->Width();
	FmtContext inner = ctx.Reserve(semi_w);
	auto cs = FlatOrFill(prefix, lp, rp, "", items, inner,
				args_node->TrailingComment());

	return AppendSuffix(cs, semi_str, semi_w, ctx.Col());
	}


// Expression statement: expr ;
// Children: [0]=expr ... [last]=SEMI
Candidates ExprStmtNode::Format(const FmtContext& ctx) const
	{
	auto semi = Children().back().get();
	int semi_w = semi->Width();
	auto expr_cs = FormatExpr(*Child(0), ctx.Reserve(semi_w));
	return AppendSuffix(expr_cs, semi->Text(), semi_w, ctx.Col(), &ctx);
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

	auto body_text = FormatStmtList(body, ctx.Indented());
	auto pad = LinePrefix(ctx.Indent(), ctx.Col());

	int up_indent = ctx.Indent() + 1;
	auto inner_pad = LinePrefix(up_indent, up_indent * INDENT_WIDTH);

	auto rb = Children().back().get();
	auto close = EmitPreComments(*rb, inner_pad) + pad + rb->Text();
	auto kw = Child(0, Tag::Keyword)->Text();
	auto lb = Child(2, Tag::LBrace)->Text();
	auto head = Best(BuildLayout({kw, SoftSp, lb}, ctx)).Text();

	return {Candidate(head + "\n" + body_text + close, ctx)};
	}

// Switch statement: switch expr { case val: body ... }
static void AppendCaseBody(const Node* body, std::string& result,
                           const FmtContext& ctx)
	{
	if ( ! body )
		return;

	auto text = FormatStmtList(body->Children(), ctx.Indented());
	if ( ! text.empty() && text.back() == '\n' )
		text.pop_back();

	result += "\n" + text;
	}

// Children: [0]=KEYWORD [1]=SP [2]=expr [3]=LBRACE ... [last]=RBRACE
Candidates SwitchNode::Format(const FmtContext& ctx) const
	{
	auto switch_expr = Child(2);

	// Format the expression.  If the source used parens, unwrap
	// the PAREN node and apply Zeek-style ( expr ) spacing.
	std::string expr_text;
	if ( switch_expr->GetTag() == Tag::Paren )
		{
		// PAREN: [0]=LPAREN [1]=expr [2]=RPAREN
		auto lp = switch_expr->Child(0, Tag::LParen)->Text();
		auto rp = switch_expr->Child(2, Tag::RParen)->Text();
		auto pc = switch_expr->ContentChildren();
		if ( ! pc.empty() )
			expr_text = lp + " " +
				Best(FormatExpr(*pc[0], ctx)).Text() +
				" " + rp;
		}
	else
		expr_text = Best(FormatExpr(*switch_expr, ctx)).Text();

	auto pad = LinePrefix(ctx.Indent(), ctx.Col());
	auto sw_kw = Child(0, Tag::Keyword)->Text();
	auto lb = Child(3, Tag::LBrace)->Text();
	auto result = sw_kw + " " + expr_text + " " + lb;

	// Format each CASE/DEFAULT.
	for ( const auto& c : Children() )
		{
		if ( c->GetTag() != Tag::Case && c->GetTag() != Tag::Default )
			continue;

		// DEFAULT: [0]=KEYWORD [1]=COLON [optional BODY]
		if ( c->GetTag() == Tag::Default )
			{
			result += "\n" + pad +
				c->Child(0, Tag::Keyword)->Text() +
				c->Child(1, Tag::Colon)->Text();
			AppendCaseBody(c->FindOptChild(Tag::Body), result, ctx);
			continue;
			}

		// CASE: [0]=KEYWORD [1]=SP [2]=VALUES [3]=COLON [4]=BODY
		auto values = c->Child(2, Tag::Values);
		auto case_text = c->Child(0, Tag::Keyword)->Text() + " ";

		// Collect formatted values and commas.
		std::vector<std::string> vals;
		Nodes vcommas;
		const Node* vpending = nullptr;

		for ( const auto& vc : values->Children() )
			{
			Tag vt = vc->GetTag();

			if ( vt == Tag::Comma )
				{
				vpending = vc.get();
				continue;
				}

			if ( vc->IsToken() || vc->IsMarker() )
				continue;

			vals.push_back(Best(FormatExpr(*vc, ctx)).Text());
			vcommas.push_back(vpending);
			vpending = nullptr;
			}

		// Fill-pack values, wrapping at comma.
		int case_col = ctx.Col() + static_cast<int>(case_text.size());
		auto vpad = LinePrefix(ctx.Indent(), case_col);
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

		case_text += c->Child(3, Tag::Colon)->Text();

		result += "\n" + pad + case_text;
		AppendCaseBody(c->FindOptChild(Tag::Body), result, ctx);
		}

	auto rb = Children().back()->Text();
	result += "\n" + pad + rb;

	return {Candidate(result, ctx)};
	}

// PreprocBaseNode methods.  PREPROC-COND (@if/@ifdef/@ifndef) always
// opens depth and sits at column 0.  Plain PREPROC checks the
// directive string for @else/@endif.

std::string PreprocNode::FormatText() const
	{
	const auto& directive = Arg(0);
	const auto& arg = Arg(1);

	if ( arg.empty() )
		return directive;

	return directive + " " + arg;
	}

// Children: [0]=LPAREN [1]=RPAREN
std::string PreprocCondNode::FormatText() const
	{
	const auto& directive = Arg(0);
	const auto& arg = Arg(1);
	auto lp = Child(0, Tag::LParen)->Text();
	auto rp = Child(1, Tag::RParen)->Text();
	return directive + " " + lp + " " + arg + " " + rp;
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

std::string FormatStmtList(const NodeVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks)
	{
	const int max_col = ctx.MaxCol();
	int preproc_depth = 0;
	FmtContext cur_ctx = ctx;
	auto pad = LinePrefix(cur_ctx.Indent(), cur_ctx.Col());

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
				pad = LinePrefix(new_indent, new_col);
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
				pad = LinePrefix(new_indent, new_col);
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
			{
			auto it = FormatDispatch().find(t);
			if ( it == FormatDispatch().end() )
				stmt_text = std::string("/* TODO: ") +
						TagToString(t) + " */";
			else
				stmt_text = Best(it->second(node,
					cur_ctx.Reserve(trail_w))).Text();
			}

		result += pad + stmt_text + semi_str + comment_text + "\n";
		}

	return result;
	}

// Format a BODY or BLOCK node as a Whitesmith-style braced block.
std::string FormatWhitesmithBlock(const Node* body, const FmtContext& ctx)
	{
	auto block_ctx = ctx.Indented();
	auto brace_pad = LinePrefix(block_ctx.Indent(), block_ctx.Col());

	// Extract the children between LBRACE and RBRACE, reading
	// trailing comments from the brace tokens themselves.
	auto lb = body->FindChild(Tag::LBrace);
	auto rb = body->FindChild(Tag::RBrace);
	auto close_trail = rb->TrailingComment();
	NodeVec inner;

	bool past_open = false;
	for ( const auto& c : body->Children() )
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
		return "\n" + brace_pad + lb->Text() + " " + rb->Text();

	auto body_text = FormatStmtList(inner, block_ctx, true);

	// If the closing brace has a trailing comment, move it
	// to the last statement line, not the '}' itself.
	auto rb_text = rb->Text();
	if ( ! close_trail.empty() && ! body_text.empty() &&
	     body_text.back() == '\n' )
		{
		body_text = body_text.substr(0, body_text.size() - 1) +
				close_trail + "\n";
		// Already relocated - use bare brace.
		rb_text = rb_text.substr(0,
			rb_text.size() - close_trail.size());
		}

	auto rb_comments = EmitPreComments(*rb, brace_pad);
	return "\n" + brace_pad + lb->Text() + "\n" +
		body_text + rb_comments + brace_pad + rb_text;
	}

// Format a single-statement body (no braces, indented one level).
static std::string FormatSingleStmtBody(const Node& body, const FmtContext& ctx)
	{

	auto text = FormatStmtList(body.Children(), ctx.Indented());

	// Strip trailing newline - the parent loop adds its own.
	if ( ! text.empty() && text.back() == '\n' )
		text.pop_back();

	return text;
	}

// Format a BODY node: Whitesmith block if first child is BLOCK,
// otherwise indented single-statement body.
std::string FormatBodyText(const Node* body, const FmtContext& ctx)
	{
	auto content = body->ContentChildren();
	if ( content.empty() || content[0]->GetTag() != Tag::Block )
		return "\n" + FormatSingleStmtBody(*body, ctx);

	return FormatWhitesmithBlock(content[0], ctx);
	}
