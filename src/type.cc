#include "expr.h"
#include "fmt_util.h"

// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t
Candidates TypeParamNode::Format(const FmtContext& ctx) const
	{
	const auto& keyword = Arg();	// "table", "set", "vector"

	// Collect bracketed type args (between LBRACKET/RBRACKET)
	// and "of" type (after KEYWORD "of").
	ArgComments bt_items;
	NodePtr of_type;
	NodePtr pending_comma;
	bool in_brackets = false;
	bool past_of = false;

	for ( const auto& c : Children() )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBracket )
			{
			in_brackets = true;
			continue;
			}

		if ( t == Tag::RBracket )
			{
			in_brackets = false;
			continue;
			}

		if ( t == Tag::Keyword )
			{
			past_of = true;
			continue;
			}

		if ( t == Tag::Comma )
			{
			pending_comma = c;
			continue;
			}

		if ( c->IsToken() )
			continue;

		if ( past_of )
			of_type = c;

		else if ( in_brackets )
			{
			bt_items.push_back({c, "", {}, pending_comma});
			pending_comma = nullptr;
			}
		}

	Formatting suffix;
	if ( of_type )
		suffix = " " + FindChild(Tag::Keyword) + " " +
			best(format_expr(*of_type, ctx)).Fmt();

	if ( bt_items.empty() )
		return {Candidate(Formatting(keyword) + suffix, ctx)};

	return flat_or_fill(keyword, FindChild(Tag::LBracket),
		FindChild(Tag::RBracket), suffix, bt_items, ctx);
	}

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
const NodePtr& Node::FindTypeChild() const
	{
	for ( const auto& c : Children() )
		if ( c->IsType() )
			return c;

	return null_node;
	}

// TYPE-FUNC: event(params), function(params): rettype

// Compute the type suffix for a PARAM node: ": type".
LayoutItem Node::ComputeParamType(ComputeCtx& cctx, const FmtContext& ctx) const
	{
	return Formatting(Child(0, Tag::Colon)) + " " +
		best(format_expr(*FindTypeChild(), ctx)).Fmt();
	}

// Compute the return type suffix for a TYPE-FUNC-RET node: ": rettype".
LayoutItem Node::ComputeRetType(ComputeCtx& cctx, const FmtContext& ctx) const
	{
	auto& returns = FindChild(Tag::Returns);
	return Formatting(FindChild(Tag::Colon)) + " " +
		best(format_expr(*returns->FindTypeChild(), ctx)).Fmt();
	}

static const NodePtr& get_non_token_child(const Node& parent)
	{
	for ( const auto& c : parent.Children() )
		if ( ! c->IsToken() )
			return c;
	return null_node;
	}

// Check whether any attr value in an ATTR-LIST contains blanks.
// If so, all attrs use " = " spacing; otherwise "=".
static bool attr_list_needs_spaces(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		// Find the value expression (first non-token child).
		auto val = get_non_token_child(*attr);
		if ( ! val )
			continue;

		if ( best(format_expr(*val, ctx)).Fmt().Contains(' ') )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static Formatting format_one_attr(const Node& attr, bool spaced,
                                  const FmtContext& ctx)
	{
	Formatting fmt(attr.Arg());

	if ( auto eq = attr.FindOptChild(Tag::Assign) )
		{
		auto sep = spaced ? " " : "";
		fmt += sep + Formatting(eq) + sep;

		if ( auto val = get_non_token_child(attr) )
			fmt += best(format_expr(*val, ctx)).Fmt();
		}

	return fmt;
	}

std::vector<Formatting> Node::FormatAttrStrings(const FmtContext& ctx) const
	{
	bool spaced = attr_list_needs_spaces(*this, ctx);
	std::vector<Formatting> result;

	for ( const auto& attr : Children() )
		if ( attr->GetTag() == Tag::Attr )
			result.push_back(format_one_attr(*attr, spaced, ctx));

	return result;
	}

Formatting Node::FormatAttrList(const FmtContext& ctx) const
	{
	Formatting fmt;
	for ( const auto& a : FormatAttrStrings(ctx) )
		{
		if ( ! fmt.Empty() )
			fmt += " ";
		fmt += a;
		}
	return fmt;
	}
