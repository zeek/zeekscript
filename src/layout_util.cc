// Layout helpers: compute/format member functions and their static
// helpers - declarations, types, lambdas, functions, switch, preproc,
// body/block formatting, and else follow-on.

#include "fmt_util.h"

// ---- Pre-comment emission -----------------------------------------------

// Pre-comment / pre-marker emission
FmtPtr Layout::EmitPreComments(const std::string& pad) const
	{
	auto result = std::make_shared<Formatting>();

	for ( const auto& pc : PreComments() )
		{
		// Leading '\n' = blank line before this comment.
		size_t start = 0;
		while ( start < pc.size() && pc[start] == '\n' )
			{
			*result += "\n";
			++start;
			}

		// The comment text itself.
		size_t end = pc.size();
		while ( end > start && pc[end - 1] == '\n' )
			--end;

		*result += pad + pc.substr(start, end - start) + "\n";

		// Trailing '\n' = blank line after this comment.
		for ( size_t j = end; j < pc.size(); ++j )
			*result += "\n";
		}

	for ( const auto& pm : PreMarkers() )
		if ( pm->GetTag() == Tag::Blank )
			*result += "\n";

	return result;
	}

// ---- Type helpers --------------------------------------------------------

// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
const LayoutPtr& Layout::FindTypeChild() const
	{
	for ( const auto& c : Children() )
		if ( c->IsType() )
			return c;
	return null_node;
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

// Join a vector of formatted attr strings with spaces.
static Formatting join_attrs(const std::vector<Formatting>& strs)
	{
	Formatting result;
	for ( const auto& a : strs )
		{
		if ( ! result.Empty() )
			result += " ";
		result += a;
		}
	return result;
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
	return join_attrs(FormatAttrStrings(ctx));
	}

// Compute "of type" suffix for TYPE-PARAMETERIZED: " of type".
// Empty when there's no "of" clause (e.g. "set[count]").
LIPtr Layout::ComputeOfType(const FmtContext& ctx) const
	{
	auto kw = FindOptChild(Tag::Keyword);
	if ( ! kw )
		return lit(Formatting());

	return lit(" " + Formatting(kw) + " " +
		best(format_expr(*FindTypeChild(), ctx)).Fmt());
	}

// Compute the type suffix for a PARAM node: ": type".
LIPtr Layout::ComputeParamType(const FmtContext& ctx) const
	{
	Formatting colon_sp = Formatting(Child(0, Tag::Colon)) + " ";
	int name_w = Formatting(Arg(0)).Size() + colon_sp.Size();

	// Compute attr suffix so type formatting can reserve trail for it.
	auto attrs = FindOptChild(Tag::AttrList);
	Formatting attr_str;
	if ( attrs )
		attr_str = " " + attrs->FormatAttrList(ctx);

	auto type_ctx = ctx.After(name_w);
	if ( ! attr_str.Empty() )
		type_ctx = FmtContext(type_ctx.Indent(), type_ctx.Col(),
			type_ctx.Width(), type_ctx.Trail() + attr_str.Size());

	auto type_str = best(format_expr(*FindTypeChild(), type_ctx)).Fmt();
	return lit(colon_sp + type_str + attr_str);
	}

// Compute the return type suffix for a TYPE-FUNC-RET node: ": rettype".
LIPtr Layout::ComputeRetType(const FmtContext& ctx) const
	{
	auto& returns = FindChild(Tag::Returns);
	return lit(Formatting(FindChild(Tag::Colon)) + " " +
		best(format_expr(*returns->FindTypeChild(), ctx)).Fmt());
	}

// ---- Lambda compute ------------------------------------------------------

// LAMBDA:          [0]=KW [1]=SP [2]=PARAMS [opt COLON RETURNS] BODY(last)
// LAMBDA-CAPTURES: [0]=KW [1]=SP [2]=CAPTURES [3]=PARAMS [opt ...] BODY(last)

// Prefix for arglist: keyword for plain lambda, keyword[captures]
// for lambda-with-captures.
LIPtr Layout::ComputeLambdaPrefix(const FmtContext& ctx) const
	{
	if ( GetTag() != Tag::LambdaCaptures )
		return lit(Formatting(Child(0, Tag::Keyword)));

	auto kw = Child(0, Tag::Keyword);
	auto captures = Child(2, Tag::Captures);
	auto clb = captures->Child(0, Tag::LBracket);
	const auto& crb = captures->Children().back();
	auto cap_items = collect_args(captures->Children());

	if ( cap_items.empty() )
		return lit(Formatting(kw) + clb + crb);

	auto cs = flat_or_fill(kw, clb, crb, "", cap_items, ctx);
	return lit(best(cs).Fmt());
	}

// Return type suffix for lambda: ": type" or empty.
LIPtr Layout::ComputeLambdaRet(const FmtContext& ctx) const
	{
	int pp = (GetTag() == Tag::LambdaCaptures) ? 3 : 2;
	auto after_params = Child(pp + 1);

	if ( after_params->GetTag() != Tag::Colon )
		return lit(Formatting());

	auto returns = Child(pp + 2, Tag::Returns);
	if ( auto rt = returns->FindTypeChild() )
		return lit(Formatting(after_params) + " " +
			best(format_expr(*rt, ctx)).Fmt());

	return lit(Formatting());
	}

// Body block for lambda: uses column-based indent so the
// Whitesmith block aligns to the next tab stop.
LIPtr Layout::ComputeLambdaBody(const FmtContext& ctx) const
	{
	int lambda_indent = ctx.Col() / INDENT_WIDTH;
	FmtContext body_ctx(lambda_indent, ctx.Col(), ctx.MaxCol() - ctx.Col());
	return lit(Children().back()->FormatWhitesmithBlock(body_ctx));
	}

// ---- Function declaration compute ----------------------------------------

// Optional return type suffix for FUNC-DECL: ": rettype" or empty.
LIPtr Layout::ComputeFuncRet(const FmtContext& ctx) const
	{
	auto returns = FindOptChild(Tag::Returns);
	if ( ! returns )
		return lit(Formatting());

	auto rt = returns->FindTypeChild();
	if ( ! rt )
		return lit(Formatting());

	return lit(Formatting(FindChild(Tag::Colon)) + " " +
		best(format_expr(*rt, ctx)).Fmt());
	}

// Attribute list for FUNC-DECL: bare attrs or empty.
LIPtr Layout::ComputeFuncAttrs(const FmtContext& ctx) const
	{
	auto attrs = FindOptChild(Tag::AttrList);
	if ( ! attrs )
		return lit(Formatting());

	auto as = attrs->FormatAttrList(ctx);
	if ( as.Empty() )
		return lit(Formatting());

	return lit(std::move(as));
	}

// Attribute list for CALL/CONSTRUCTOR: " &attr ..." or empty.
LIPtr Layout::ComputeCallAttrs(const FmtContext& ctx) const
	{
	auto attrs = FindOptChild(Tag::AttrList);
	if ( ! attrs )
		return lit(Formatting());

	return lit(" " + attrs->FormatAttrList(ctx));
	}

// Standalone BLOCK statement: Whitesmith braces at current indent.
LIPtr Layout::ComputeBlock(const FmtContext& ctx) const
	{
	auto& kids = Children();

	Formatting body;
	if ( kids.size() > 2 )
		{
		LayoutVec inner(kids.begin() + 1, kids.end() - 1);
		body = format_stmt_list(inner, ctx, true);
		}

	auto pad = line_prefix(ctx.Indent(), ctx.IndentCol());
	auto& rb = kids.back();
	return lit(Formatting(kids.front()) + "\n" + body +
	           rb->EmitPreComments(pad) + pad + rb);
	}

// Timeout clause for WHEN-TIMEOUT statement.
// Children: 0=kw 1=sp 2=( 3=expr 4=) 5=body 6=kw 7=sp 8=TIMEOUT
// TIMEOUT children: 0=interval 1={ 2=body 3=}
LIPtr Layout::ComputeWhenTimeout(const FmtContext& ctx) const
	{
	auto pad = line_prefix(ctx.Indent(), ctx.IndentCol());
	auto& tk = Child(8)->Children();

	auto interval = best(format_expr(*tk[0], ctx)).Fmt();

	auto block_ctx = ctx.Indented();
	auto brace_pad = line_prefix(block_ctx.Indent(), block_ctx.Col());
	auto body = format_stmt_list(tk[2]->Children(), block_ctx, true);
	auto rb_comments = tk[3]->EmitPreComments(brace_pad);

	return lit("\n" + pad + Formatting(Child(6)) + " " + interval +
	           "\n" + brace_pad + Formatting(tk[1]) + "\n" +
	           body + rb_comments + brace_pad + Formatting(tk[3]));
	}

// Trailing comment on a FUNC-DECL header (from the BODY node's
// text, which holds any COMMENT-TRAILING attached by the parser).
// Stays in the beam so the header accounts for its width.
LIPtr Layout::ComputeFuncTrailing(const FmtContext&) const
	{
	auto& text = Children().back()->Text();
	if ( text.empty() )
		return lit(Formatting());
	return lit(Formatting(text));
	}

// Whitesmith body block for FUNC-DECL, decoupled from header.
LIPtr Layout::ComputeFuncBodyBlock(const FmtContext& ctx) const
	{
	return lit(Children().back()->FormatWhitesmithBlock(ctx));
	}

// ---- Declaration formatting ----------------------------------------------

// Declarations: global/local/const/redef name [: type] [= init] [attrs] ;

// Shared state for declaration candidate generation.
struct DeclParts {
	Formatting head;	// "global foo", "local bar", etc.
	Formatting type_str;	// ": type" or ""
	Formatting suffix;	// " &attr1 &attr2;" or ";" or ""
	Formatting assign_op;	// "=", "+=", or ""
	LayoutPtr type_node;	// direct type child (after COLON)
	LayoutPtr colon_node;	// COLON before type
	LayoutPtr init_val;		// direct init value (after ASSIGN)
	LayoutPtr attrs_node;	// ATTR-LIST child
	LayoutPtr semi_node;	// SEMI child
};

// Build the suffix: attrs + optional semicolon.
static Formatting decl_suffix(const LayoutPtr& attrs_node,
                              const LayoutPtr& semi_node,
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

// Flat candidate + split-after-init for declarations with initializers.
static void decl_with_init(const DeclParts& d, Candidates& result,
                         const FmtContext& ctx)
	{
	auto before_val = d.head + d.type_str + " " + d.assign_op + " ";
	int before_w = before_val.Size();
	int suffix_w = d.suffix.Size();

	FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
	val_ctx.SetInitRHS();
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

	// Split after init operator when the flat candidate overflows
	// or when the value is multi-line (e.g. a constructor that
	// went vertical at high column but could be flat after split).
	int flat_mlo = flat.MaxLineOverflow(ctx.Col(), ctx.MaxCol());

	if ( flat_mlo > 0 || val.Lines() > 1 )
		{
		// Skip when the column savings from splitting are
		// too small to justify the extra line break.
		int savings = before_w - ctx.Indented().Col();
		if ( savings > 0 && savings < INDENT_WIDTH )
			return;

		FmtContext cont = ctx.Indented().Reserve(suffix_w);
		cont.SetInitRHS();
		auto val2 = best(format_expr(*d.init_val, cont));

		auto line1 = d.head + d.type_str + " " + d.assign_op;
		auto pad = line_prefix(cont.Indent(), cont.Col());

		// For tall values that don't end with a closing bracket
		// (e.g. pattern unions), put the suffix on its own line
		// at the declaration's base indent.
		std::string base;
		char last = val2.Fmt().Back();
		if ( val2.Lines() >= 3 && last != ')' &&
		     last != ']' && last != '}' )
			base = "\n" + line_prefix(ctx.Indent(), ctx.Col());

		Formatting split = line1 + "\n" + pad + val2.Fmt() + base +
					d.suffix;
		int last_w = split.LastLineLen();
		int lines = split.CountLines();
		int ovf = split.TextOverflow(ctx.Col(), ctx.MaxCol());

		// Skip split when the value stays single-line and
		// still overflows significantly - the value is
		// unsplittable, so the extra line doesn't help.
		if ( val2.Lines() == 1 && ovf > INDENT_WIDTH )
			return;

		result.push_back({split, last_w, lines, ovf, ctx.Col()});
		}
	}

// Shared setup for declarations that split type to a continuation line.
struct DeclContState {
	FmtContext cont;
	Formatting type_fmt;
	Formatting split_prefix;	// "head:" + newline + indent pad
};

static DeclContState decl_cont_setup(const DeclParts& d,
                                     const FmtContext& ctx,
                                     int suffix_w = 0)
	{
	auto base = ctx.Indented();
	auto cont = suffix_w > 0 ? base.Reserve(suffix_w) : base;
	auto type_fmt = best(format_expr(*d.type_node, cont)).Fmt();
	auto pad = line_prefix(cont.Indent(), cont.Col());
	auto prefix = d.head + d.colon_node + "\n" + pad;
	return {cont, std::move(type_fmt), std::move(prefix)};
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

	auto [cont, tv, prefix] = decl_cont_setup(d, ctx, 1);

	// Try type + suffix on one continuation line.
	auto oneline = tv + d.suffix;
	int oneline_w = oneline.CountLines() > 1 ?
			oneline.LastLineLen() : cont.Col() + oneline.Size();

	if ( oneline_w <= ctx.MaxCol() )
		{
		auto split = prefix + oneline;
		int last_w = split.LastLineLen();
		result.push_back({split, last_w, split.CountLines(),
					ovf(last_w, ctx), ctx.Col()});
		return;
		}

	// Type alone, attrs on continuation lines.
	Formatting type_suffix = d.attrs_node ? Formatting() : d.suffix;
	auto split = prefix + tv + type_suffix;

	if ( d.attrs_node )
		{
		auto apad = line_prefix(cont.Indent(), cont.Col() + 1);
		auto joined = d.attrs_node->FormatAttrList(ctx);
		int joined_w = cont.Col() + 1 + joined.Size() +
			d.semi_node->Width();

		if ( joined_w <= ctx.MaxCol() )
			split += "\n" + apad + joined + d.semi_node;
		else
			{
			auto astrs = d.attrs_node->FormatAttrStrings(ctx);
			for ( const auto& a : astrs )
				split += "\n" + apad + a;
			split += d.semi_node;
			}
		}

	int last_w = split.LastLineLen();
	result.push_back({split, last_w, split.CountLines(),
				ovf(last_w, ctx), ctx.Col()});
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
	int attr_col = ctx.Col() + d.head.Size() + 3;
	auto attr_pad = line_prefix(ctx.Indent(), attr_col);
	int max_col = ctx.MaxCol();
	int semi_w = d.semi_node->Width();

	// Check if all attrs fit on one continuation line.
	auto all_attrs = join_attrs(attr_strs);

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
		for ( size_t i = 0; i < attr_strs.size(); ++i )
			{
			wrapped += "\n" + attr_pad + attr_strs[i];
			int aw = attr_col + attr_strs[i].Size();
			if ( i + 1 == attr_strs.size() )
				aw += semi_w;
			if ( aw > max_col )
				ovf += aw - max_col;
			}

	wrapped += d.semi_node;

	int last_w = wrapped.LastLineLen();
	int lines = wrapped.CountLines();

	result.push_back({wrapped, last_w, lines, ovf, ctx.Col()});
	}

// Re-format the type at its actual column, letting it fill-pack,
// with suffix on the last line.  Produces a 2-line candidate when
// the type wraps but suffix fits on the last line.
static void decl_type_fill(const DeclParts& d, Candidates& result,
                           const FmtContext& ctx)
	{
	if ( ! d.type_node || d.init_val )
		return;

	int head_w = d.head.Size() + 2;  // "name: "
	int suffix_w = d.suffix.Size();
	FmtContext type_ctx = ctx.After(head_w).Reserve(suffix_w);
	auto tc = best(format_expr(*d.type_node, type_ctx));

	if ( tc.Lines() < 2 )
		return;  // flat already handled elsewhere

	auto filled = d.head + Formatting(d.colon_node) + " " +
			tc.Fmt() + d.suffix;
	int last_w = filled.LastLineLen();
	int lines = filled.CountLines();
	int ovf = filled.TextOverflow(ctx.Col(), ctx.MaxCol());

	result.push_back({filled, last_w, lines, ovf, ctx.Col()});
	}

// Split after colon: type (and optional init) on indented continuation.
static void decl_type_split(const DeclParts& d, Candidates& result,
				  const FmtContext& ctx)
	{
	if ( d.type_str.Empty() )
		return;

	auto [cont, bare_type, prefix] = decl_cont_setup(d, ctx);
	auto split = prefix + bare_type;

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
	int overflow = split.TextOverflow(ctx.Col(), ctx.MaxCol());

	result.push_back({split, last_w, lines, overflow, ctx.Col()});
	}

// GLOBAL-DECL/LOCAL-DECL: [0]=KEYWORD [1]=SP [2]=IDENTIFIER
//   [optional DECL-TYPE, DECL-INIT, ATTR-LIST] SEMI
Candidates Layout::ComputeDecl(const FmtContext& ctx) const
	{
	auto kw_node = Child(0, Tag::Keyword);
	auto id_node = Child(2, Tag::Identifier);

	DeclParts d;
	d.head = Formatting(kw_node) + " " + id_node;
	d.attrs_node = FindOptChild(Tag::AttrList);
	d.semi_node = FindChild(Tag::Semi);

	if ( auto dt = FindOptChild(Tag::DeclType) )
		{
		d.colon_node = dt->FindChild(Tag::Colon);
		if ( auto tc = dt->FindTypeChild() )
			d.type_node = tc;
		}

	if ( auto di = FindOptChild(Tag::DeclInit) )
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

	if ( result[0].Ovf() > 0 || result[0].Lines() > 1 )
		{
		decl_type_fill(d, result, ctx);
		decl_wrapped_attrs(d, result, ctx);
		decl_type_split(d, result, ctx);
		}

	return result;
	}

// ---- Type declaration formatting -----------------------------------------

// Format a record field.  suffix includes ";" and any trailing
// comment so we can measure overflow and wrap attrs if needed.
// FIELD: [0]=COLON [1]=type_expr [optional ATTR-LIST] [last]=SEMI
static Formatting format_field(const Layout& node, const Formatting& suffix,
                              const FmtContext& ctx)
	{
	Formatting head = node.Arg() + Formatting(node.Child(0, Tag::Colon));
	head += " ";

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

	return head + type_str + "\n" + pad + join_attrs(attr_strs) + suffix;
	}

// Collect enum values and their associated commas from a TYPE-ENUM node.
struct EnumValues {
	std::vector<std::string> values;
	LayoutVec nodes;
	LayoutVec commas;
	std::vector<bool> blank_before;
	LayoutPtr trailing_comma;
	bool has_init_values;
	bool has_comments;
};

static EnumValues collect_enum_values(const Layout& inner)
	{
	EnumValues ev;
	ev.has_init_values = false;
	ev.has_comments = false;
	LayoutPtr pending_comma;
	bool pending_blank = false;

	for ( const auto& c : inner.Children() )
		{
		if ( c->GetTag() == Tag::EnumValue )
			{
			auto v = c->Arg();
			if ( ! c->Arg(1).empty() )
				{
				v += " " + c->Arg(1);
				ev.has_init_values = true;
				}

			if ( ! c->PreComments().empty() )
				ev.has_comments = true;

			ev.values.push_back(v);
			ev.nodes.push_back(c);
			ev.commas.push_back(pending_comma);
			ev.blank_before.push_back(pending_blank);
			pending_comma = nullptr;
			pending_blank = false;
			}

		else if ( c->GetTag() == Tag::Comma )
			pending_comma = c;

		else if ( c->GetTag() == Tag::TrailingComma )
			ev.trailing_comma = c;

		else if ( c->GetTag() == Tag::Blank )
			pending_blank = true;
		}

	return ev;
	}

// Helper: build " &attr ..." suffix from an optional ATTR-LIST child.
static Formatting attr_suffix(const Layout& node, const FmtContext& ctx)
	{
	auto attrs = node.FindOptChild(Tag::AttrList);
	if ( ! attrs )
		return {};

	return " " + attrs->FormatAttrList(ctx);
	}

// Format enum values + close brace from a node whose children
// contain EnumValue, Comma, TrailingComma, and a closing RBrace.
static LIPtr format_enum_body(const Layout& source,
	const LayoutPtr& close_brace, const FmtContext& ctx,
	const Formatting& suffix = Formatting())
	{
	auto ev = collect_enum_values(source);

	// Try flat: " VALUE1, VALUE2 }" on the same line as "{".
	// Enums with init values or comments always use vertical layout.
	if ( ! ev.has_init_values && ! ev.has_comments )
		{
		Formatting flat_body(" ");
		for ( size_t i = 0; i < ev.values.size(); ++i )
			{
			if ( i > 0 )
				flat_body += ", ";
			flat_body += ev.values[i];
			}

		if ( ev.trailing_comma )
			flat_body += ",";

		flat_body += " ";
		flat_body += close_brace;
		flat_body += suffix;

		int flat_len = ctx.Col() + flat_body.Size();
		if ( flat_len <= ctx.MaxCol() )
			return lit(std::move(flat_body));
		}

	// Vertical: each value on its own line.
	auto pad = line_prefix(ctx.Indent() + 1,
				(ctx.Indent() + 1) * INDENT_WIDTH);
	Formatting body;
	for ( size_t i = 0; i < ev.values.size(); ++i )
		{
		if ( ev.blank_before[i] )
			body += "\n";
		body += ev.nodes[i]->EmitPreComments(pad);
		body += pad + ev.values[i];
		auto nc = (i + 1 < ev.commas.size()) ?
					ev.commas[i + 1] : nullptr;
		if ( nc || ev.trailing_comma )
			{
			if ( nc )
				body += nc;
			else
				{
				body += ",";
				body += ev.trailing_comma;
				}
			}
		body += "\n";
		}

	auto close_pad = line_prefix(ctx.Indent(), ctx.Col());
	return lit(Formatting("\n") + body + close_pad + close_brace + suffix);
	}

// Type alias: optional " &attr ..." + ";".
LIPtr Layout::ComputeTypeAliasSuffix(const FmtContext& ctx) const
	{
	auto sfx = attr_suffix(*this, ctx);
	auto semi = FindChild(Tag::Semi);
	return lit(sfx + Formatting(semi));
	}

// Enum body + close brace + optional attrs.  Inner = Child(5) = TYPE-ENUM.
LIPtr Layout::ComputeEnumBody(const FmtContext& ctx) const
	{
	auto inner = Child(5);
	return format_enum_body(*inner, inner->Children().back(), ctx,
				attr_suffix(*this, ctx));
	}

// Enum body + close brace for redef enum (values are direct children).
LIPtr Layout::ComputeRedefEnumBody(const FmtContext& ctx) const
	{
	return format_enum_body(*this, ChildFromEnd(1, Tag::RBrace), ctx);
	}

// Format record fields + close brace from a node whose children
// contain Field, Blank, and a closing RBrace.
static LIPtr format_record_body(const Layout& source,
	const LayoutPtr& close_brace, const FmtContext& ctx,
	const Formatting& suffix = Formatting())
	{
	int field_indent = ctx.Indent() + 1;
	int field_col = field_indent * INDENT_WIDTH;
	FmtContext field_ctx(field_indent, field_col, ctx.MaxCol() - field_col);
	auto field_pad = line_prefix(field_indent, field_col);

	Formatting body;
	for ( const auto& ki : source.Children() )
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

			auto sfx = Formatting(ki->Children().back()) +
						ki->Text().substr(ki->Arg().size());
			auto field_text = format_field(*ki, sfx, field_ctx);

			body += field_pad + field_text + "\n";
			}
		}

	auto close_pad = line_prefix(ctx.Indent(), ctx.Col());
	return lit(Formatting("\n") + body + close_pad + close_brace + suffix);
	}

// Record body + close brace + optional attrs.  Inner = Child(5) = TYPE-RECORD.
LIPtr Layout::ComputeRecordBody(const FmtContext& ctx) const
	{
	auto inner = Child(5);
	return format_record_body(*inner, inner->Children().back(), ctx,
					attr_suffix(*this, ctx));
	}

// Record body + close brace for redef record (fields are direct children).
LIPtr Layout::ComputeRedefRecordBody(const FmtContext& ctx) const
	{
	return format_record_body(*this, ChildFromEnd(1, Tag::RBrace), ctx,
					attr_suffix(*this, ctx));
	}

// ---- Switch formatting ---------------------------------------------------

// Switch statement: switch expr { case val: body ... }
static void append_case_body(const LayoutPtr& body, Formatting& result,
                           const FmtContext& ctx)
	{
	if ( ! body )
		return;

	auto text = format_stmt_list(body->Children(), ctx.Indented());
	if ( ! text.Empty() && text.Back() == '\n' )
		text.PopBack();

	result += "\n" + text;
	}

// SCHEDULE: [0]=KEYWORD [1]=SP [2]=interval [3]={ [4]=CALL [5]=}
// Three formats: flat, two-line (break before {), three-line
// (break before {, before call, and before }).
LIPtr Layout::ComputeSchedule(const FmtContext& ctx) const
	{
	auto kw = Formatting(Child(0));
	auto lb = Formatting(Child(3));
	auto rb = Formatting(Child(5));

	// Format interval in the header context (after "schedule ").
	int hdr_w = kw.Size() + 1;
	auto iv = best(format_expr(*Child(2), ctx.After(hdr_w))).Fmt();

	// Flat: schedule <expr> { <call> }
	int flat_pre = hdr_w + iv.Size() + 1 + lb.Size() + 1;
	int flat_trail = 1 + rb.Size();
	auto fc = ctx.After(flat_pre).Reserve(flat_trail);
	auto call_flat = best(format_expr(*Child(4), fc)).Fmt();
	auto flat = kw + " " + iv + " " + lb + " " + call_flat + " " + rb;
	Candidate flat_c(flat, ctx);

	if ( flat_c.Fits() )
		return lit(flat);

	// Two-line: schedule <expr>\n<pad>{ <call> }
	auto ind = ctx.Indented();
	auto pad = line_prefix(ind.Indent(), ind.Col());
	int two_pre = lb.Size() + 1;
	int two_trail = 1 + rb.Size();
	auto tc = ind.After(two_pre).Reserve(two_trail);
	auto call_two = best(format_expr(*Child(4), tc)).Fmt();
	auto two = kw + " " + iv + "\n" + pad + lb + " " + call_two + " " + rb;
	int tw = ind.Col() + two_pre + call_two.Size() + two_trail;
	int tl = 1 + call_two.CountLines();
	int to = two.TextOverflow(ctx.Col(), ctx.MaxCol());
	Candidate two_c(two, tw, tl, to, ctx.Col());

	if ( two_c.Fits() )
		return lit(two);

	// Three-line: schedule <expr>\n<pad>{\n<pad><call>\n<pad>}
	auto call_three = best(format_expr(*Child(4), ind)).Fmt();
	auto three = kw + " " + iv + "\n" + pad + lb + "\n" +
		     pad + call_three + "\n" + pad + rb;
	int thr_w = ind.Col() + rb.Size();
	int thr_l = 2 + call_three.CountLines() + 1;
	int thr_o = three.TextOverflow(ctx.Col(), ctx.MaxCol());

	Candidates cands;
	cands.push_back(two_c);
	cands.push_back({three, thr_w, thr_l, thr_o, ctx.Col()});
	return std::make_shared<LIDeclCandsR>(std::move(cands));
	}

// Switch expression: unwrap parens for Zeek-style ( expr ) spacing.
LIPtr Layout::ComputeSwitchExpr(const FmtContext& ctx) const
	{
	auto switch_expr = Child(2);

	if ( switch_expr->GetTag() == Tag::Paren )
		{
		auto pc = switch_expr->ContentChildren();
		if ( ! pc.empty() )
			return lit(Formatting(switch_expr->Child(0, Tag::LParen)) +
				" " + best(format_expr(*pc[0], ctx)).Fmt() +
				" " + switch_expr->Child(2, Tag::RParen));
		}

	return lit(best(format_expr(*switch_expr, ctx)).Fmt());
	}

// Format a single CASE node with fill-packed values.
// CASE: [0]=KEYWORD [1]=SP [2]=VALUES [3]=COLON [4]=BODY
static Formatting format_case(const Layout& c, const FmtContext& ctx)
	{
	auto& kw = c.Child(0, Tag::Keyword);
	auto values = c.Child(2, Tag::Values);
	Formatting case_text(kw);
	case_text += " ";

	// Collect formatted values and commas.
	std::vector<Formatting> vals;
	LayoutVec vcommas;
	LayoutPtr vpending;

	for ( const auto& vc : values->Children() )
		{
		Tag vt = vc->GetTag();

		if ( vt == Tag::Comma )
			{
			vpending = vc;
			continue;
			}

		if ( vc->IsToken() || vc->IsMarker() )
			continue;

		vals.push_back(best(format_expr(*vc, ctx)).Fmt());
		vcommas.push_back(vpending);
		vpending = nullptr;
		}

	// Fill-pack values, wrapping at comma.
	int case_col = ctx.Col() + case_text.Size();
	auto vpad = line_prefix(ctx.Indent(), case_col);
	int max_col = ctx.MaxCol();
	int cur_col = case_col;

	for ( size_t i = 0; i < vals.size(); ++i )
		{
		auto& vi = vals[i];
		int need = vi.Size();
		if ( i > 0 )
			need += 2;

		if ( i > 0 && cur_col + need > max_col )
			{
			case_text += Formatting(vcommas[i]) + "\n" + vpad;
			cur_col = case_col;
			}

		else if ( i > 0 )
			{
			case_text += Formatting(vcommas[i]) + " ";
			cur_col += 2;
			}

		case_text += vi;
		cur_col += vi.Size();
		}

	case_text += c.Child(3, Tag::Colon);
	return case_text;
	}

// Switch cases: format each CASE/DEFAULT with fill-packed values
// and indented bodies.
LIPtr Layout::ComputeSwitchCases(const FmtContext& ctx) const
	{
	auto pad = line_prefix(ctx.Indent(), ctx.Col());
	Formatting result;

	for ( const auto& c : Children() )
		{
		auto tag = c->GetTag();

		if ( tag == Tag::Blank )
			{
			result += "\n";
			continue;
			}

		if ( tag != Tag::Case && tag != Tag::Default )
			continue;

		// Emit pre-comments attached to this case.
		for ( const auto& pc : c->PreComments() )
			result += "\n" + pad + pc;

		auto& kw = c->Child(0, Tag::Keyword);

		if ( tag == Tag::Default )
			{
			result += "\n" + pad + kw + c->Child(1, Tag::Colon);
			append_case_body(c->FindOptChild(Tag::Body), result, ctx);
			continue;
			}

		result += "\n" + pad + format_case(*c, ctx);
		append_case_body(c->FindOptChild(Tag::Body), result, ctx);
		}

	// Emit pre-comments on the closing brace (they sit after
	// the last case but before the RBRACE).
	auto& last = Children().back();
	for ( const auto& pc : last->PreComments() )
		result += "\n" + pad + pc;

	return lit(std::move(result));
	}

// ---- Preproc formatting --------------------------------------------------

bool Layout::OpensDepth() const
	{
	return GetTag() == Tag::PreprocCond || Arg(0) == "@else";
	}

bool Layout::ClosesDepth() const
	{
	if ( GetTag() == Tag::PreprocCond )
		return false;

	const auto& d = Arg(0);
	return d == "@else" || d == "@endif";
	}

bool Layout::AtColumnZero() const
	{
	if ( GetTag() == Tag::PreprocCond )
		return true;

	const auto& d = Arg(0);
	return d == "@else" || d == "@endif";
	}

// ---- Binary operator chains -----------------------------------------------

// Collect all leaf operands from a left-recursive | chain of pattern
// constants.  Returns false if any leaf is not a /.../ pattern.
static bool collect_pattern_chain(const Layout& node,
                                  std::vector<std::string>& pats)
	{
	if ( node.GetTag() != Tag::BinaryOp || node.Arg() != "|" )
		return false;

	auto& lhs = *node.Child(0);
	auto& rhs = *node.Child(2);

	if ( rhs.GetTag() != Tag::Constant || rhs.Text().front() != '/' )
		return false;

	if ( lhs.GetTag() == Tag::BinaryOp && lhs.Arg() == "|" )
		{
		if ( ! collect_pattern_chain(lhs, pats) )
			return false;
		}
	else if ( lhs.GetTag() == Tag::Constant && lhs.Text().front() == '/' )
		pats.push_back(lhs.Text());
	else
		return false;

	pats.push_back(rhs.Text());
	return true;
	}

LIPtr Layout::ComputeBinaryOp(const FmtContext& ctx) const
	{
	// Count left-recursive chain depth for the same operator.
	int depth = 1;
	auto& op = Arg();
	const Layout* n = this;

	while ( n->Child(0)->GetTag() == Tag::BinaryOp &&
	        n->Child(0)->Arg() == op )
		{
		++depth;
		n = n->Child(0).get();
		}

	// Pattern union: a | chain where every leaf is a /.../ constant.
	// Format one-per-line when 3+ patterns.
	if ( op == "|" && depth >= 2 )
		{
		std::vector<std::string> pats;
		if ( collect_pattern_chain(*this, pats) && pats.size() >= 3 )
			{
			Formatting flat;
			for ( size_t i = 0; i < pats.size(); ++i )
				{
				if ( i > 0 )
					flat += " | ";
				flat += pats[i];
				}

			Candidates cands;
			Candidate fc(flat, ctx);
			if ( fc.Fits() )
				return lit(flat);

			// Only produce the vertical form at a clean indent
			// boundary.  At arbitrary inline columns (e.g. after
			// "name = "), the flat overflow steers the caller to
			// re-evaluate at a lower, indented column.
			if ( ctx.Col() == ctx.IndentCol() )
				{
				auto pad = line_prefix(ctx.Indent(), ctx.Col());
				Formatting vert;
				vert += "  " + pats[0];
				for ( size_t i = 1; i < pats.size(); ++i )
					vert += "\n" + pad + "| " + pats[i];

				int last_w = vert.LastLineLen();
				int lines = static_cast<int>(pats.size());
				int ovf = vert.TextOverflow(ctx.Col(),
							ctx.MaxCol());
				cands.push_back({vert, last_w, lines, ovf,
							ctx.Col()});
				}
			else
				cands.push_back(fc);

			return std::make_shared<LIDeclCandsR>(std::move(cands));
			}
		}

	if ( depth <= 6 )
		return std::make_shared<LIFlatSplitR>(
			FmtSteps{FmtStep::E(Child(0)), FmtStep::L(" "),
				FmtStep::L(Child(1)), FmtStep::S(),
				FmtStep::E(Child(2))},
			std::vector<SplitAt>{{2, SplitAt::IndentedOrSame}});

	// Deep chain: pre-format LHS to prevent exponential recursion.
	// Each level formats its LHS once (not 3x via flat/alt/split),
	// reducing 3^N to linear.
	auto lhs = best(format_expr(*Child(0), ctx)).Fmt();
	return std::make_shared<LIFlatSplitR>(
			FmtSteps{FmtStep::L(lhs), FmtStep::L(" "),
				FmtStep::L(Child(1)), FmtStep::S(),
				FmtStep::E(Child(2))},
			std::vector<SplitAt>{{2, SplitAt::IndentedOrSame}});
	}

// ---- Body/block formatting -----------------------------------------------

// Format a BODY or BLOCK node as a Whitesmith-style braced block.
Formatting Layout::FormatWhitesmithBlock(const FmtContext& ctx) const
	{
	auto block_ctx = ctx.Indented();
	auto brace_pad = line_prefix(block_ctx.Indent(), block_ctx.Col());

	// Extract the children between LBRACE and RBRACE, reading
	// trailing comments from the brace tokens themselves.
	auto lb = FindChild(Tag::LBrace);
	auto rb = FindChild(Tag::RBrace);
	auto close_trail = rb->Text().substr(1);
	LayoutVec inner;

	bool past_open = false;
	for ( const auto& c : Children() )
		{
		Tag t = c->GetTag();

		if ( t == Tag::LBrace )
			{
			past_open = true;
			continue;
			}

		if ( t != Tag::RBrace && past_open )
			inner.push_back(c);
		}

	if ( inner.empty() && ! lb->MustBreakAfter() &&
	     ! rb->MustBreakBefore() )
		return "\n" + brace_pad + lb + " " + rb;

	auto body_text = format_stmt_list(inner, block_ctx, true);

	// If the closing brace has a trailing comment, move it
	// to the last statement line, not the '}' itself.
	Formatting rb_fmt(rb);
	if ( ! close_trail.empty() && ! body_text.Empty() &&
	     body_text.Back() == '\n' )
		{
		body_text = body_text.Substr(0, body_text.Size() - 1) +
				close_trail + "\n";
		// Already relocated - use bare brace.
		int ct_size = static_cast<int>(close_trail.size());
		rb_fmt = rb_fmt.Substr(0, rb_fmt.Size() - ct_size);
		}

	auto rb_comments = rb->EmitPreComments(brace_pad);
	return "\n" + brace_pad + lb + "\n" +
		body_text + rb_comments + brace_pad + rb_fmt;
	}

// Format a single-statement body (no braces, indented one level).
static Formatting format_single_stmt_body(const Layout& body,
						const FmtContext& ctx)
	{
	auto text = format_stmt_list(body.Children(), ctx.Indented());

	// Strip trailing newline - the parent loop adds its own.
	if ( ! text.Empty() && text.Back() == '\n' )
		text.PopBack();

	return text;
	}

// Format a BODY node: Whitesmith block if first child is BLOCK,
// otherwise indented single-statement body.
FmtPtr Layout::FormatBodyText(const FmtContext& ctx) const
	{
	auto content = ContentChildren();
	if ( content.empty() || content[0]->GetTag() != Tag::Block )
		return std::make_shared<Formatting>("\n" +
					format_single_stmt_body(*this, ctx));

	auto result = content[0]->FormatWhitesmithBlock(ctx);

	// Format trailing children after the block (e.g., #@ END
	// comments associated with the body instead of the else).
	if ( content.size() > 1 )
		{
		LayoutVec trailing(content.begin() + 1, content.end());
		auto trail = format_stmt_list(trailing, ctx);
		if ( ! trail.Empty() && trail.Back() == '\n' )
			trail.PopBack();
		result += "\n" + trail;
		}

	return std::make_shared<Formatting>(result);
	}

// ---- Else follow-on ------------------------------------------------------

// Else follow-on for if-else.
LIPtr Layout::ComputeElseFollowOn(const FmtContext& ctx) const
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
	auto& sp = else_node->Child(1, Tag::Sp);

	result += "\n" + stmt_pad + Formatting(else_kw);

	// Include trailing comment on the SP after "else".
	if ( sp->MustBreakAfter() )
		result += Formatting(sp);

	if ( else_node->GetTag() == Tag::ElseIf )
		{
		int prefix_w = else_kw->Width() + 1;  // "else "
		auto inner_cs = else_child->Format(ctx, prefix_w);
		result += " " + best(inner_cs).Fmt();
		}

	else if ( else_child->GetTag() == Tag::Block )
		result += else_child->FormatWhitesmithBlock(ctx);

	else
		{
		auto else_ctx = ctx.Indented();
		auto epad = line_prefix(else_ctx.Indent(), else_ctx.Col());
		auto child_comments = else_child->EmitPreComments(epad);

		if ( ! child_comments->Empty() )
			{
			// Pre-comment ends with '\n'; strip it so the
			// statement's "\n" + epad provides the break.
			if ( child_comments->Back() == '\n' )
				child_comments->PopBack();
			result += "\n" + *child_comments;
			}

		auto cs = format_expr(*else_child, else_ctx);
		result += "\n" + epad + best(cs).Fmt();
		}

	return lit(std::move(result));
	}
