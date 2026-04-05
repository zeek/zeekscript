#include "expr_nodes.h"
#include "fmt_internal.h"

// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t
Candidates TypeParamNode::Format(const FmtContext& ctx) const
	{
	const auto& keyword = Arg();	// "table", "set", "vector"

	// Collect bracketed type args (between LBRACKET/RBRACKET)
	// and "of" type (after KEYWORD "of").
	ArgComments bt_items;
	const Node* of_type = nullptr;
	const Node* pending_comma = nullptr;
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
			pending_comma = c.get();
			continue;
			}

		if ( c->IsToken() )
			continue;

		if ( past_of )
			of_type = c.get();

		else if ( in_brackets )
			{
			bt_items.push_back({c.get(), "", {}, pending_comma});
			pending_comma = nullptr;
			}
		}

	std::string suffix;
	if ( of_type )
		suffix = " " + FindChild(Tag::Keyword)->Text() + " " +
			Best(FormatExpr(*of_type, ctx)).Text();

	if ( bt_items.empty() )
		return {Candidate(keyword + suffix, ctx)};

	auto lb = FindChild(Tag::LBracket)->Text();
	auto rb = FindChild(Tag::RBracket)->Text();
	return FlatOrFill(keyword, lb, rb, suffix, bt_items, ctx);
	}

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
const Node* FindTypeChild(const Node& node)
	{
	for ( const auto& c : node.Children() )
		if ( c->IsType() )
			return c.get();
	return nullptr;
	}

// TYPE-FUNC: event(params), function(params): rettype

// Format a single parameter: name[: type]
// PARAM children: [0]=COLON [1]=type_expr
Candidates ParamNode::Format(const FmtContext& ctx) const
	{
	auto text = Arg();
	if ( auto ptype = FindTypeChild(*this) )
		text += Child(0, Tag::Colon)->Text() + " " +
			Best(FormatExpr(*ptype, ctx)).Text();
	return {Candidate(text, ctx)};
	}

// TYPE-FUNC: [0]=PARAMS [optional COLON, RETURNS]
Candidates TypeFuncNode::Format(const FmtContext& ctx) const
	{
	const auto& keyword = Arg();

	// Return type suffix.
	std::string ret_str;
	if ( auto returns = FindOptChild(Tag::Returns) )
		{
		auto colon = FindChild(Tag::Colon)->Text();
		if ( auto rt = FindTypeChild(*returns) )
			ret_str = colon + " " +
				Best(FormatExpr(*rt, ctx)).Text();
		}

	auto params = Child(0, Tag::Params);
	auto items = CollectArgs(params->Children());

	auto lp = params->Child(0, Tag::LParen)->Text();
	auto rp = params->Children().back()->Text();

	if ( items.empty() )
		return {Candidate(keyword + lp + rp + ret_str, ctx)};

	return FlatOrFill(keyword, lp, rp, ret_str, items, ctx);
	}

static const Node* get_non_token_child(const Node* parent)
	{
	for ( const auto& c : parent->Children() )
		if ( ! c->IsToken() )
			return c.get();

	return nullptr;
	}

// Check whether any attr value in an ATTR-LIST contains blanks.
// If so, all attrs use " = " spacing; otherwise "=".
static bool AttrListNeedsSpaces(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		// Find the value expression (first non-token child).
		auto val = get_non_token_child(attr.get());
		if ( ! val )
			continue;

		if ( Best(FormatExpr(*val, ctx)).Text().find(' ') !=
		     std::string::npos )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static std::string FormatOneAttr(const Node& attr, bool spaced,
                                 const FmtContext& ctx)
	{
	auto text = attr.Arg();

	if ( auto eq = attr.FindOptChild(Tag::Assign) )
		{
		auto sep = spaced ? " " : "";
		text += sep + eq->Text() + sep;

		if ( auto val = get_non_token_child(&attr) )
			text += Best(FormatExpr(*val, ctx)).Text();
		}

	return text;
	}

std::vector<std::string> FormatAttrStrings(const Node& node,
                                           const FmtContext& ctx)
	{
	bool spaced = AttrListNeedsSpaces(node, ctx);
	std::vector<std::string> result;

	for ( const auto& attr : node.Children() )
		if ( attr->GetTag() == Tag::Attr )
			result.push_back(FormatOneAttr(*attr, spaced, ctx));

	return result;
	}

std::string FormatAttrList(const Node& node, const FmtContext& ctx)
	{
	std::string text;
	for ( const auto& s : FormatAttrStrings(node, ctx) )
		{
		if ( ! text.empty() )
			text += " ";
		text += s;
		}
	return text;
	}
