#include "condition_block.h"

// ------------------------------------------------------------------
// Shared condition-block formatting: keyword ( condition ) body
// ------------------------------------------------------------------

Candidates ConditionBlockNode::Format(const FmtContext& ctx) const
	{
	auto kw_node = FindChild(Tag::Keyword);
	auto lparen_node = FindChild(Tag::LParen);
	auto rparen_node = FindChild(Tag::RParen);

	// Format the condition assuming the common (non-break) column.
	int cond_col =
		ctx.Col() + kw_node->Width() + 1 + lparen_node->Width() + 1;
	int rp_w = 1 + rparen_node->Width();
	FmtContext cond_ctx(ctx.Indent(), cond_col,
				ctx.MaxCol() - cond_col, rp_w);
	auto cond = BuildCondition(cond_ctx);

	// Build the head via BuildLayout so trailing comments on keyword
	// or lparen correctly force line breaks.
	LayoutItems los{Tok(kw_node), SoftSp, Tok(lparen_node), SoftSp, cond,
			" " + rparen_node->Text()};
	auto head_cs = BuildLayout(los, ctx);
	auto head = Best(head_cs).Text();

	auto body_node = FindChild(Tag::Body);
	auto result = head + FormatBodyText(body_node, ctx) +
			BuildFollowOn(ctx);

	return {Candidate(result, ctx)};
	}

// Default: format the single expression between parens.
std::string ConditionBlockNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	return Best(FormatExpr(*ContentChildren()[0], cond_ctx)).Text();
	}

// ------------------------------------------------------------------
// ForNode: for ( vars in iterable ) body
// ------------------------------------------------------------------

std::string ForNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	auto vars_node = FindChild(Tag::Vars);
	auto for_kw = FindChild(Tag::Keyword);
	auto in_node = FindChild(Tag::Keyword, for_kw);
	auto iter_node = FindChild(Tag::Iterable);

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
		iter_text = Best(FormatExpr(*iter_content[0], cond_ctx)).Text();

	return vars_text + " " + in_node->Text() + " " + iter_text;
	}

// ------------------------------------------------------------------
// IfElseNode: else clause follow-on
// ------------------------------------------------------------------

// Find the ElseIf or ElseBody child.
static const Node* find_else(const Node& node)
	{
	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::ElseIf || t == Tag::ElseBody )
			return c.get();
		}

	return nullptr;
	}

std::string IfElseNode::BuildFollowOn(const FmtContext& ctx) const
	{
	auto else_node = find_else(*this);

	// Check for blank lines before the else.
	bool has_blank = false;
	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Blank )
			has_blank = true;

	// Collect pre-comments from the else node.
	std::string comments;
	auto stmt_pad = LinePrefix(ctx.Indent(), ctx.Col());

	for ( const auto& pc : else_node->PreComments() )
		{
		if ( ! comments.empty() || has_blank )
			comments += "\n";

		comments += stmt_pad + pc;
		has_blank = false;
		}

	std::string result;

	if ( has_blank || ! comments.empty() )
		result += "\n";

	result += comments;

	auto else_child = else_node->ContentChildren()[0];
	auto else_kw = else_node->FindChild(Tag::Keyword)->Text();

	if ( else_node->GetTag() == Tag::ElseIf )
		{
		auto inner_cs = FormatExpr(*else_child, ctx);
		result += "\n" + stmt_pad + else_kw + " " +
				Best(inner_cs).Text();
		}

	else if ( else_child->GetTag() == Tag::Block )
		result += "\n" + stmt_pad + else_kw +
				FormatWhitesmithBlock(else_child, ctx);

	else
		{
		FmtContext else_ctx = ctx.Indented();
		auto cs = FormatExpr(*else_child, else_ctx);
		auto epad = LinePrefix(else_ctx.Indent(), else_ctx.Col());
		result += "\n" + stmt_pad + else_kw + "\n" +
				epad + Best(cs).Text();
		}

	return result;
	}
