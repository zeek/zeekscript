#include "fmt_util.h"

// Compute "of type" suffix for TYPE-PARAMETERIZED: " of type".
LayoutItem Layout::ComputeOfType(ComputeCtx& cctx, const FmtContext& ctx) const
	{
	auto kw = FindOptChild(Tag::Keyword);
	if ( ! kw )
		return Formatting();
	return " " + Formatting(kw) + " " +
		best(format_expr(*FindTypeChild(), ctx)).Fmt();
	}

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
const LayoutPtr& Layout::FindTypeChild() const
	{
	for ( const auto& c : Children() )
		if ( c->IsType() )
			return c;

	return null_node;
	}

// TYPE-FUNC: event(params), function(params): rettype

// Compute the type suffix for a PARAM node: ": type".
LayoutItem Layout::ComputeParamType(ComputeCtx& cctx, const FmtContext& ctx) const
	{
	return Formatting(Child(0, Tag::Colon)) + " " +
		best(format_expr(*FindTypeChild(), ctx)).Fmt();
	}

// Compute the return type suffix for a TYPE-FUNC-RET node: ": rettype".
LayoutItem Layout::ComputeRetType(ComputeCtx& cctx, const FmtContext& ctx) const
	{
	auto& returns = FindChild(Tag::Returns);
	return Formatting(FindChild(Tag::Colon)) + " " +
		best(format_expr(*returns->FindTypeChild(), ctx)).Fmt();
	}

static const LayoutPtr& get_non_token_child(const Layout& parent)
	{
	for ( const auto& c : parent.Children() )
		if ( ! c->IsToken() )
			return c;
	return null_node;
	}

// Check whether any attr value in an ATTR-LIST contains blanks.
// If so, all attrs use " = " spacing; otherwise "=".
static bool attr_list_needs_spaces(const Layout& node, const FmtContext& ctx)
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
static Formatting format_one_attr(const Layout& attr, bool spaced,
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

std::vector<Formatting> Layout::FormatAttrStrings(const FmtContext& ctx) const
	{
	bool spaced = attr_list_needs_spaces(*this, ctx);
	std::vector<Formatting> result;

	for ( const auto& attr : Children() )
		if ( attr->GetTag() == Tag::Attr )
			result.push_back(format_one_attr(*attr, spaced, ctx));

	return result;
	}

Formatting Layout::FormatAttrList(const FmtContext& ctx) const
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
