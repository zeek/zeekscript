#include "expr.h"
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
			best(format_expr(*of_type, ctx)).Text();

	if ( bt_items.empty() )
		return {Candidate(keyword + suffix, ctx)};

	auto lb = FindChild(Tag::LBracket)->Text();
	auto rb = FindChild(Tag::RBracket)->Text();
	return flat_or_fill(keyword, lb, rb, suffix, bt_items, ctx);
	}

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
const Node* Node::FindTypeChild() const
	{
	for ( const auto& c : Children() )
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
	if ( auto ptype = FindTypeChild() )
		text += Child(0, Tag::Colon)->Text() + " " +
			best(format_expr(*ptype, ctx)).Text();
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
		if ( auto rt = returns->FindTypeChild() )
			ret_str = colon + " " +
				best(format_expr(*rt, ctx)).Text();
		}

	auto params = Child(0, Tag::Params);
	auto items = collect_args(params->Children());

	auto lp = params->Child(0, Tag::LParen)->Text();
	auto rp = params->Children().back()->Text();

	if ( items.empty() )
		return {Candidate(keyword + lp + rp + ret_str, ctx)};

	return flat_or_fill(keyword, lp, rp, ret_str, items, ctx);
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
static bool attr_list_needs_spaces(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		// Find the value expression (first non-token child).
		auto val = get_non_token_child(attr.get());
		if ( ! val )
			continue;

		if ( best(format_expr(*val, ctx)).Text().find(' ') !=
		     std::string::npos )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static std::string format_one_attr(const Node& attr, bool spaced,
                                   const FmtContext& ctx)
	{
	auto text = attr.Arg();

	if ( auto eq = attr.FindOptChild(Tag::Assign) )
		{
		auto sep = spaced ? " " : "";
		text += sep + eq->Text() + sep;

		if ( auto val = get_non_token_child(&attr) )
			text += best(format_expr(*val, ctx)).Text();
		}

	return text;
	}

std::vector<std::string> Node::FormatAttrStrings(const FmtContext& ctx) const
	{
	bool spaced = attr_list_needs_spaces(*this, ctx);
	std::vector<std::string> result;

	for ( const auto& attr : Children() )
		if ( attr->GetTag() == Tag::Attr )
			result.push_back(format_one_attr(*attr, spaced, ctx));

	return result;
	}

std::string Node::FormatAttrList(const FmtContext& ctx) const
	{
	std::string text;
	for ( const auto& s : FormatAttrStrings(ctx) )
		{
		if ( ! text.empty() )
			text += " ";
		text += s;
		}
	return text;
	}
