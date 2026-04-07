#include "fmt_util.h"
#include "layout.h"
#include "node.h"

// Else follow-on for if-else.
LayoutItem Layout::ComputeElseFollowOn(ComputeCtx& /*cctx*/,
                                      const FmtContext& ctx) const
	{
	// Find ElseIf or ElseBody child.
	LayoutPtr else_node;
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
