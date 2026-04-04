#include "fmt_internal.h"

// Declarations: global/local/const/redef name [: type] [= init] [attrs] ;

// Build the suffix: attrs + optional semicolon.
static std::string DeclSuffix(const Node* attrs_node, const Node* semi_node,
                              const FmtContext& ctx)
	{
	std::string suffix;

	if ( attrs_node )
		{
		auto as = FormatAttrList(*attrs_node, ctx);
		if ( ! as.empty() )
			suffix += " " + as;
		}

	if ( semi_node )
		suffix += semi_node->Text();

	return suffix;
	}

// Shared state for declaration candidate generation.
struct DeclParts {
	std::string head;	// "global foo", "local bar", etc.
	std::string type_str;	// ": type" or ""
	std::string suffix;	// " &attr1 &attr2;" or ";" or ""
	std::string assign_op;	// "=", "+=", or ""
	const Node* type_node = nullptr; // direct type child (after COLON)
	const Node* colon_node = nullptr; // COLON before type
	const Node* init_val = nullptr; // direct init value (after ASSIGN)
	const Node* attrs_node = nullptr;
	const Node* semi_node = nullptr;
};

// Flat candidate + split-after-init for declarations with initializers.
static void DeclWithInit(const DeclParts& d, Candidates& result,
                         const FmtContext& ctx)
	{
	auto before_val = d.head + d.type_str + " " + d.assign_op + " ";
	int before_w = static_cast<int>(before_val.size());
	int suffix_w = static_cast<int>(d.suffix.size());

	FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
	auto val = Best(FormatExpr(*d.init_val, val_ctx));

	auto flat = before_val + val.Text() + d.suffix;

	if ( val.Lines() > 1 )
		{
		int last_w = LastLineLen(flat);
		int lines = CountLines(flat);

		auto nl = val.Text().find('\n');
		int first_val_w = (nl != std::string::npos) ?
					static_cast<int>(nl) : val.Width();
		int ovf = OvfNoTrail(before_w + first_val_w, ctx) + val.Ovf();

		if ( last_w > ctx.MaxCol() )
			ovf += last_w - ctx.MaxCol();

		result.push_back({flat, last_w, lines, ovf, ctx.Col()});
		}
	else
		result.push_back(Candidate(flat, ctx));

	// Split after init operator.
	if ( result[0].Ovf() > 0 )
		{
		// The split moves the value from col before_w to
		// col indent_col.  Skip if the worst-line overflow
		// barely improves.
		int flat_mlo = MaxLineOverflow(flat, ctx.Col(),
		                               ctx.MaxCol());
		int savings = before_w - ctx.Indented().Col();

		if ( flat_mlo == 0 && val.Lines() > 1 )
			return;
		if ( savings > 0 && savings < flat_mlo &&
		     savings < INDENT_WIDTH )
			return;

		FmtContext cont = ctx.Indented().Reserve(suffix_w);
		auto val2 = Best(FormatExpr(*d.init_val, cont));

		auto line1 = d.head + d.type_str + " " + d.assign_op;
		auto pad = LinePrefix(cont.Indent(), cont.Col());
		auto split = line1 + "\n" + pad + val2.Text() + d.suffix;
		int last_w = LastLineLen(split);
		int lines = CountLines(split);
		int ovf = TextOverflow(split, ctx.Col(), ctx.MaxCol());

		result.push_back({split, last_w, lines, ovf, ctx.Col()});
		}
	}

// Flat candidate + type-on-continuation for declarations without initializers.
static void DeclNoInit(const DeclParts& d, Candidates& result,
                       const FmtContext& ctx)
	{
	auto flat = d.head + d.type_str + d.suffix;
	result.push_back(Candidate(flat, ctx));

	// Split after ":" when head + type overflows.
	int head_type_w = static_cast<int>((d.head + d.type_str).size());
	if ( head_type_w <= ctx.MaxCol() || d.type_str.empty() )
		return;

	FmtContext cont = ctx.Indented();
	auto tv = Best(FormatExpr(*d.type_node, cont)).Text();
	auto line1 = d.head + d.colon_node->Text();
	auto pad = LinePrefix(cont.Indent(), cont.Col());

	// Try type + suffix on one continuation line.
	auto oneline = tv + d.suffix;
	if ( cont.Col() + static_cast<int>(oneline.size()) <= ctx.MaxCol() )
		{
		auto split = line1 + "\n" + pad + oneline;
		int last_w = LastLineLen(split);
		result.push_back({split, last_w, CountLines(split),
					Ovf(last_w, ctx), ctx.Col()});
		return;
		}

	// Type alone, attrs on separate lines.
	auto semi_str = d.semi_node ? d.semi_node->Text() : "";
	auto type_suffix = d.attrs_node ? "" : d.suffix;
	auto split = line1 + "\n" + pad + tv + type_suffix;

	if ( d.attrs_node )
		{
		auto apad = LinePrefix(cont.Indent(), cont.Col() + 1);
		auto astrs = FormatAttrStrings(*d.attrs_node, ctx);
		for ( const auto& a : astrs )
			split += "\n" + apad + a;
		split += semi_str;
		}

	int last_w = LastLineLen(split);
	result.push_back({split, last_w, CountLines(split), Ovf(last_w, ctx),
				ctx.Col()});
	}

// Attrs on continuation lines, type stays on first line.
static void DeclWrappedAttrs(const DeclParts& d, Candidates& result,
                             const FmtContext& ctx)
	{
	if ( ! d.attrs_node || d.type_str.empty() )
		return;

	auto attr_strs = FormatAttrStrings(*d.attrs_node, ctx);
	if ( attr_strs.empty() )
		return;

	// First line: everything except attrs and semi.
	std::string line1 = d.head + d.type_str;

	if ( d.init_val )
		{
		auto after = line1.size() + d.assign_op.size() + 2;
		FmtContext val_ctx = ctx.After(static_cast<int>(after));
		auto vcs = FormatExpr(*d.init_val, val_ctx);
		line1 += " " + d.assign_op + " " + Best(vcs).Text();
		}

	// Attrs aligned one column past where the type starts.
	int attr_col = static_cast<int>(d.head.size()) + 3;
	auto attr_pad = LinePrefix(ctx.Indent(), attr_col);
	int max_col = ctx.MaxCol();
	int semi_w = d.semi_node ? d.semi_node->Width() : 0;

	// Check if all attrs fit on one continuation line.
	std::string all_attrs;
	for ( size_t i = 0; i < attr_strs.size(); ++i )
		{
		if ( i > 0 )
			all_attrs += " ";
		all_attrs += attr_strs[i];
		}

	std::string wrapped = line1;
	int ovf = OvfNoTrail(static_cast<int>(line1.size()), ctx);

	if ( attr_col + static_cast<int>(all_attrs.size()) + semi_w <= max_col )
		{
		wrapped += "\n" + attr_pad + all_attrs;
		int aw = attr_col + static_cast<int>(all_attrs.size()) + semi_w;
		if ( aw > max_col )
			ovf += aw - max_col;
		}
	else
		{
		for ( size_t i = 0; i < attr_strs.size(); ++i )
			{
			wrapped += "\n" + attr_pad + attr_strs[i];
			int aw = attr_col +
				static_cast<int>(attr_strs[i].size());
			if ( i + 1 == attr_strs.size() )
				aw += semi_w;
			if ( aw > max_col )
				ovf += aw - max_col;
			}
		}

	if ( d.semi_node )
		wrapped += d.semi_node->Text();

	int last_w = LastLineLen(wrapped);
	int lines = CountLines(wrapped);

	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});
	}

// Split after colon: type (and optional init) on indented continuation.
static void DeclTypeSplit(const DeclParts& d, Candidates& result,
                          const FmtContext& ctx)
	{
	if ( d.type_str.empty() )
		return;

	FmtContext cont = ctx.Indented();
	auto bare_type = Best(FormatExpr(*d.type_node, cont)).Text();

	auto line1 = d.head + d.colon_node->Text();
	auto pad = LinePrefix(cont.Indent(), cont.Col());
	auto split = line1 + "\n" + pad + bare_type;

	if ( d.init_val )
		{
		int suffix_w = static_cast<int>(d.suffix.size());
		auto after = bare_type.size() + d.assign_op.size() + 2;
		FmtContext val_ctx =
			cont.After(static_cast<int>(after)).Reserve(suffix_w);
		auto val_cs = FormatExpr(*d.init_val, val_ctx);
		split += " " + d.assign_op + " " + Best(val_cs).Text();
		}

	split += d.suffix;

	int last_w = LastLineLen(split);
	int lines = CountLines(split);
	int ovf = OvfNoTrail(static_cast<int>(line1.size()), ctx) +
				Ovf(last_w, ctx);
	result.push_back({split, last_w, lines, ovf, ctx.Col()});
	}

Candidates FormatDecl(const Node& node, const FmtContext& ctx)
	{
	auto kw_node = node.FindChild(Tag::Keyword);
	auto id_node = node.FindChild(Tag::Identifier);

	DeclParts d;
	d.head = kw_node->Text() + " " + id_node->Text();
	d.attrs_node = node.FindOptChild(Tag::AttrList);
	d.semi_node = node.FindChild(Tag::Semi);

	// Scan children sequentially: COLON precedes type, ASSIGN
	// precedes init value.  Skip the head keyword and name nodes.
	bool expect_type = false;
	bool expect_init = false;

	for ( const auto& c : node.Children() )
		{
		if ( c.get() == kw_node || c.get() == id_node )
			continue;

		Tag t = c->GetTag();

		if ( t == Tag::Colon )
			{
			d.colon_node = c.get();
			expect_type = true;
			continue;
			}

		if ( t == Tag::Assign )
			{
			d.assign_op = c->Arg();
			expect_init = true;
			continue;
			}

		if ( expect_type && ! c->IsToken() )
			{
			d.type_node = c.get();
			expect_type = false;
			continue;
			}

		if ( expect_init && ! c->IsToken() )
			{
			d.init_val = c.get();
			expect_init = false;
			continue;
			}
		}

	if ( d.type_node )
		{
		auto ts = Best(FormatExpr(*d.type_node, ctx)).Text();
		if ( ! ts.empty() )
			d.type_str = d.colon_node->Text() + " " + ts;
		}

	d.suffix = DeclSuffix(d.attrs_node, d.semi_node, ctx);

	Candidates result;

	if ( d.init_val )
		DeclWithInit(d, result, ctx);
	else
		DeclNoInit(d, result, ctx);

	if ( result[0].Ovf() > 0 )
		{
		DeclWrappedAttrs(d, result, ctx);
		DeclTypeSplit(d, result, ctx);
		}

	return result;
	}

// ------------------------------------------------------------------
// Module declaration: module SomeName;
// ------------------------------------------------------------------

Candidates FormatModuleDecl(const Node& node, const FmtContext& ctx)
	{
	auto kw_text = node.FindChild(Tag::Keyword)->Text();
	auto id_text = node.FindChild(Tag::Identifier)->Text();
	auto semi_text = node.FindChild(Tag::Semi)->Text();
	return BuildLayout({kw_text, SoftSp, id_text, semi_text}, ctx);
	}

// ------------------------------------------------------------------
// Function/event/hook declarations
// ------------------------------------------------------------------

struct ParamEntry {
	std::string text;
	const Node* comma = nullptr;	// COMMA before this param
};

static std::vector<ParamEntry> FormatParamEntries(const Node* params,
                                                   const FmtContext& ctx)
	{
	std::vector<ParamEntry> result;

	if ( ! params )
		return result;

	const Node* pending_comma = nullptr;
	for ( const auto& p : params->Children() )
		{
		Tag t = p->GetTag();

		if ( t == Tag::Comma )
			{
			pending_comma = p.get();
			continue;
			}

		if ( p->IsToken() )
			continue;

		if ( t != Tag::Param )
			continue;

		std::string text = p->Arg();

		if ( auto ptype = FindTypeChild(*p) )
			text += p->FindChild(Tag::Colon)->Text() + " " +
				Best(FormatExpr(*ptype, ctx)).Text();

		result.push_back({text, pending_comma});
		pending_comma = nullptr;
		}

	return result;
	}

Candidates FormatFuncDecl(const Node& node, const FmtContext& ctx)
	{
	auto params = node.FindChild(Tag::Params);
	auto pentries = FormatParamEntries(params, ctx);

	// Build flat param list.
	std::string flat_params;
	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		if ( pe.comma )
			flat_params += pe.comma->Text() + " ";
		flat_params += pe.text;
		}

	// Return type suffix.
	std::string ret_str;
	if ( auto returns = node.FindOptChild(Tag::Returns) )
		{
		if ( auto rt = FindTypeChild(*returns) )
			{
			auto rcol = node.FindChild(Tag::Colon);
			ret_str = rcol->Text() + " " +
				Best(FormatExpr(*rt, ctx)).Text();
			}
		}

	// Attribute suffix.
	std::string attr_str;
	if ( auto attrs = node.FindOptChild(Tag::AttrList) )
		{
		auto as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			attr_str = " " + as;
		}

	// Trailing comment on the func decl (attached to body).
	auto body = node.FindChild(Tag::Body);
	auto trail_str = body->TrailingComment();

	auto kw_node = node.FindChild(Tag::Keyword)->Text();
	auto id_node = node.FindChild(Tag::Identifier)->Text();
	auto lp = params->FindChild(Tag::LParen)->Text();
	auto prefix = kw_node + " " + id_node + lp;

	// --- Candidate 1: flat signature ---
	auto rp = params->FindChild(Tag::RParen)->Text();
	auto sig = prefix + flat_params + rp + ret_str + attr_str + trail_str;
	auto block = FormatWhitesmithBlock(body, ctx);

	Candidates result;
	result.push_back(Candidate(sig + block, ctx));

	if ( result[0].Ovf() <= 0 )
		return result;

	// --- Candidate 2: greedy-fill params + attrs on continuation ---
	int align_col = ctx.Col() + static_cast<int>(prefix.size());
	int max_col = ctx.MaxCol();
	auto pad = LinePrefix(ctx.Indent(), align_col);
	auto wrapped = prefix;

	int cur_col = align_col;

	for ( size_t i = 0; i < pentries.size(); ++i )
		{
		auto& pe = pentries[i];
		int pw = static_cast<int>(pe.text.size());

		if ( i == 0 )
			{
			wrapped += pe.text;
			cur_col += pw;
			continue;
			}

		auto sep = pe.comma->Text();
		int need = static_cast<int>(sep.size()) + 1 + pw;
		if ( cur_col + need <= max_col )
			{
			wrapped += sep + " " + pe.text;
			cur_col += need;
			}
		else
			{
			int suffix = (i == pentries.size() - 1) ? 1 : 0;
			int pcol = FitCol(align_col, pw + suffix, max_col);
			auto ppad = LinePrefix(ctx.Indent(), pcol);
			wrapped += sep + "\n" + ppad + pe.text;
			cur_col = pcol + pw;
			}
		}

	wrapped += rp + ret_str;

	// Put attrs on their own continuation line if present.
	// Use param alignment column, but shift left if that overflows
	// so the line ends at column max_col - 1.
	if ( ! attr_str.empty() )
		{
		auto bare_attr = attr_str.substr(1);
		int aw = static_cast<int>(bare_attr.size());
		int attr_col = FitCol(align_col, aw, max_col);
		auto attr_pad = LinePrefix(ctx.Indent(), attr_col);
		wrapped += "\n" + attr_pad + bare_attr;
		}

	wrapped += trail_str + block;

	int last_w = LastLineLen(wrapped);
	int lines = CountLines(wrapped);
	int ovf = TextOverflow(wrapped, ctx.Col(), max_col);

	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Type declarations: type name: enum/record/basetype ;
// ------------------------------------------------------------------

// Format a record field.  suffix includes ";" and any trailing
// comment so we can measure overflow and wrap attrs if needed.
static std::string FormatField(const Node& node, const std::string& suffix,
                               const FmtContext& ctx)
	{
	auto fcol = node.FindChild(Tag::Colon)->Text();
	auto head = node.Arg() + fcol + " ";

	std::string type_str;
	if ( auto tc = FindTypeChild(node) )
		type_str = Best(FormatExpr(*tc, ctx)).Text();

	auto attrs = node.FindOptChild(Tag::AttrList);
	std::string attr_str;
	if ( attrs )
		{
		auto as = FormatAttrList(*attrs, ctx);
		if ( ! as.empty() )
			attr_str = " " + as;
		}

	// Try flat.
	auto flat = head + type_str + attr_str + suffix;
	if ( ctx.Col() + static_cast<int>(flat.size()) <= ctx.MaxCol() )
		return flat;

	if ( attr_str.empty() )
		return flat;

	// Wrap attrs to continuation line aligned one past type start.
	int attr_col = static_cast<int>(head.size()) + 1;
	auto pad = LinePrefix(ctx.Indent(), ctx.Col() + attr_col);
	auto attr_strs = FormatAttrStrings(*attrs, ctx);

	std::string all_attrs;
	for ( size_t i = 0; i < attr_strs.size(); ++i )
		{
		if ( i > 0 )
			all_attrs += " ";
		all_attrs += attr_strs[i];
		}

	return head + type_str + "\n" + pad + all_attrs + suffix;
	}

static Candidate format_enum_decl(const Node* enum_node, const std::string& kw,
				const std::string& id, const std::string& semi,
				const std::string& colon, const FmtContext& ctx)
	{
	auto ekw = enum_node->FindChild(Tag::Keyword)->Text();
	auto lb = enum_node->FindChild(Tag::LBrace)->Text();

	auto head = Best(BuildLayout({kw, SoftSp, id, colon, SoftSp,
					ekw + " " + lb}, ctx)).Text();

	// Collect enum values and commas.
	std::vector<std::string> values;
	Nodes commas;
	bool has_trailing_comma = false;
	const Node* pending_comma = nullptr;

	for ( const auto& c : enum_node->Children() )
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
			pending_comma = c.get();

		else if ( c->GetTag() == Tag::TrailingComma )
			has_trailing_comma = true;
		}

	// One per line.
	auto pad = LinePrefix(ctx.Indent() + 1,
				(ctx.Indent() + 1) * INDENT_WIDTH);
	std::string body;
	for ( size_t i = 0; i < values.size(); ++i )
		{
		body += pad + values[i];
		auto nc = (i + 1 < commas.size()) ? commas[i + 1] : nullptr;
		if ( nc || has_trailing_comma )
			body += nc ? nc->Text() : ",";
		body += "\n";
		}

	auto close_pad = LinePrefix(ctx.Indent(), ctx.Col());
	auto rb = enum_node->FindChild(Tag::RBrace)->Text();
	auto text = head + "\n" + body + close_pad + rb + semi;
	return Candidate(text, ctx);
	}

static Candidate format_rec_decl(const Node* rec_node, const std::string& kw,
				const std::string& id, const std::string& semi,
				const std::string& colon, const FmtContext& ctx)
	{
	auto rkw = rec_node->FindChild(Tag::Keyword)->Text();
	auto lb = rec_node->FindChild(Tag::LBrace)->Text();

	auto head = Best(BuildLayout({kw, SoftSp, id, colon, SoftSp,
					rkw + " " + lb}, ctx)).Text();

	int field_indent = ctx.Indent() + 1;
	int field_col = field_indent * INDENT_WIDTH;
	FmtContext field_ctx(field_indent, field_col, ctx.MaxCol() - field_col);
	auto field_pad = LinePrefix(field_indent, field_col);

	// Collect fields, comments, blanks.
	std::string body;
	const auto& kids = rec_node->Children();
	for ( size_t i = 0; i < kids.size(); ++i )
		{
		auto& ki = kids[i];
		Tag t = ki->GetTag();

		if ( t == Tag::Blank )
			{
			body += "\n";
			continue;
			}

		if ( t == Tag::Field )
			{
			body += EmitPreComments(*ki, field_pad);

			auto fsemi = ki->FindChild(Tag::Semi)->Text();
			auto suffix = fsemi + ki->TrailingComment();
			auto field_text = FormatField(*ki, suffix, field_ctx);

			body += field_pad + field_text + "\n";
			}
		}

	auto close_pad = LinePrefix(ctx.Indent(), ctx.Col());
	auto rb = rec_node->FindChild(Tag::RBrace)->Text();
	auto text = head + "\n" + body + close_pad + rb + semi;

	return {Candidate(text, ctx)};
	}

Candidates FormatTypeDecl(const Node& node, const FmtContext& ctx)
	{
	const Node* kw_node = node.FindChild(Tag::Keyword);
	const Node* id_node = node.FindChild(Tag::Identifier);
	const Node* colon = node.FindChild(Tag::Colon);
	const Node* semi = node.FindChild(Tag::Semi);
	std::string kw = kw_node->Text();
	std::string id = id_node->Text();
	std::string semi_str = semi->Text();

	// Simple type alias: type name: basetype;
	const Node* base_type = FindTypeChild(node);
	if ( base_type )
		return BuildLayout({kw, SoftSp, id, colon->Text(),
			SoftSp, base_type, semi_str}, ctx);

	if ( auto enum_node = node.FindOptChild(Tag::TypeEnum) )
		return {format_enum_decl(enum_node, kw, id,
						semi_str, colon->Text(), ctx)};

	// Record type.
	if ( auto rec_node = node.FindOptChild(Tag::TypeRecord) )
		return {format_rec_decl(rec_node, kw, id,
						semi_str, colon->Text(), ctx)};

	// Fallback.
	return BuildLayout({kw, SoftSp, id, semi_str}, ctx);
	}
