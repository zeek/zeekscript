#include "fmt_util.h"
#include "layout.h"
#include "node.h"

// Condition compute for for-loop: rparen at position 7.
// FOR: [0]=KW [1]=SP [2]=LPAREN [3]=VARS [4]=KW("in")
//   [5]=SP [6]=ITERABLE [7]=RPAREN [8]=BODY
LayoutItem Node::ComputeForCond(ComputeCtx& /*cctx*/,
                                 const FmtContext& ctx) const
	{
	auto kw = Child(0, Tag::Keyword);
	auto lp = Child(2, Tag::LParen);
	auto rp = Child(7, Tag::RParen);

	int cond_col = ctx.Col() + kw->Width() + 1 + lp->Width() + 1;
	int rp_w = 1 + rp->Width();
	FmtContext cond_ctx(ctx.Indent(), cond_col,
				ctx.MaxCol() - cond_col, rp_w);

	auto vars_node = Child(3, Tag::Vars);
	auto in_node = Child(4, Tag::Keyword);
	auto iter_node = Child(6, Tag::Iterable);

	Formatting vars_text;
	auto vars_content = vars_node->ContentChildren();
	bool first = true;

	for ( const auto& v : vars_content )
		{
		if ( ! first )
			vars_text += ", ";
		first = false;
		vars_text += best(format_expr(*v, cond_ctx)).Fmt();
		}

	Formatting iter_text;
	auto iter_content = iter_node->ContentChildren();
	if ( ! iter_content.empty() )
		iter_text += best(format_expr(*iter_content[0], cond_ctx)).Fmt();

	auto cond = vars_text + " " + Formatting(in_node) + " " + iter_text;
	return cond + " " + Formatting(rp);
	}

// Else follow-on for if-else.
LayoutItem Node::ComputeElseFollowOn(ComputeCtx& /*cctx*/,
                                      const FmtContext& ctx) const
	{
	// Find ElseIf or ElseBody child.
	NodePtr else_node;
	for ( const auto& c : Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::ElseIf || t == Tag::ElseBody )
			{
			else_node = c;
			break;
			}
		}

	// Check for standalone blank lines before the else.
	bool has_blank = false;
	for ( const auto& c : Children() )
		if ( c->GetTag() == Tag::Blank )
			has_blank = true;

	auto stmt_pad = line_prefix(ctx.Indent(), ctx.Col());
	auto comments = else_node->EmitPreComments(stmt_pad);

	if ( ! comments->Empty() && comments->Back() == '\n' )
		comments->PopBack();

	Formatting result;

	if ( has_blank || ! comments->Empty() )
		result += "\n";

	result += *comments;

	// ELSE-IF/ELSE-BODY: [0]=KEYWORD [1]=SP [2]=content
	auto else_child = else_node->Child(2);
	auto else_kw = else_node->Child(0, Tag::Keyword);

	if ( else_node->GetTag() == Tag::ElseIf )
		{
		auto inner_cs = format_expr(*else_child, ctx);
		result += "\n" + stmt_pad + Formatting(else_kw) + " " +
				best(inner_cs).Fmt();
		}
	else if ( else_child->GetTag() == Tag::Block )
		result += "\n" + stmt_pad + Formatting(else_kw) +
				else_child->FormatWhitesmithBlock(ctx);
	else
		{
		auto else_ctx = ctx.Indented();
		auto cs = format_expr(*else_child, else_ctx);
		auto epad = line_prefix(else_ctx.Indent(), else_ctx.Col());
		result += "\n" + stmt_pad + Formatting(else_kw) + "\n" +
				epad + best(cs).Fmt();
		}

	return result;
	}
