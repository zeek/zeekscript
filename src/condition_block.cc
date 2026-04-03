#include "condition_block.h"

// ------------------------------------------------------------------
// Shared condition-block formatting: keyword ( condition ) body
// ------------------------------------------------------------------

Candidates ConditionBlockNode::Format(const FmtContext& ctx) const
	{
	const Node* kw_node = FindChild(Tag::Keyword);
	const Node* lparen_node = FindChild(Tag::LParen);
	const Node* rparen_node = FindChild(Tag::RParen);

	// Format the condition assuming the common (non-break) column.
	int cond_col = ctx.Col() + kw_node->Width() + 1
		+ lparen_node->Width() + 1;
	int rp_w = 1 + rparen_node->Width();
	FmtContext cond_ctx(ctx.Indent(), cond_col,
		ctx.MaxCol() - cond_col, rp_w);
	std::string cond = BuildCondition(cond_ctx);

	// Build the head via BuildLayout so trailing comments on
	// keyword or lparen correctly force line breaks.
	auto head_cs = BuildLayout({Tok(kw_node), SoftSp,
		Tok(lparen_node), SoftSp, cond,
		" " + rparen_node->Text()}, ctx);
	std::string head = Best(head_cs).Text();

	const Node* body_node = FindChild(Tag::Body);
	std::string result = head + FormatBodyText(body_node, ctx);

	// Check for blank lines after the body (e.g., before else).
	bool has_blank = false;

	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Blank )
			has_blank = true;

	// Collect pre-comments from the follow-on node (e.g., Else).
	std::string comments;
	std::string stmt_pad = LinePrefix(ctx.Indent(), ctx.Col());
	const Node* else_node = FindOptChild(Tag::Else);

	if ( else_node )
		for ( const auto& pc : else_node->PreComments() )
			{
			if ( ! comments.empty() || has_blank )
				comments += "\n";

			comments += stmt_pad + pc;
			has_blank = false;
			}

	result += BuildFollowOn(ctx, comments, has_blank);

	return {Candidate(result, ctx)};
	}

// Default: format the single expression between parens.
std::string ConditionBlockNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	// First content child is the condition expression.
	auto content = ContentChildren();
	return Best(FormatExpr(*content[0], cond_ctx)).Text();
	}

// ------------------------------------------------------------------
// ForNode: for ( vars in iterable ) body
// ------------------------------------------------------------------

std::string ForNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	const Node* vars_node = FindChild(Tag::Vars);
	const Node* for_kw = FindChild(Tag::Keyword);
	const Node* in_node = FindChild(Tag::Keyword, for_kw);
	const Node* iter_node = FindChild(Tag::Iterable);

	// Format vars (comma-separated identifiers).
	std::string vars_text;
	auto vars_content = vars_node->ContentChildren();
	bool first = true;
	for ( const auto* v : vars_content )
		{
		if ( ! first )
			vars_text += ", ";
		first = false;
		vars_text += Best(FormatExpr(*v, cond_ctx)).Text();
		}

	// Format iterable.
	std::string iter_text;
	auto iter_content = iter_node->ContentChildren();
	if ( ! iter_content.empty() )
		iter_text = Best(FormatExpr(
			*iter_content[0], cond_ctx)).Text();

	return vars_text + " " + in_node->Text() + " " + iter_text;
	}

// ------------------------------------------------------------------
// IfNode: else clause follow-on
// ------------------------------------------------------------------

std::string IfNode::BuildFollowOn(const FmtContext& ctx,
	const std::string& comments, bool has_blank) const
	{
	const Node* else_node = FindOptChild(Tag::Else);

	if ( ! else_node )
		return "";

	auto else_content = else_node->ContentChildren();
	const Node* else_child = else_content[0];
	std::string stmt_pad = LinePrefix(ctx.Indent(), ctx.Col());
	std::string result;

	if ( has_blank || ! comments.empty() )
		result += "\n";

	result += comments;

	if ( else_child->GetTag() == Tag::If )
		{
		auto inner_cs = FormatExpr(*else_child, ctx);
		result += "\n" + stmt_pad + "else " + Best(inner_cs).Text();
		}

	else if ( else_child->GetTag() == Tag::Block )
		result += "\n" + stmt_pad + "else" +
			FormatWhitesmithBlock(else_child, ctx);

	else
		{
		FmtContext else_ctx = ctx.Indented();
		auto cs = FormatExpr(*else_child, else_ctx);
		std::string epad = LinePrefix(
			else_ctx.Indent(), else_ctx.Col());
		result += "\n" + stmt_pad + "else\n" + epad + Best(cs).Text();
		}

	return result;
	}
