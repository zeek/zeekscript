#include "fmt_internal.h"

// ------------------------------------------------------------------
// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t
// ------------------------------------------------------------------

Candidates FormatTypeParam(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "table", "set", "vector"

	const Node* lb = node.FindOptChild(Tag::LBracket);
	const Node* rb = node.FindOptChild(Tag::RBracket);
	const Node* of_kw = node.FindOptChild(Tag::Keyword);

	// Collect bracketed type args (between LBRACKET/RBRACKET)
	// and "of" type (after KEYWORD "of").
	std::vector<ArgComment> bt_items;
	const Node* of_type = nullptr;
	const Node* pending_comma = nullptr;
	bool in_brackets = false;
	bool past_of = false;

	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBracket )
			{ in_brackets = true; continue; }
		if ( t == Tag::RBracket )
			{ in_brackets = false; continue; }
		if ( t == Tag::Keyword )
			{ past_of = true; continue; }
		if ( t == Tag::Comma )
			{ pending_comma = c.get(); continue; }
		if ( is_token(t) || is_comment(t) )
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
		suffix = " " + of_kw->Text() + " " +
			Best(FormatExpr(*of_type, ctx)).Text();

	if ( bt_items.empty() )
		return {Candidate(keyword + suffix, ctx)};

	return FlatOrFill(keyword, lb->Text(), rb->Text(), suffix,
		bt_items, ctx);
	}

// ------------------------------------------------------------------
// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
// ------------------------------------------------------------------

const Node* FindTypeChild(const Node& node)
	{
	for ( const auto& c : node.Children() )
		if ( is_type_tag(c->GetTag()) )
			return c.get();
	return nullptr;
	}

// ------------------------------------------------------------------
// TYPE-FUNC: event(params), function(params): rettype
// ------------------------------------------------------------------

Candidates FormatTypeFunc(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "event", "function", "hook"

	const Node* params = node.FindChild(Tag::Params);
	const Node* returns = node.FindOptChild(Tag::Returns);

	const Node* lp = params->FindChild(Tag::LParen);
	const Node* rp = params->FindChild(Tag::RParen);
	std::string text = keyword + lp->Text();

	for ( const auto& p : params->Children() )
		{
		Tag t = p->GetTag();

		if ( t == Tag::Comma )
			{
			text += p->Text() + " ";
			continue;
			}

		if ( is_comment(t) || is_token(t) )
			continue;
		if ( t != Tag::Param )
			continue;

		text += p->Arg();
		const Node* ptype = FindTypeChild(*p);
		if ( ptype )
			{
			const Node* pcol = p->FindChild(Tag::Colon);
			text += pcol->Text() + " " +
				Best(FormatExpr(*ptype, ctx)).Text();
			}
		}

	text += rp->Text();

	if ( returns )
		{
		const Node* colon = node.FindChild(Tag::Colon);
		const Node* rt = FindTypeChild(*returns);
		if ( rt )
			text += colon->Text() + " " +
				Best(FormatExpr(*rt, ctx)).Text();
		}

	return {Candidate(text, ctx)};
	}

// ------------------------------------------------------------------
// Attributes: &redef, &default=expr
// ------------------------------------------------------------------

// Check whether any attr value in an ATTR-LIST contains blanks.
// If so, all attrs use " = " spacing; otherwise "=".
static bool AttrListNeedsSpaces(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		// Find the value expression (first non-token child).
		const Node* val = nullptr;
		for ( const auto& c : attr->Children() )
			if ( ! is_token(c->GetTag()) )
				{ val = c.get(); break; }
		if ( ! val )
			continue;

		auto val_cs = FormatExpr(*val, ctx);
		if ( Best(val_cs).Text().find(' ') != std::string::npos )
			return true;
		}

	return false;
	}

// Format a single attr: "&name" or "&name=value" / "&name = value".
static std::string FormatOneAttr(const Node& attr, bool spaced,
                                 const FmtContext& ctx)
	{
	std::string text = attr.Arg();

	const Node* eq = attr.FindOptChild(Tag::Assign);
	if ( eq )
		{
		const Node* val = nullptr;
		for ( const auto& c : attr.Children() )
			if ( ! is_token(c->GetTag()) )
				{ val = c.get(); break; }

		std::string sep = spaced ? " " : "";
		text += sep + eq->Text() + sep;
		if ( val )
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
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		result.push_back(FormatOneAttr(*attr, spaced, ctx));
		}

	return result;
	}

std::string FormatAttrList(const Node& node, const FmtContext& ctx)
	{
	auto strs = FormatAttrStrings(node, ctx);
	std::string text;
	for ( const auto& s : strs )
		{
		if ( ! text.empty() )
			text += " ";
		text += s;
		}
	return text;
	}
