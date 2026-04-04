#include <cstdio>

#include "condition_block.h"
#include "fmt_internal.h"

// ------------------------------------------------------------------
// Simple keyword statements: return [expr], add expr, delete expr
// ------------------------------------------------------------------

Candidates FormatKeywordStmt(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	std::string keyword = kw_node->Text();
	const Node* expr = nullptr;
	const Node* semi = nullptr;

	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			semi = c.get();
		else if ( ! is_comment(t) && ! is_token(t) && ! expr )
			expr = c.get();
		}

	std::string semi_str = semi ? semi->Text() : "";

	if ( ! expr )
		return {Candidate(keyword + semi_str, ctx)};

	return BuildLayout({keyword, SoftSp, expr, semi_str}, ctx);
	}

// ------------------------------------------------------------------
// Event statement: event name(args);
// ------------------------------------------------------------------

Candidates FormatEventStmt(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	std::string name = node.Arg();
	const Node* args_node = node.FindChild(Tag::Args);
	const Node* semi = node.FindOptChild(Tag::Semi);
	std::string semi_str = semi ? semi->Text() : "";

	std::string prefix = kw_node->Text() + " " + name;

	const Node* lp = args_node->FindChild(Tag::LParen);
	const Node* rp = args_node->FindChild(Tag::RParen);

	auto items = CollectArgs(args_node->Children());

	if ( items.empty() )
		return {Candidate(prefix + lp->Text() + rp->Text() +
			semi_str, ctx)};

	int semi_w = semi ? semi->Width() : 0;
	FmtContext inner = semi ? ctx.Reserve(semi_w) : ctx;
	auto cs = FlatOrFill(prefix, lp->Text(), rp->Text(), "",
		items, inner, args_node->TrailingComment());

	Candidates result;
	for ( auto& c : cs )
		{
		std::string text = c.Text() + semi_str;
		int w = c.Width() + semi_w;
		result.push_back({text, w, c.Lines(), c.Ovf(), ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Print statement: print expr, expr, ...
// ------------------------------------------------------------------

Candidates FormatPrint(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* semi = node.FindChild(Tag::Semi);
	std::string semi_str = semi->Text();
	std::string prefix = kw_node->Text();

	auto items = CollectArgs(node.Children());

	if ( items.empty() )
		return {Candidate(prefix + semi_str, ctx)};

	if ( items.size() == 1 )
		return BuildLayout({prefix, SoftSp, items[0].arg,
			semi_str}, ctx);

	int semi_w = semi->Width();
	FmtContext inner = ctx.Reserve(semi_w);
	auto cs = FlatOrFill(prefix + " ", "", "", "", items, inner);

	Candidates result;
	for ( auto& c : cs )
		{
		std::string text = c.Text() + semi_str;
		int w = c.Width() + semi_w;
		result.push_back({text, w, c.Lines(), c.Ovf(), ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Expression statement: expr ;
// ------------------------------------------------------------------

Candidates FormatExprStmt(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("EXPR-STMT node needs children");

	const Node* expr = nullptr;
	const Node* semi = nullptr;

	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			semi = c.get();

		else if ( ! is_comment(t) && ! expr )
			expr = c.get();
		}

	if ( ! expr )
		{
		std::string st = semi ? semi->Text() : "";
		return {Candidate(st, ctx)};
		}

	// Reserve trailing space for the semicolon.
	std::string semi_str = semi ? semi->Text() : "";
	int semi_w = semi ? semi->Width() : 0;
	FmtContext expr_ctx = ctx.Reserve(semi_w);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = ec.Text() + semi_str;
		int w = ec.Width() + semi_w;

		int ovf = ec.Ovf();
		if ( ec.Lines() == 1 )
			ovf = Ovf(w, ctx);

		result.push_back({text, w, ec.Lines(), ovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Condition-block dispatch (if, for, while)
// ------------------------------------------------------------------

Candidates FormatCondBlock(const Node& node, const FmtContext& ctx)
	{
	return static_cast<const ConditionBlockNode&>(node).Format(ctx);
	}

// ------------------------------------------------------------------
// Export declaration: export { decls }
// ------------------------------------------------------------------

Candidates FormatExport(const Node& node, const FmtContext& ctx)
	{
	const Node* kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	// Collect non-token children for the body.
	NodeVec body;
	for ( const auto& c : node.Children() )
		if ( ! is_token(c->GetTag()) )
			body.push_back(c);

	std::string body_text = FormatStmtList(body, ctx.Indented());
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());
	std::string inner_pad = LinePrefix(ctx.Indent() + 1,
		(ctx.Indent() + 1) * INDENT_WIDTH);
	std::string close = EmitPreComments(*rb, inner_pad) +
		pad + rb->Text();

	std::string head = Best(BuildLayout({kw->Text(), SoftSp,
		lb->Text()}, ctx)).Text();
	return {Candidate(head + "\n" + body_text + close, ctx)};
	}

// ------------------------------------------------------------------
// Switch statement: switch expr { case val: body ... }
// ------------------------------------------------------------------

static void AppendCaseBody(const Node* body, std::string& result,
                           const FmtContext& ctx)
	{
	if ( ! body )
		return;

	std::string text = FormatStmtList(body->Children(), ctx.Indented());
	if ( ! text.empty() && text.back() == '\n' )
		text.pop_back();
	result += "\n" + text;
	}

Candidates FormatSwitch(const Node& node, const FmtContext& ctx)
	{
	const Node* sw_kw = node.FindChild(Tag::Keyword);
	const Node* lb = node.FindChild(Tag::LBrace);
	const Node* rb = node.FindChild(Tag::RBrace);

	// Find the switch expression: first content child.
	const Node* switch_expr = nullptr;
	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( ! is_token(t) && ! is_comment(t) )
			{ switch_expr = c.get(); break; }
		}

	// Format the expression.  If the source used parens, unwrap
	// the PAREN node and apply Zeek-style ( expr ) spacing.
	std::string expr_text;
	if ( switch_expr )
		{
		if ( switch_expr->GetTag() == Tag::Paren )
			{
			const Node* lp = switch_expr->FindChild(Tag::LParen);
			const Node* rp = switch_expr->FindChild(Tag::RParen);
			auto paren_content = switch_expr->ContentChildren();
			if ( ! paren_content.empty() )
				{
				auto cs = FormatExpr(*paren_content[0], ctx);
				expr_text = lp->Text() + " " +
					Best(cs).Text() + " " + rp->Text();
				}
			}
		else
			expr_text = Best(FormatExpr(*switch_expr, ctx)).Text();
		}

	std::string head = sw_kw->Text() + " " + expr_text + " " + lb->Text();
	std::string pad = LinePrefix(ctx.Indent(), ctx.Col());

	std::string result = head;

	// Format each CASE/DEFAULT.
	for ( const auto& c : node.Children() )
		{
		if ( c->GetTag() != Tag::Case && c->GetTag() != Tag::Default )
			continue;

		if ( c->GetTag() == Tag::Default )
			{
			const Node* dkw = c->FindChild(Tag::Keyword);
			const Node* dcol = c->FindChild(Tag::Colon);
			result += "\n" + pad + dkw->Text() + dcol->Text();
			AppendCaseBody(c->FindOptChild(Tag::Body),
				result, ctx);
			continue;
			}

		// CASE: KEYWORD "case" VALUES {...} COLON BODY {...}
		const Node* ckw = c->FindChild(Tag::Keyword);
		const Node* ccol = c->FindChild(Tag::Colon);
		const Node* values = c->FindChild(Tag::Values);
		const Node* body = c->FindOptChild(Tag::Body);

		std::string case_text = ckw->Text() + " ";

		// Collect formatted values and commas.
		std::vector<std::string> vals;
		Nodes vcommas;
		const Node* vpending = nullptr;
		for ( const auto& vc : values->Children() )
			{
			Tag vt = vc->GetTag();
			if ( vt == Tag::Comma )
				vpending = vc.get();
			else if ( ! is_token(vt) && ! is_comment(vt) &&
			          ! is_marker(vt) )
				{
				vals.push_back(
					Best(FormatExpr(*vc, ctx)).Text());
				vcommas.push_back(vpending);
				vpending = nullptr;
				}
			}

		// Fill-pack values, wrapping at comma.
		int case_col = ctx.Col() +
			static_cast<int>(case_text.size());
		std::string vpad = LinePrefix(ctx.Indent(), case_col);
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

		case_text += ccol->Text();

		result += "\n" + pad + case_text;
		AppendCaseBody(body, result, ctx);
		}

	result += "\n" + pad + rb->Text();

	return {Candidate(result, ctx)};
	}

// ------------------------------------------------------------------
// Block/body formatting: Whitesmith brace style
// ------------------------------------------------------------------

// Format a PREPROC directive.  Returns the text (always at column 0).
// Conditional directives have LPAREN/RPAREN children for "( arg )" spacing.
static std::string FormatPreproc(const Node& node)
	{
	const auto& directive = node.Arg(0);
	const auto& arg = node.Arg(1);

	if ( arg.empty() )
		return directive;

	// @if, @ifdef, @ifndef have LPAREN/RPAREN children.
	const Node* lp = node.FindOptChild(Tag::LParen);
	const Node* rp = node.FindOptChild(Tag::RParen);
	if ( lp && rp )
		return directive + " " + lp->Text() + " " + arg +
			" " + rp->Text();

	// @load, @load-sigs, etc. use space.
	return directive + " " + arg;
	}

static bool preproc_opens(const std::string& directive)
	{
	return directive == "@if" || directive == "@ifdef" ||
		directive == "@ifndef" || directive == "@else";
	}

static bool preproc_closes(const std::string& directive)
	{
	return directive == "@else" || directive == "@endif";
	}

std::string FormatStmtList(const NodeVec& nodes,
                           const FmtContext& ctx,
                           bool skip_leading_blanks)
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

		result += EmitPreComments(node, pad);

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

		// COMMENT-TRAILING nodes are handled by the parser
		// (attached to preceding node) or by the parent
		// (e.g. after open brace).  Skip them here.
		if ( t == Tag::CommentTrailing )
			continue;

		if ( is_comment(t) )
			{
			result += pad + node.Arg() + "\n";
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

		std::string semi_str = sibling_semi
			? sibling_semi->Text() : "";

		// Check for trailing comment on the node or its SEMI.
		std::string comment_text = node.TrailingComment();
		if ( comment_text.empty() && sibling_semi )
			comment_text = sibling_semi->TrailingComment();

		int comment_w = static_cast<int>(comment_text.size());
		int trail_w = static_cast<int>(semi_str.size()) + comment_w;

		std::string stmt_text;

		// Bare KEYWORD at statement level: break, next, etc.
		if ( t == Tag::Keyword )
			{
			stmt_text = node.Arg();
			}
		else
			{
			auto it = FormatDispatch().find(t);

			if ( it != FormatDispatch().end() )
				{
				FmtContext stmt_ctx = cur_ctx.Reserve(trail_w);
				auto cs = it->second(node, stmt_ctx);
				stmt_text = Best(cs).Text();
				}
			else
				{
				const char* s = TagToString(t);
				stmt_text = std::string("/* TODO: ") +
					s + " */";
				}
			}

		result += pad + stmt_text + semi_str + comment_text + "\n";
		}

	return result;
	}

// Format a BODY or BLOCK node as a Whitesmith-style braced block.
std::string FormatWhitesmithBlock(const Node* body,
                                         const FmtContext& ctx)
	{
	FmtContext block_ctx = ctx.Indented();
	std::string brace_pad = LinePrefix(block_ctx.Indent(), block_ctx.Col());

	if ( ! body )
		return "\n" + brace_pad + "{ }";

	// Extract the children between LBRACE and RBRACE, reading
	// trailing comments from the brace tokens themselves.
	const Node* lb = body->FindChild(Tag::LBrace);
	const Node* rb = body->FindChild(Tag::RBrace);
	const auto& kids = body->Children();
	std::string close_trail = rb->TrailingComment();
	NodeVec inner;

	bool past_open = false;
	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBrace )
			{
			past_open = true;
			continue;
			}

		if ( t == Tag::RBrace )
			continue;

		if ( past_open )
			inner.push_back(c);
		}

	if ( inner.empty() && ! lb->MustBreakAfter() )
		return "\n" + brace_pad + lb->Text() + " " +
			rb->Text();

	std::string body_text =
		FormatStmtList(inner, block_ctx, true);

	// If the closing brace has a trailing comment, move it
	// to the last statement line, not the '}' itself.
	std::string rb_text = rb->Text();
	if ( ! close_trail.empty() && ! body_text.empty() &&
	     body_text.back() == '\n' )
		{
		body_text = body_text.substr(0, body_text.size() - 1)
			+ close_trail + "\n";
		// Already relocated - use bare brace.
		rb_text = rb_text.substr(0,
			rb_text.size() - close_trail.size());
		}

	return "\n" + brace_pad + lb->Text() + "\n" +
		body_text + brace_pad + rb_text;
	}

// Format a single-statement body (no braces, indented one level).
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
std::string FormatBodyText(const Node* body, const FmtContext& ctx)
	{
	if ( ! body || body->Children().empty() )
		return "";

	auto content = body->ContentChildren();
	if ( ! content.empty() && content[0]->GetTag() == Tag::Block )
		return FormatWhitesmithBlock(content[0], ctx);

	return "\n" + FormatSingleStmtBody(body, ctx);
	}
