#include "condition_block.h"
#include "fmt_util.h"

// ------------------------------------------------------------------
// Shared condition-block formatting: keyword ( condition ) body
// ------------------------------------------------------------------

// Children: [0]=KEYWORD [1]=SP [2]=LPAREN ... [rp]=RPAREN [rp+1]=BODY
// RPAREN position varies: 4 for if/while, 7 for for (via RParenPos).
Candidates ConditionBlockNode::Format(const FmtContext& ctx) const
	{
	int rp_pos = RParenPos();
	auto kw_node = Child(0, Tag::Keyword);
	auto lparen_node = Child(2, Tag::LParen);
	auto rparen_node = Child(rp_pos, Tag::RParen);

	// Format the condition assuming the common (non-break) column.
	int cond_col =
		ctx.Col() + kw_node->Width() + 1 + lparen_node->Width() + 1;
	int rp_w = 1 + rparen_node->Width();
	FmtContext cond_ctx(ctx.Indent(), cond_col,
				ctx.MaxCol() - cond_col, rp_w);
	auto cond = BuildCondition(cond_ctx);

	// Build the head via build_layout so trailing comments on keyword
	// or lparen correctly force line breaks.
	LayoutItems los{tok(kw_node), soft_sp, tok(lparen_node), soft_sp, cond,
			" " + rparen_node->Text()};
	auto head_cs = build_layout(los, ctx);
	auto head = best(head_cs).Text();

	auto body_node = Child(rp_pos + 1, Tag::Body);
	auto result = head + body_node->FormatBodyText(ctx) +
			BuildFollowOn(ctx);

	return {Candidate(result, ctx)};
	}

// Default: format the single expression between parens.
std::string ConditionBlockNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	return best(format_expr(*ContentChildren()[0], cond_ctx)).Text();
	}

// ------------------------------------------------------------------
// ForNode: for ( vars in iterable ) body
// ------------------------------------------------------------------

// FOR children: [0]=KEYWORD [1]=SP [2]=LPAREN [3]=VARS
//   [4]=KEYWORD("in") [5]=SP [6]=ITERABLE [7]=RPAREN [8]=BODY
std::string ForNode::BuildCondition(const FmtContext& cond_ctx) const
	{
	auto vars_node = Child(3, Tag::Vars);
	auto in_node = Child(4, Tag::Keyword);
	auto iter_node = Child(6, Tag::Iterable);

	// Format vars (comma-separated identifiers).
	std::string vars_text;
	auto vars_content = vars_node->ContentChildren();
	bool first = true;

	for ( const auto* v : vars_content )
		{
		if ( ! first )
			vars_text += ", ";
		first = false;
		vars_text += best(format_expr(*v, cond_ctx)).Text();
		}

	// Format iterable.
	std::string iter_text;
	auto iter_content = iter_node->ContentChildren();
	if ( ! iter_content.empty() )
		iter_text = best(format_expr(*iter_content[0], cond_ctx)).Text();

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

	// Check for standalone blank lines before the else
	// (not merged with comments).
	bool has_blank = false;
	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Blank )
			has_blank = true;

	auto stmt_pad = line_prefix(ctx.Indent(), ctx.Col());
	auto comments = else_node->EmitPreComments(stmt_pad);

	// Strip trailing newline - the else line provides its own.
	if ( ! comments.empty() && comments.back() == '\n' )
		comments.pop_back();

	std::string result;

	if ( has_blank || ! comments.empty() )
		result += "\n";

	result += comments;

	// ELSE-IF/ELSE-BODY: [0]=KEYWORD [1]=SP [2]=content
	auto else_child = else_node->Child(2);
	auto else_kw = else_node->Child(0, Tag::Keyword)->Text();

	if ( else_node->GetTag() == Tag::ElseIf )
		{
		auto inner_cs = format_expr(*else_child, ctx);
		result += "\n" + stmt_pad + else_kw + " " +
				best(inner_cs).Text();
		}

	else if ( else_child->GetTag() == Tag::Block )
		result += "\n" + stmt_pad + else_kw +
				else_child->FormatWhitesmithBlock(ctx);

	else
		{
		FmtContext else_ctx = ctx.Indented();
		auto cs = format_expr(*else_child, else_ctx);
		auto epad = line_prefix(else_ctx.Indent(), else_ctx.Col());
		result += "\n" + stmt_pad + else_kw + "\n" +
				epad + best(cs).Text();
		}

	return result;
	}
