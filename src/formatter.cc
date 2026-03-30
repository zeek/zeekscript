#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "formatter.h"

// ------------------------------------------------------------------
// Line prefix: tabs for indent, spaces for remaining offset
// ------------------------------------------------------------------

std::string LinePrefix(int indent, int col)
	{
	std::string s(indent, '\t');
	int space_col = indent * INDENT_WIDTH;

	if ( col > space_col )
		s.append(col - space_col, ' ');

	return s;
	}

// ------------------------------------------------------------------
// Candidate comparison
// ------------------------------------------------------------------

int Candidate::ComputeSpread(const std::string& t, int first_col)
	{
	int max_w = 0;
	int min_w = 99999;
	int line_w = first_col;

	for ( char c : t )
		{
		if ( c == '\n' )
			{
			if ( line_w > max_w )
				max_w = line_w;
			if ( line_w < min_w )
				min_w = line_w;
			line_w = 0;
			}
		else if ( c == '\t' )
			line_w = (line_w / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++line_w;
		}

	// Last line (after final \n or entire string).
	if ( line_w > max_w )
		max_w = line_w;
	if ( line_w < min_w )
		min_w = line_w;

	return max_w - min_w;
	}

bool Candidate::BetterThan(const Candidate& o) const
	{
	if ( Ovf() != o.Ovf() )
		return Ovf() < o.Ovf();
	if ( Lines() != o.Lines() )
		return Lines() < o.Lines();
	return Spread() < o.Spread();
	}

const Candidate& Best(const Candidates& cs)
	{
	assert(! cs.empty());
	const Candidate* best = &cs[0];

	for ( size_t i = 1; i < cs.size(); ++i )
		if ( cs[i].BetterThan(*best) )
			best = &cs[i];

	return *best;
	}

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

// How many columns a candidate overflows past the available context width,
// accounting for trailing reservation.
static int Ovf(int candidate_w, const FmtContext& ctx)
	{
	int avail = ctx.Width() - ctx.Trail();
	int excess = candidate_w - avail;
	return excess > 0 ? excess : 0;
	}

// Overflow ignoring trailing reservation (for intermediate lines).
static int OvfNoTrail(int candidate_w, const FmtContext& ctx)
	{
	int excess = candidate_w - ctx.Width();
	return excess > 0 ? excess : 0;
	}

// How many lines are needed to represent a string.
static int CountLines(const std::string& s)
	{
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

// Width of the last line in a (possibly multi-line) string, counting only
// characters (no tab expansion - candidates use spaces for alignment).
static int LastLineLen(const std::string& s)
	{
	auto n = s.size();
	auto pos = s.rfind('\n');
	if ( pos != std::string::npos )
		n -= (pos + 1);
	return static_cast<int>(n);
	}

// Find a child node by tag.  Returns nullptr if not found.
static const Node* FindChild(const Node& node, Tag tag)
	{
	for ( const auto& c : node.Children() )
		if ( c->GetTag() == tag )
			return c.get();
	return nullptr;
	}

// Forward declarations for mutual recursion.
static Candidates FormatExpr(const Node& node, const FmtContext& ctx);
static Candidates FormatExprStmt(const Node& node, const FmtContext& ctx);
static Candidates FormatDecl(const Node& node, const FmtContext& ctx);
static std::string FormatType(const Node& node, const FmtContext& ctx);

// ------------------------------------------------------------------
// Atoms
// ------------------------------------------------------------------

static Candidates FormatIdentifier(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(), ctx)};
	}

static Candidates FormatConstant(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(), ctx)};
	}

// ------------------------------------------------------------------
// Field access: rec$field
// ------------------------------------------------------------------

static Candidates FormatFieldAccess(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.size() < 2 )
		throw FormatError("FIELD-ACCESS node needs 2 children");

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	auto rhs_cs = FormatExpr(*kids[1], ctx.After(lhs.Width() + 1));
	const auto& rhs = Best(rhs_cs);

	return {lhs.Cat("$").Cat(rhs).In(ctx)};
	}

// ------------------------------------------------------------------
// Field assign: $field=expr
// ------------------------------------------------------------------

static Candidates FormatFieldAssign(const Node& node, const FmtContext& ctx)
	{
	Candidate prefix("$" + node.Arg() + "=", ctx);

	if ( node.Children().empty() )
		throw FormatError("FIELD-ASSIGN node needs a value child");

	auto val_cs = FormatExpr(*node.Children()[0], ctx.After(prefix.Width()));
	const auto& val = Best(val_cs);

	return {prefix.Cat(val).In(ctx)};
	}

// ------------------------------------------------------------------
// Call: func(args)
// ------------------------------------------------------------------

// Format a comma-separated arg list on one line.
static Candidate FormatArgsFlat(const std::vector<const Node*>& args,
				const FmtContext& ctx)
	{
	std::string text;
	int w = 0;

	for ( size_t i = 0; i < args.size(); ++i )
		{
		if ( i > 0 )
			{
			text += ", ";
			w += 2;
			}

		auto cs = FormatExpr(*args[i], ctx.After(w));
		const auto& best = Best(cs);
		text += best.Text();
		w += best.Width();
		}

	return {text, static_cast<int>(text.size()), 1, 0};
	}

// Format args with line breaks: first N on line 1, rest start at align_col.
static Candidate FormatArgsSplit(const std::vector<const Node*>& args,
				int align_col, int indent,
				const FmtContext& first_line_ctx,
				size_t split_after)
	{
	std::string pad = LinePrefix(indent, align_col);
	std::string text;

	int max_col = first_line_ctx.MaxCol();
	int cur_col = first_line_ctx.Col();
	int lines = 1;
	int total_overflow = 0;

	for ( size_t i = 0; i < args.size(); ++i )
		{
		if ( i > 0 && i == split_after )
			{
			text += ",\n" + pad;
			cur_col = align_col;
			++lines;
			}

		else if ( i > 0 )
			{
			text += ", ";
			cur_col += 2;
			}

		FmtContext sub(indent, cur_col, max_col - cur_col);
		auto cs = FormatExpr(*args[i], sub);
		const auto& best = Best(cs);
		text += best.Text();

		if ( best.Lines() > 1 )
			{
			lines += best.Lines() - 1;
			cur_col = LastLineLen(text);
			}
		else
			cur_col += best.Width();

		total_overflow += best.Ovf();
		}

	int end_ovf = cur_col > max_col ? cur_col - max_col : 0;
	total_overflow += end_ovf;

	return {text, cur_col, lines, total_overflow};
	}

static Candidates FormatCall(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("CALL node needs children");

	auto func_cs = FormatExpr(*kids[0], ctx);
	const auto& func = Best(func_cs);

	const Node* args_node = nullptr;
	for ( const auto& c : kids )
		if ( c->GetTag() == Tag::Args )
			{
			args_node = c.get();
			break;
			}

	if ( ! args_node )
		return {func.Cat("()").In(ctx)};

	std::vector<const Node*> args;
	for ( const auto& c : args_node->Children() )
		{
		Tag t = c->GetTag();
		if ( t != Tag::CommentLeading && t != Tag::CommentTrailing &&
		     t != Tag::CommentPrev )
			args.push_back(c.get());
		}

	if ( args.empty() )
		return {func.Cat("()").In(ctx)};

	// Column right after "(" - alignment point for continuation lines.
	int open_col = ctx.Col() + func.Width() + 1;

	// Args context: after "(", leaving room for ")".
	FmtContext args_ctx(ctx.Indent(), open_col,
	                    ctx.Width() - func.Width() - 2);

	// Try flat.
	auto flat = FormatArgsFlat(args, args_ctx);

	auto flat_c = func.Cat("(").Cat(flat).Cat(")").In(ctx);

	Candidates result;
	result.push_back(flat_c);

	if ( flat_c.Ovf() == 0 || args.size() <= 1 )
		return result;

	// If flat doesn't fit, try splitting at each position.
	for ( size_t split = 1; split < args.size(); ++split )
		{
		auto sc = FormatArgsSplit(args, open_col, ctx.Indent(),
					args_ctx, split);

		std::string stext = func.Text() + "(" + sc.Text() + ")";
		int last_w = sc.Width() + 1;  // ")"
		int sovf = sc.Ovf();

		result.push_back({stext, last_w, sc.Lines(), sovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Index: expr[subscripts]
// ------------------------------------------------------------------

static Candidates FormatIndex(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("INDEX node needs children");

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	const Node* subs_node = FindChild(node, Tag::Subscripts);
	if ( ! subs_node || subs_node->Children().empty() )
		return {base.Cat("[]").In(ctx)};

	int sub_col = ctx.Col() + base.Width() + 1;
	FmtContext bracket_ctx(ctx.Indent(), sub_col,
	                       ctx.Width() - base.Width() - 2);
	auto sub_cs = FormatExpr(*subs_node->Children()[0], bracket_ctx);
	const auto& sub = Best(sub_cs);

	return {base.Cat("[").Cat(sub).Cat("]").In(ctx)};
	}

// ------------------------------------------------------------------
// Index literal: [$field=expr, ...]
// ------------------------------------------------------------------

static Candidates FormatIndexLiteral(const Node& node, const FmtContext& ctx)
	{
	std::vector<const Node*> fields;
	for ( const auto& c : node.Children() )
		fields.push_back(c.get());

	if ( fields.empty() )
		return {Candidate("[]", ctx)};

	int open_col = ctx.Col() + 1;  // after "["

	// Width for contents: total minus "[" and "]".
	FmtContext inner_ctx(ctx.Indent(), open_col, ctx.Width() - 2);

	auto flat = FormatArgsFlat(fields, inner_ctx);
	auto flat_c = Candidate("[", ctx).Cat(flat).Cat("]").In(ctx);

	Candidates result;
	result.push_back(flat_c);

	if ( flat_c.Ovf() == 0 || fields.size() <= 1 )
		return result;

	for ( size_t split = 1; split < fields.size(); ++split )
		{
		auto sc = FormatArgsSplit(fields, open_col, ctx.Indent(),
					inner_ctx, split);
		std::string st = "[" + sc.Text() + "]";
		int last_w = sc.Width() + 1;
		int sovf = sc.Ovf();
		result.push_back({st, last_w, sc.Lines(), sovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Slice: expr[lo:hi]
// ------------------------------------------------------------------

static Candidates FormatSlice(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.size() < 3 )
		throw FormatError("SLICE node needs 3 children");

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	std::string lo = Best(FormatExpr(*kids[1], ctx)).Text();
	std::string hi = Best(FormatExpr(*kids[2], ctx)).Text();

	return {base.Cat("[" + lo + ":" + hi + "]").In(ctx)};
	}

// ------------------------------------------------------------------
// Paren: (expr)
// ------------------------------------------------------------------

static Candidates FormatParen(const Node& node, const FmtContext& ctx)
	{
	if ( node.Children().empty() )
		throw FormatError("PAREN node needs a child");

	auto inner_cs = FormatExpr(*node.Children()[0], ctx.After(2));
	const auto& inner = Best(inner_cs);

	return {Candidate("( ", ctx).Cat(inner).Cat(" )").In(ctx)};
	}

// ------------------------------------------------------------------
// Unary: ! expr, -expr, ~expr
// ------------------------------------------------------------------

static Candidates FormatUnary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.empty() )
		throw FormatError("UNARY-OP node needs a child");

	// Zeek style: space after "!".
	std::string ps = op;
	if ( op == "!" )
		ps += " ";

	Candidate prefix(ps, ctx);
	auto operand_cs = FormatExpr(*kids[0], ctx.After(prefix.Width()));
	const auto& operand = Best(operand_cs);

	return {prefix.Cat(operand).In(ctx)};
	}

// ------------------------------------------------------------------
// Binary: lhs op rhs
// ------------------------------------------------------------------

static Candidates FormatBinary(const Node& node, const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.size() < 2 )
		throw FormatError("BINARY-OP node needs 2 children");

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	// " op " costs op.size() + 2.
	int op_w = static_cast<int>(op.size()) + 2;

	auto rhs_cs = FormatExpr(*kids[1], ctx.After(lhs.Width() + op_w));
	const auto& rhs = Best(rhs_cs);

	// Candidate 1: flat - lhs op rhs
	std::string flat = lhs.Text() + " " + op + " " + rhs.Text();
	int flat_w = lhs.Width() + op_w + rhs.Width();
	int flat_ovf = Ovf(flat_w, ctx);

	Candidates result;

	if ( rhs.Lines() > 1 )
		{
		int last_w = LastLineLen(flat);
		result.push_back({flat, last_w, CountLines(flat),
		                  flat_ovf, ctx.Col()});
		}
	else
		result.push_back({flat, flat_w, 1, flat_ovf});

	// Candidate 2: split after operator, continuation at next indent level.
	if ( flat_ovf > 0 )
		{
		FmtContext cont_ctx = ctx.Indented();

		auto rhs2_cs = FormatExpr(*kids[1], cont_ctx);
		const auto& rhs2 = Best(rhs2_cs);

		std::string cont_prefix = LinePrefix(cont_ctx.Indent(),
							cont_ctx.Col());

		std::string split = lhs.Text() + " " + op + "\n" +
					cont_prefix + rhs2.Text();
		int line1_w = lhs.Width() + 1 + static_cast<int>(op.size());
		int line2_ovf = Ovf(rhs2.Width(), cont_ctx);
		int split_ovf = OvfNoTrail(line1_w, ctx) + line2_ovf;

		int split_lines = 1 + rhs2.Lines();
		int last_w = rhs2.Lines() > 1 ?
				LastLineLen(split) : rhs2.Width();

		result.push_back({split, last_w, split_lines, split_ovf,
		                  ctx.Col()});
		}

	// Candidate 3: split after operator, continuation aligned to start
	// of this expression (ctx.Col()).
	if ( flat_ovf == 0 || ctx.Col() <= ctx.IndentCol() )
		return result;

	FmtContext cont_ctx = ctx.AtCol(ctx.Col());

	auto rhs3_cs = FormatExpr(*kids[1], cont_ctx);
	const auto& rhs3 = Best(rhs3_cs);

	std::string cont_prefix = LinePrefix(ctx.Indent(), ctx.Col());

	std::string split = lhs.Text() + " " + op + "\n" + cont_prefix +
				rhs3.Text();
	int line1_w = lhs.Width() + 1 + static_cast<int>(op.size());
	int line2_ovf = Ovf(rhs3.Width(), cont_ctx);
	int split_ovf = OvfNoTrail(line1_w, ctx) + line2_ovf;

	int split_lines = 1 + rhs3.Lines();
	int last_w = rhs3.Lines() > 1 ?  LastLineLen(split) : rhs3.Width();

	result.push_back({split, last_w, split_lines, split_ovf, ctx.Col()});

	return result;
	}

// ------------------------------------------------------------------
// Interval: 1 sec, 3.5 hrs
// ------------------------------------------------------------------

static Candidates FormatInterval(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(0) + " " + node.Arg(1), ctx)};
	}

// ------------------------------------------------------------------
// Types
// ------------------------------------------------------------------

// TYPE-ATOM: string, count, addr, etc.
static Candidates FormatTypeAtom(const Node& node, const FmtContext& ctx)
	{
	return {Candidate(node.Arg(), ctx)};
	}

// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t
// Children are type args plus optional OF marker.
static Candidates FormatTypeParam(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "table", "set", "vector"
	const auto& kids = node.Children();

	// Collect bracketed type args and "of" type.
	std::vector<const Node*> bracket_types;
	const Node* of_type = nullptr;
	bool past_of = false;

	for ( const auto& c : kids )
		{
		if ( c->GetTag() == Tag::Of )
			past_of = true;
		else if ( past_of )
			of_type = c.get();
		else
			bracket_types.push_back(c.get());
		}

	std::string text = keyword;

	if ( ! bracket_types.empty() )
		{
		text += "[";
		for ( size_t i = 0; i < bracket_types.size(); ++i )
			{
			if ( i > 0 )
				text += ", ";
			text += Best(FormatExpr(*bracket_types[i],
				ctx)).Text();
			}
		text += "]";
		}

	if ( of_type )
		text += " of " + Best(FormatExpr(*of_type, ctx)).Text();

	return {Candidate(text, ctx)};
	}

// TYPE-FUNC: event(params), function(params): rettype
static Candidates FormatTypeFunc(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg();	// "event", "function", "hook"
	std::string text = keyword + "(";

	const Node* params = FindChild(node, Tag::Params);
	const Node* returns = FindChild(node, Tag::Returns);

	if ( params )
		{
		bool first = true;
		for ( const auto& p : params->Children() )
			{
			if ( p->GetTag() == Tag::CommentLeading ||
			     p->GetTag() == Tag::CommentTrailing ||
			     p->GetTag() == Tag::CommentPrev )
				continue;

			if ( ! first )
				text += ", ";
			first = false;

			// PARAM "name" { TYPE-ATOM "t" }
			text += p->Arg();
			const Node* ptype = FindChild(*p, Tag::TypeAtom);
			if ( ! ptype )
				ptype = FindChild(*p, Tag::TypeParameterized);
			if ( ! ptype )
				ptype = FindChild(*p, Tag::TypeFunc);

			if ( ptype )
				text += ": " + Best(FormatExpr(*ptype,
					ctx)).Text();
			}
		}

	text += ")";

	if ( returns )
		{
		for ( const auto& c : returns->Children() )
			{
			Tag t = c->GetTag();
			if ( t == Tag::TypeAtom || t == Tag::TypeParameterized ||
			     t == Tag::TypeFunc )
				{
				text += ": " + Best(FormatExpr(*c,
					ctx)).Text();
				break;
				}
			}
		}

	return {Candidate(text, ctx)};
	}

// TYPE wrapper node: contains a type child.
static std::string FormatType(const Node& node, const FmtContext& ctx)
	{
	for ( const auto& c : node.Children() )
		{
		Tag t = c->GetTag();
		if ( t == Tag::TypeAtom || t == Tag::TypeParameterized ||
		     t == Tag::TypeFunc )
			return Best(FormatExpr(*c, ctx)).Text();
		}

	return "";
	}

// ------------------------------------------------------------------
// Attributes: &redef, &default=expr
// ------------------------------------------------------------------

static std::string FormatAttrList(const Node& node, const FmtContext& ctx)
	{
	std::string text;

	for ( const auto& attr : node.Children() )
		{
		if ( attr->GetTag() != Tag::Attr )
			continue;

		if ( ! text.empty() )
			text += " ";

		text += attr->Arg();	// e.g. "&default", "&redef"

		// If the attr has a value child, append =value.
		if ( ! attr->Children().empty() )
			{
			auto val_cs = FormatExpr(*attr->Children()[0], ctx);
			text += "=" + Best(val_cs).Text();
			}
		}

	return text;
	}

// ------------------------------------------------------------------
// Declarations: global/local/const/redef name [: type] [= init] [attrs] ;
// ------------------------------------------------------------------

// Build the suffix: attrs + optional semicolon.
static std::string DeclSuffix(const Node* attrs_node, bool has_semi,
                              const FmtContext& ctx)
	{
	std::string suffix;

	if ( attrs_node )
		{
		std::string as = FormatAttrList(*attrs_node, ctx);
		if ( ! as.empty() )
			suffix += " " + as;
		}

	if ( has_semi )
		suffix += ";";

	return suffix;
	}

static Candidates FormatDecl(const Node& node, const FmtContext& ctx)
	{
	const auto& keyword = node.Arg(0);	// "global", "local", etc.
	const auto& name = node.Arg(1);

	std::string head = keyword + " " + name;

	// Look for optional parts.
	const Node* type_node = FindChild(node, Tag::Type);
	const Node* init_node = FindChild(node, Tag::Init);
	const Node* attrs_node = FindChild(node, Tag::AttrList);
	bool has_semi = FindChild(node, Tag::Semi) != nullptr;

	// Build type string if present.
	std::string type_str;
	if ( type_node )
		{
		type_str = FormatType(*type_node, ctx);
		if ( ! type_str.empty() )
			type_str = ": " + type_str;
		}

	std::string suffix = DeclSuffix(attrs_node, has_semi, ctx);

	// --- Candidate 1: flat ---
	Candidates result;

	if ( init_node )
		{
		const auto& op = init_node->Arg();	// "=", "+="

		if ( init_node->Children().empty() )
			throw FormatError("INIT node needs a value child");

		std::string before_val = head + type_str + " " + op + " ";
		int before_w = static_cast<int>(before_val.size());

		int suffix_w = static_cast<int>(suffix.size());
		FmtContext val_ctx = ctx.After(before_w).Reserve(suffix_w);
		auto val_cs = FormatExpr(*init_node->Children()[0], val_ctx);
		const auto& val = Best(val_cs);

		std::string flat = before_val + val.Text() + suffix;
		result.push_back(Candidate(flat, ctx));

		// --- Candidate 2: split after init operator ---
		if ( result[0].Ovf() > 0 )
			{
			FmtContext cont = ctx.Indented().Reserve(suffix_w);
			auto val2_cs = FormatExpr(*init_node->Children()[0],
						cont);
			const auto& val2 = Best(val2_cs);

			std::string line1 = head + type_str + " " + op;
			std::string pad = LinePrefix(cont.Indent(),
						cont.Col());
			std::string split = line1 + "\n" + pad +
						val2.Text() + suffix;
			int last_w = LastLineLen(split);
			int lines = CountLines(split);
			int ovf = OvfNoTrail(
				static_cast<int>(line1.size()), ctx) +
				Ovf(last_w, ctx);
			result.push_back({split, last_w, lines, ovf,
			                  ctx.Col()});
			}
		}
	else
		{
		std::string flat = head + type_str + suffix;
		result.push_back(Candidate(flat, ctx));
		}

	// --- Candidate 3: split after colon (type on next line) ---
	if ( ! type_str.empty() && result[0].Ovf() > 0 )
		{
		FmtContext cont = ctx.Indented();
		// Type string without leading ": ".
		std::string bare_type = type_str.substr(2);

		std::string line1 = head + ":";
		std::string pad = LinePrefix(cont.Indent(), cont.Col());

		std::string split = line1 + "\n" + pad + bare_type;

		if ( init_node )
			{
			const auto& op = init_node->Arg();
			int suffix_w = static_cast<int>(suffix.size());
			FmtContext val_ctx = cont.After(
				static_cast<int>(bare_type.size()) +
				static_cast<int>(op.size()) + 2).Reserve(
				suffix_w);
			auto val_cs = FormatExpr(
				*init_node->Children()[0], val_ctx);

			split += " " + op + " " + Best(val_cs).Text();
			}

		split += suffix;

		int last_w = LastLineLen(split);
		int lines = CountLines(split);
		int ovf = OvfNoTrail(
			static_cast<int>(line1.size()), ctx) +
			Ovf(last_w, ctx);
		result.push_back({split, last_w, lines, ovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Dispatch table
// ------------------------------------------------------------------

using FormatFunc = Candidates (*)(const Node&, const FmtContext&);

static const std::unordered_map<Tag, FormatFunc>& FormatDispatch()
	{
	static const std::unordered_map<Tag, FormatFunc> table = {
		{Tag::Identifier, FormatIdentifier},
		{Tag::Constant, FormatConstant},
		{Tag::FieldAccess, FormatFieldAccess},
		{Tag::FieldAssign, FormatFieldAssign},
		{Tag::BinaryOp, FormatBinary},
		{Tag::UnaryOp, FormatUnary},
		{Tag::Call, FormatCall},
		{Tag::Index, FormatIndex},
		{Tag::IndexLiteral, FormatIndexLiteral},
		{Tag::Slice, FormatSlice},
		{Tag::Paren, FormatParen},
		{Tag::Interval, FormatInterval},
		{Tag::TypeAtom, FormatTypeAtom},
		{Tag::TypeParameterized, FormatTypeParam},
		{Tag::TypeFunc, FormatTypeFunc},
		{Tag::GlobalDecl, FormatDecl},
		{Tag::LocalDecl, FormatDecl},
		{Tag::ExprStmt, FormatExprStmt},
	};

	return table;
	}

static Candidates FormatExpr(const Node& node, const FmtContext& ctx)
	{
	auto it = FormatDispatch().find(node.GetTag());
	if ( it != FormatDispatch().end() )
		return it->second(node, ctx);

	return {Candidate(std::string("/* ") + TagToString(node.GetTag()) + " */", ctx)};
	}

// ------------------------------------------------------------------
// Statements
// ------------------------------------------------------------------

static Candidates FormatExprStmt(const Node& node, const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		throw FormatError("EXPR-STMT node needs children");

	const Node* expr = nullptr;
	bool has_semi = false;

	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			has_semi = true;

		else if ( t != Tag::CommentLeading &&
		          t != Tag::CommentTrailing && t != Tag::CommentPrev )
			{
			if ( ! expr )
				expr = c.get();
			}
		}

	if ( ! expr )
		return {Candidate(";", ctx)};

	// Reserve trailing space for the semicolon.
	int semi_cost = has_semi ? 1 : 0;
	FmtContext expr_ctx = ctx.Reserve(semi_cost);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = ec.Text();
		int w = ec.Width();

		if ( has_semi )
			{
			text += ";";
			++w;
			}

		int ovf = ec.Ovf();
		if ( ec.Lines() == 1 )
			ovf = Ovf(w, ctx);

		result.push_back({text, w, ec.Lines(), ovf, ctx.Col()});
		}

	return result;
	}

// ------------------------------------------------------------------
// Top-level formatting
// ------------------------------------------------------------------

static std::string FormatComment(const Node& node)
	{
	return node.Arg();
	}

std::string Format(const Node::NodeVec& nodes)
	{
	static constexpr int MAX_WIDTH = 80;
	FmtContext ctx(0, 0, MAX_WIDTH);

	std::string result;

	for ( size_t i = 0; i < nodes.size(); ++i )
		{
		const auto& node = *nodes[i];
		Tag t = node.GetTag();

		if ( t == Tag::Blank )
			{
			result += "\n";
			continue;
			}

		if ( t == Tag::CommentLeading || t == Tag::CommentPrev )
			{
			result += FormatComment(node) + "\n";
			continue;
			}

		if ( t == Tag::CommentTrailing )
			{
			// Orphan trailing comment (not preceded by a
			// statement) - emit on its own line.
			result += FormatComment(node) + "\n";
			continue;
			}

		// Peek ahead: if the next node is a trailing comment,
		// we'll append it to the last line of this statement.
		std::string trailing;
		if ( i + 1 < nodes.size() &&
		     nodes[i + 1]->GetTag() == Tag::CommentTrailing )
			{
			trailing = " " + FormatComment(*nodes[i + 1]);
			++i;  // consume the comment node
			}

		auto it = FormatDispatch().find(t);
		if ( it == FormatDispatch().end() )
			{
			const char* s = TagToString(t);
			std::string text = std::string("/* TODO: ") + s + " */";
			result += text + trailing + "\n";
			continue;
			}

		int trail_w = static_cast<int>(trailing.size());
		FmtContext stmt_ctx = ctx.Reserve(trail_w);
		auto cs = it->second(node, stmt_ctx);
		result += Best(cs).Text() + trailing + "\n";
		}

	return result;
	}

Candidates FormatNode(const Node& node, const FmtContext& ctx)
	{
	auto it = FormatDispatch().find(node.GetTag());
	if ( it != FormatDispatch().end() )
		return it->second(node, ctx);

	return {Candidate(std::string("/* ") + TagToString(node.GetTag()) + " */", ctx)};
	}
