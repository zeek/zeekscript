#include "fmt_util.h"
#include "stmt.h"

// Declarations: global/local/const/redef name [: type] [= init] [attrs] ;

// Build the suffix: attrs + optional semicolon.
static Formatting decl_suffix(const NodePtr& attrs_node,
                              const NodePtr& semi_node,
                              const FmtContext& ctx)
	{
	Formatting suffix;

	if ( attrs_node )
		{
		auto as = attrs_node->FormatAttrList(ctx);
		if ( ! as.Empty() )
			suffix += " " + as;
		}

	if ( semi_node )
		suffix += semi_node;

	return suffix;
	}

// Shared state for declaration candidate generation.
struct DeclParts {
	Formatting head;	// "global foo", "local bar", etc.
	Formatting type_str;	// ": type" or ""
	Formatting suffix;	// " &attr1 &attr2;" or ";" or ""
	Formatting assign_op;	// "=", "+=", or ""
	NodePtr type_node;	// direct type child (after COLON)
	NodePtr colon_node;	// COLON before type
	NodePtr init_val;		// direct init value (after ASSIGN)
	NodePtr attrs_node;	// ATTR-LIST child
	NodePtr semi_node;	// SEMI child
};

// Flat candidate + split-after-init for declarations with initializers.
static void decl_with_init(const DeclParts& d, Candidates& result,
                         const FmtContext& ctx)
	{
	auto before_val = d.head + d.type_str + " " + d.assign_op + " ";
	int before_w = before_val.Size();
	int suffix_w = d.suffix.Size();

	FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
	auto val = best(format_expr(*d.init_val, val_ctx));

	auto flat = before_val + val.Fmt() + d.suffix;

	if ( val.Lines() > 1 )
		{
		int last_w = flat.LastLineLen();
		int lines = flat.CountLines();
		int ovf = flat.TextOverflow(ctx.Col(), ctx.MaxCol());
		result.push_back({flat, last_w, lines, ovf, ctx.Col()});
		}
	else
		result.push_back(Candidate(flat, ctx));

	// Split after init operator when the flat candidate overflows.
	// Use per-line overflow of the assembled text, which catches
	// cases where the Candidate overflow is 0 but continuation
	// lines extend past max_col.
	int flat_mlo = flat.MaxLineOverflow(ctx.Col(), ctx.MaxCol());

	if ( flat_mlo > 0 )
		{
		// Skip when the column savings from splitting are
		// too small to justify the extra line break.
		int savings = before_w - ctx.Indented().Col();
		if ( savings > 0 && savings < INDENT_WIDTH )
			return;

		FmtContext cont = ctx.Indented().Reserve(suffix_w);
		auto val2 = best(format_expr(*d.init_val, cont));

		auto line1 = d.head + d.type_str + " " + d.assign_op;
		auto pad = line_prefix(cont.Indent(), cont.Col());
		auto split = line1 + "\n" + pad + val2.Fmt() + d.suffix;
		int last_w = split.LastLineLen();
		int lines = split.CountLines();
		int ovf = split.TextOverflow(ctx.Col(), ctx.MaxCol());

		result.push_back({split, last_w, lines, ovf, ctx.Col()});
		}
	}

// Flat candidate + type-on-continuation for declarations without initializers.
static void decl_no_init(const DeclParts& d, Candidates& result,
                       const FmtContext& ctx)
	{
	auto flat = d.head + d.type_str + d.suffix;
	Candidate flat_c(flat, ctx);

	if ( flat_c.Fits() )
		{
		result.push_back(flat_c);
		return;
		}

	if ( flat_c.Lines() == 1 )
		result.push_back(flat_c);

	if ( d.type_str.Empty() )
		return;

	FmtContext cont = ctx.Indented();
	auto tv = best(format_expr(*d.type_node, cont)).Fmt();
	auto line1 = d.head + d.colon_node;
	auto pad = line_prefix(cont.Indent(), cont.Col());

	// Try type + suffix on one continuation line.
	auto oneline = tv + d.suffix;
	int oneline_w = oneline.CountLines() > 1 ?
			oneline.LastLineLen() : cont.Col() + oneline.Size();

	if ( oneline_w <= ctx.MaxCol() )
		{
		auto split = line1 + "\n" + pad + oneline;
		int last_w = split.LastLineLen();
		result.push_back({split, last_w, split.CountLines(),
					ovf(last_w, ctx), ctx.Col()});
		return;
		}

	// Type alone, attrs on separate lines.
	Formatting type_suffix = d.attrs_node ? Formatting() : d.suffix;
	auto split = line1 + "\n" + pad + tv + type_suffix;

	if ( d.attrs_node )
		{
		auto apad = line_prefix(cont.Indent(), cont.Col() + 1);
		auto astrs = d.attrs_node->FormatAttrStrings(ctx);
		for ( const auto& a : astrs )
			split += "\n" + apad + a;
		split += d.semi_node;
		}

	int last_w = split.LastLineLen();
	result.push_back({split, last_w, split.CountLines(), ovf(last_w, ctx),
				ctx.Col()});
	}

// Attrs on continuation lines, type stays on first line.
static void decl_wrapped_attrs(const DeclParts& d, Candidates& result,
                             const FmtContext& ctx)
	{
	if ( ! d.attrs_node || d.type_str.Empty() )
		return;

	auto attr_strs = d.attrs_node->FormatAttrStrings(ctx);
	if ( attr_strs.empty() )
		return;

	// First line: everything except attrs and semi.
	Formatting line1 = d.head + d.type_str;

	if ( d.init_val )
		{
		int after = line1.Size() + d.assign_op.Size() + 2;
		FmtContext val_ctx = ctx.After(after);
		auto vcs = format_expr(*d.init_val, val_ctx);
		line1 += " " + d.assign_op + " " + best(vcs).Fmt();
		}

	// Attrs aligned one column past where the type starts.
	int attr_col = d.head.Size() + 3;
	auto attr_pad = line_prefix(ctx.Indent(), attr_col);
	int max_col = ctx.MaxCol();
	int semi_w = d.semi_node->Width();

	// Check if all attrs fit on one continuation line.
	Formatting all_attrs;
	for ( size_t i = 0; i < attr_strs.size(); ++i )
		{
		if ( i > 0 )
			all_attrs += " ";
		all_attrs += attr_strs[i];
		}

	Formatting wrapped = line1;
	int ovf = ovf_no_trail(line1.Size(), ctx);

	if ( attr_col + all_attrs.Size() + semi_w <= max_col )
		{
		wrapped += "\n" + attr_pad + all_attrs;
		int aw = attr_col + all_attrs.Size() + semi_w;
		if ( aw > max_col )
			ovf += aw - max_col;
		}
	else
		{
		for ( size_t i = 0; i < attr_strs.size(); ++i )
			{
			wrapped += "\n" + attr_pad + attr_strs[i];
			int aw = attr_col + attr_strs[i].Size();
			if ( i + 1 == attr_strs.size() )
				aw += semi_w;
			if ( aw > max_col )
				ovf += aw - max_col;
			}
		}

	wrapped += d.semi_node;

	int last_w = wrapped.LastLineLen();
	int lines = wrapped.CountLines();

	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});
	}

// Split after colon: type (and optional init) on indented continuation.
static void decl_type_split(const DeclParts& d, Candidates& result,
                          const FmtContext& ctx)
	{
	if ( d.type_str.Empty() )
		return;

	FmtContext cont = ctx.Indented();
	auto bare_type = best(format_expr(*d.type_node, cont)).Fmt();

	auto line1 = d.head + d.colon_node;
	auto pad = line_prefix(cont.Indent(), cont.Col());
	auto split = line1 + "\n" + pad + bare_type;

	if ( d.init_val )
		{
		int suffix_w = d.suffix.Size();
		int after = bare_type.Size() + d.assign_op.Size() + 2;
		FmtContext val_ctx = cont.After(after).Reserve(suffix_w);
		auto val_cs = format_expr(*d.init_val, val_ctx);
		split += " " + d.assign_op + " " + best(val_cs).Fmt();
		}

	split += d.suffix;

	int last_w = split.LastLineLen();
	int lines = split.CountLines();
	int overflow = ovf_no_trail(line1.Size(), ctx) + ovf(last_w, ctx);
	result.push_back({split, last_w, lines, overflow, ctx.Col()});
	}

// GLOBAL-DECL/LOCAL-DECL: [0]=KEYWORD [1]=SP [2]=IDENTIFIER
//   [optional DECL-TYPE, DECL-INIT, ATTR-LIST] SEMI
Candidates DeclNode::Format(const FmtContext& ctx) const
	{
	auto kw_node = Child(0, Tag::Keyword);
	auto id_node = Child(2, Tag::Identifier);

	DeclParts d;
	d.head = Formatting(kw_node) + " " + id_node;
	d.attrs_node = FindOptChild(Tag::AttrList);
	d.semi_node = FindChild(Tag::Semi);

	if ( auto dt = TypeWrapper() )
		{
		d.colon_node = dt->FindChild(Tag::Colon);
		if ( auto tc = dt->FindTypeChild() )
			d.type_node = tc;
		}

	if ( auto di = InitWrapper() )
		{
		auto assign = di->FindChild(Tag::Assign);
		d.assign_op = assign->Arg();
		auto cc = di->ContentChildren();
		if ( ! cc.empty() )
			d.init_val = cc[0];
		}

	if ( d.type_node )
		{
		auto ts = best(format_expr(*d.type_node, ctx));
		if ( ! ts.Fmt().Empty() )
			d.type_str = Formatting(d.colon_node) + " " + ts.Fmt();
		}

	d.suffix = decl_suffix(d.attrs_node, d.semi_node, ctx);

	Candidates result;

	if ( d.init_val )
		decl_with_init(d, result, ctx);
	else
		decl_no_init(d, result, ctx);

	if ( result[0].Ovf() > 0 )
		{
		decl_wrapped_attrs(d, result, ctx);
		decl_type_split(d, result, ctx);
		}

	return result;
	}

// ------------------------------------------------------------------
// Function/event/hook declarations
// ------------------------------------------------------------------

struct ParamEntry {
	Formatting fmt;
	NodePtr comma;		// COMMA before this param
};

static std::vector<ParamEntry> format_param_entries(const Node& params,
                                                   const FmtContext& ctx)
	{
	std::vector<ParamEntry> result;
	NodePtr pending_comma;
	for ( const auto& p : params.Children() )
		{
		Tag t = p->GetTag();

		if ( t == Tag::Comma )
			{
			pending_comma = p;
			continue;
			}

		if ( p->IsToken() )
			continue;

		if ( t != Tag::Param )
			continue;

		Formatting fmt(p->Arg());

		if ( auto ptype = p->FindTypeChild() )
			fmt += Formatting(p->Child(0, Tag::Colon)) + " " +
				best(format_expr(*ptype, ctx)).Fmt();

		result.push_back({fmt, pending_comma});
		pending_comma = nullptr;
		}

	return result;
	}

// FUNC-DECL: [0]=KEYWORD [1]=SP [2]=IDENTIFIER [3]=PARAMS
//   [optional COLON, RETURNS, ATTR-LIST] [last]=BODY
Candidates FuncDeclNode::Format(const FmtContext& ctx) const
	{
	auto params = Child(3, Tag::Params);
	auto pentries = format_param_entries(*params, ctx);

	// Build flat param list.
	Formatting flat_params;
	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		if ( pe.comma )
			flat_params += Formatting(pe.comma) + " ";
		flat_params += pe.fmt;
		}

	// Return type suffix.
	Formatting ret_str;
	if ( auto returns = FindOptChild(Tag::Returns) )
		{
		if ( auto rt = returns->FindTypeChild() )
			{
			ret_str = Formatting(FindChild(Tag::Colon)) + " " +
				best(format_expr(*rt, ctx)).Fmt();
			}
		}

	// Attribute suffix.
	std::string attr_str;
	if ( auto attrs = FindOptChild(Tag::AttrList) )
		{
		auto as = attrs->FormatAttrList(ctx);
		if ( ! as.Empty() )
			attr_str = " " + as.Str();
		}

	// Trailing comment on the func decl (attached to body).
	const auto& body = Children().back();
	auto trail_str = body->TrailingComment();

	auto prefix = Formatting(Child(0, Tag::Keyword)) + " " +
			Child(2, Tag::Identifier) +
			params->Child(0, Tag::LParen);

	// --- Candidate 1: flat signature ---
	const auto& rp = params->Children().back();
	auto sig = prefix + flat_params + rp + ret_str + attr_str + trail_str;
	auto block = body->FormatWhitesmithBlock(ctx);

	Candidates result;
	result.push_back(Candidate(sig + block, ctx));

	if ( result[0].Ovf() <= 0 )
		return result;

	// --- Candidate 2: greedy-fill params + attrs on continuation ---
	int align_col = ctx.Col() + prefix.Size();
	int max_col = ctx.MaxCol();
	auto pad = line_prefix(ctx.Indent(), align_col);
	auto wrapped = prefix;

	int cur_col = align_col;

	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		int pw = pe.fmt.Size();

		if ( i == 0 )
			{
			wrapped += pe.fmt;
			cur_col += pw;
			continue;
			}

		int sep_w = pe.comma->Width();
		int need = sep_w + 1 + pw;
		if ( cur_col + need <= max_col )
			{
			wrapped += Formatting(pe.comma) + " " + pe.fmt;
			cur_col += need;
			}
		else
			{
			int suffix = (i == pentries.size() - 1) ? 1 : 0;
			int pcol = fit_col(align_col, pw + suffix, max_col);
			auto ppad = line_prefix(ctx.Indent(), pcol);
			wrapped += Formatting(pe.comma) + "\n" + ppad + pe.fmt;
			cur_col = pcol + pw;
			}
		}

	wrapped += Formatting(rp) + ret_str;

	// Put attrs on their own continuation line if present.
	// Use param alignment column, but shift left if that overflows
	// so the line ends at column max_col - 1.
	if ( ! attr_str.empty() )
		{
		auto bare_attr = attr_str.substr(1);
		int aw = static_cast<int>(bare_attr.size());
		int attr_col = fit_col(align_col, aw, max_col);
		auto attr_pad = line_prefix(ctx.Indent(), attr_col);
		wrapped += "\n" + attr_pad + bare_attr;
		}

	wrapped += trail_str + block;

	int last_w = wrapped.LastLineLen();
	int lines = wrapped.CountLines();
	int ovf = wrapped.TextOverflow(ctx.Col(), max_col);

	result.push_back({std::move(wrapped), last_w, lines, ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Type declarations: type name: enum/record/basetype ;
// ------------------------------------------------------------------

// Format a record field.  suffix includes ";" and any trailing
// comment so we can measure overflow and wrap attrs if needed.
// FIELD: [0]=COLON [1]=type_expr [optional ATTR-LIST] [last]=SEMI
static Formatting format_field(const Node& node, const Formatting& suffix,
                              const FmtContext& ctx)
	{
	Formatting head = node.Arg() + Formatting(node.Child(0, Tag::Colon)) +
				" ";

	Formatting type_str;
	if ( auto tc = node.FindTypeChild() )
		type_str = best(format_expr(*tc, ctx)).Fmt();

	auto attrs = node.FindOptChild(Tag::AttrList);
	Formatting attr_str;
	if ( attrs )
		{
		auto as = attrs->FormatAttrList(ctx);
		if ( ! as.Empty() )
			attr_str = " " + as;
		}

	// Try flat.
	auto flat = head + type_str + attr_str + suffix;
	if ( ctx.Col() + flat.Size() <= ctx.MaxCol() )
		return flat;

	if ( attr_str.Empty() )
		return flat;

	// Wrap attrs to continuation line aligned one past type start.
	int attr_col = head.Size() + 1;
	auto pad = line_prefix(ctx.Indent(), ctx.Col() + attr_col);
	auto attr_strs = attrs->FormatAttrStrings(ctx);

	Formatting all_attrs;
	for ( size_t i = 0; i < attr_strs.size(); ++i )
		{
		if ( i > 0 )
			all_attrs += " ";
		all_attrs += attr_strs[i];
		}

	return head + type_str + "\n" + pad + all_attrs + suffix;
	}

// ------------------------------------------------------------------
// Braced type declarations (enum, record): shared head/close framing.
// Children: [0]=KEYWORD [1]=SP [2]=IDENTIFIER [3]=COLON [4]=SP
//   [5]=TYPE-ENUM or TYPE-RECORD [6]=SEMI
// Inner: [0]=KEYWORD [1]=SP [2]=LBRACE ... [last]=RBRACE
// ------------------------------------------------------------------

Candidates TypeDeclBracedNode::Format(const FmtContext& ctx) const
	{
	auto inner = Child(5);

	auto head = best(BuildLayout(
		{0U, soft_sp, 2, 3,
		 soft_sp, {5, 0U},
		 soft_sp, {5, 2}}, ctx)).Fmt();

	auto body = FormatBody(inner, ctx);

	auto close_pad = line_prefix(ctx.Indent(), ctx.Col());
	auto fmt = head + "\n" + body + close_pad +
			inner->Children().back() + Child(6, Tag::Semi);
	return {Candidate(std::move(fmt), ctx)};
	}

// Enum body: collect values with commas, one per line.
Formatting TypeDeclEnumNode::FormatBody(const NodePtr& inner,
                                        const FmtContext& ctx) const
	{
	std::vector<std::string> values;
	NodeVec commas;
	bool has_trailing_comma = false;
	NodePtr pending_comma;

	for ( const auto& c : inner->Children() )
		{
		if ( c->GetTag() == Tag::EnumValue )
			{
			auto v = c->Arg();
			if ( ! c->Arg(1).empty() )
				v += " " + c->Arg(1);

			values.push_back(v);
			commas.push_back(pending_comma);
			pending_comma = nullptr;
			}

		else if ( c->GetTag() == Tag::Comma )
			pending_comma = c;

		else if ( c->GetTag() == Tag::TrailingComma )
			has_trailing_comma = true;
		}

	auto pad = line_prefix(ctx.Indent() + 1,
				(ctx.Indent() + 1) * INDENT_WIDTH);
	Formatting body;
	for ( size_t i = 0; i < values.size(); ++i )
		{
		body += pad + values[i];
		auto nc = (i + 1 < commas.size()) ? commas[i + 1] : nullptr;
		if ( nc || has_trailing_comma )
			{
			if ( nc )
				body += nc;
			else
				body += ",";
			}
		body += "\n";
		}

	return body;
	}

// Record body: fields, comments, blanks.
Formatting TypeDeclRecordNode::FormatBody(const NodePtr& inner,
                                          const FmtContext& ctx) const
	{
	int field_indent = ctx.Indent() + 1;
	int field_col = field_indent * INDENT_WIDTH;
	FmtContext field_ctx(field_indent, field_col, ctx.MaxCol() - field_col);
	auto field_pad = line_prefix(field_indent, field_col);

	Formatting body;
	for ( const auto& ki : inner->Children() )
		{
		Tag t = ki->GetTag();

		if ( t == Tag::Blank )
			{
			body += "\n";
			continue;
			}

		if ( t == Tag::Field )
			{
			body += ki->EmitPreComments(field_pad);

			auto suffix = Formatting(
				ki->Children().back()) +
				ki->TrailingComment();
			auto field_text = format_field(*ki, suffix, field_ctx);

			body += field_pad + field_text + "\n";
			}
		}

	return body;
	}
