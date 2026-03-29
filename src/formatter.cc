#include "formatter.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>

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

bool Candidate::BetterThan(const Candidate& o) const
	{
	if ( overflow != o.overflow )
		return overflow < o.overflow;
	if ( lines != o.lines )
		return lines < o.lines;
	return width < o.width;
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

// Measure overflow past MAX_WIDTH given an absolute end column.
static int Overflow(int end_col)
	{
	return end_col > MAX_WIDTH ? end_col - MAX_WIDTH : 0;
	}

// Count newlines in a string.
static int CountLines(const std::string& s)
	{
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

// Width of the last line in a (possibly multi-line) string,
// counting only characters (no tab expansion — candidates
// use spaces for alignment).
static int LastLineLen(const std::string& s)
	{
	auto pos = s.rfind('\n');
	if ( pos == std::string::npos )
		return static_cast<int>(s.size());
	return static_cast<int>(s.size() - pos - 1);
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
static Candidates FormatExpr(const Node& node,
                             const FmtContext& ctx);

// ------------------------------------------------------------------
// Atoms
// ------------------------------------------------------------------

static Candidates FormatIdentifier(const Node& node,
                                   const FmtContext&)
	{
	const auto& name = node.Arg();
	int w = static_cast<int>(name.size());
	return {{name, w}};
	}

static Candidates FormatConstant(const Node& node,
                                 const FmtContext&)
	{
	const auto& val = node.Arg();
	int w = static_cast<int>(val.size());
	return {{val, w}};
	}

// ------------------------------------------------------------------
// Field access: cert$field
// ------------------------------------------------------------------

static Candidates FormatFieldAccess(const Node& node,
                                    const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.size() < 2 )
		return {{"$???", 4}};

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	auto rhs_cs = FormatExpr(*kids[1],
	                         ctx.After(lhs.width + 1));
	const auto& rhs = Best(rhs_cs);

	std::string text = lhs.text + "$" + rhs.text;
	int w = lhs.width + 1 + rhs.width;
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Field assign: $field=expr
// ------------------------------------------------------------------

static Candidates FormatFieldAssign(const Node& node,
                                    const FmtContext& ctx)
	{
	const auto& field = node.Arg();
	std::string prefix = "$" + field + "=";
	int pw = static_cast<int>(prefix.size());

	if ( node.Children().empty() )
		return {{prefix, pw}};

	auto val_cs = FormatExpr(*node.Children()[0],
	                         ctx.After(pw));
	const auto& val = Best(val_cs);

	std::string text = prefix + val.text;
	int w = pw + val.width;
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Call: func(args)
// ------------------------------------------------------------------

// Format a comma-separated arg list on one line.
static Candidate FormatArgsFlat(
	const std::vector<const Node*>& args,
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
		text += best.text;
		w += best.width;
		}

	return {text, w};
	}

// Format args with line breaks: first N on line 1, rest
// start at align_col.
static Candidate FormatArgsSplit(
	const std::vector<const Node*>& args,
	int align_col, int indent,
	const FmtContext& first_line_ctx,
	size_t split_after)
	{
	std::string pad = LinePrefix(indent, align_col);
	std::string text;
	int max_end = 0;   // worst-case end column
	int cur_col;        // current absolute column
	int lines = 1;
	int total_overflow = 0;

	// First line starts at first_line_ctx.col.
	cur_col = first_line_ctx.col;

	for ( size_t i = 0; i < args.size(); ++i )
		{
		if ( i > 0 && i == split_after )
			{
			// Break to next line, aligned.
			int end = cur_col;
			if ( end > max_end )
				max_end = end;
			total_overflow += Overflow(end);

			text += ",\n" + pad;
			cur_col = align_col;
			++lines;
			}
		else if ( i > 0 )
			{
			text += ", ";
			cur_col += 2;
			}

		FmtContext sub(indent, cur_col);
		auto cs = FormatExpr(*args[i], sub);
		const auto& best = Best(cs);
		text += best.text;

		if ( best.lines > 1 )
			{
			lines += best.lines - 1;
			int last_len = LastLineLen(text);
			cur_col = last_len;
			}
		else
			cur_col += best.width;
		}

	int end = cur_col;
	if ( end > max_end )
		max_end = end;
	total_overflow += Overflow(end);

	return {text, cur_col, lines, total_overflow};
	}

static Candidates FormatCall(const Node& node,
                             const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		return {{"()", 2}};

	// Function name/expression.
	auto func_cs = FormatExpr(*kids[0], ctx);
	const auto& func = Best(func_cs);

	// Find ARGS child.
	const Node* args_node = nullptr;
	for ( const auto& c : kids )
		if ( c->GetTag() == Tag::Args )
			{
			args_node = c.get();
			break;
			}

	if ( ! args_node )
		{
		std::string text = func.text + "()";
		int w = func.width + 2;
		return {{text, w}};
		}

	// Collect actual argument expressions.
	std::vector<const Node*> args;
	for ( const auto& c : args_node->Children() )
		{
		Tag t = c->GetTag();
		if ( t != Tag::CommentLeading &&
		     t != Tag::CommentTrailing &&
		     t != Tag::CommentPrev )
			args.push_back(c.get());
		}

	if ( args.empty() )
		{
		std::string text = func.text + "()";
		int w = func.width + 2;
		return {{text, w}};
		}

	// Column right after "(" — alignment point for
	// continuation lines.
	int open_col = ctx.col + func.width + 1;

	// Context for args: starts after "(", must leave
	// room for ")".
	FmtContext args_ctx(ctx.indent, open_col,
	                    ctx.reserved + 1);

	// Try flat.
	auto flat = FormatArgsFlat(args, args_ctx);

	std::string flat_text = func.text + "(" +
	                        flat.text + ")";
	int flat_w = func.width + 1 + flat.width + 1;
	int flat_end = ctx.col + flat_w;
	int flat_ovf = Overflow(flat_end);

	Candidates result;
	result.push_back({flat_text, flat_w, flat.lines,
	                  flat_ovf});

	// If flat doesn't fit, try splitting at each position.
	if ( flat_ovf > 0 && args.size() > 1 )
		{
		for ( size_t split = 1;
		      split < args.size(); ++split )
			{
			auto sc = FormatArgsSplit(
				args, open_col, ctx.indent,
				args_ctx, split);

			std::string stext = func.text + "(" +
			    sc.text + ")";
			int last_w = sc.width + 1;  // ")"
			int sovf = sc.overflow +
			    Overflow(last_w);

			result.push_back({stext, last_w,
			                  sc.lines, sovf});
			}
		}

	return result;
	}

// ------------------------------------------------------------------
// Index: expr[subscripts]
// ------------------------------------------------------------------

static Candidates FormatIndex(const Node& node,
                              const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		return {{"[]", 2}};

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	const Node* subs_node = FindChild(node,
	                                  Tag::Subscripts);
	if ( ! subs_node || subs_node->Children().empty() )
		{
		std::string text = base.text + "[]";
		int w = base.width + 2;
		return {{text, w}};
		}

	// "[" + subscript + "]"
	FmtContext sub_ctx = ctx.After(base.width + 1);
	sub_ctx.reserved += 1;  // for "]"
	auto sub_cs = FormatExpr(
		*subs_node->Children()[0], sub_ctx);
	const auto& sub = Best(sub_cs);

	std::string text = base.text + "[" +
	                   sub.text + "]";
	int w = base.width + 1 + sub.width + 1;
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Index literal: [$field=expr, ...]
// ------------------------------------------------------------------

static Candidates FormatIndexLiteral(const Node& node,
                                     const FmtContext& ctx)
	{
	std::vector<const Node*> fields;
	for ( const auto& c : node.Children() )
		fields.push_back(c.get());

	if ( fields.empty() )
		return {{"[]", 2}};

	int open_col = ctx.col + 1;  // after "["

	FmtContext inner_ctx(ctx.indent, open_col,
	                     ctx.reserved + 1);

	auto flat = FormatArgsFlat(fields, inner_ctx);
	std::string flat_text = "[" + flat.text + "]";
	int flat_w = 1 + flat.width + 1;
	int flat_end = ctx.col + flat_w;
	int flat_ovf = Overflow(flat_end);

	Candidates result;
	result.push_back({flat_text, flat_w, flat.lines,
	                  flat_ovf});

	if ( flat_ovf > 0 && fields.size() > 1 )
		{
		for ( size_t split = 1;
		      split < fields.size(); ++split )
			{
			auto sc = FormatArgsSplit(
				fields, open_col, ctx.indent,
				inner_ctx, split);
			std::string st = "[" + sc.text + "]";
			int last_w = sc.width + 1;
			int sovf = sc.overflow +
			    Overflow(last_w);
			result.push_back({st, last_w, sc.lines,
			                  sovf});
			}
		}

	return result;
	}

// ------------------------------------------------------------------
// Slice: expr[lo:hi]
// ------------------------------------------------------------------

static Candidates FormatSlice(const Node& node,
                              const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		return {{"[:]", 3}};

	auto base_cs = FormatExpr(*kids[0], ctx);
	const auto& base = Best(base_cs);

	std::string lo, hi;

	if ( kids.size() == 3 )
		{
		auto lo_cs = FormatExpr(*kids[1], ctx);
		lo = Best(lo_cs).text;
		auto hi_cs = FormatExpr(*kids[2], ctx);
		hi = Best(hi_cs).text;
		}
	else if ( kids.size() == 2 )
		{
		auto c_cs = FormatExpr(*kids[1], ctx);
		const auto& c = Best(c_cs);

		if ( node.Arg() == "lo" )
			lo = c.text;
		else
			hi = c.text;
		}

	std::string text = base.text + "[" + lo + ":" +
	                   hi + "]";
	int w = static_cast<int>(text.size());
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Paren: (expr)
// ------------------------------------------------------------------

static Candidates FormatParen(const Node& node,
                              const FmtContext& ctx)
	{
	if ( node.Children().empty() )
		return {{"()", 2}};

	auto inner_cs = FormatExpr(*node.Children()[0],
	                           ctx.After(2));
	const auto& inner = Best(inner_cs);

	std::string text = "( " + inner.text + " )";
	int w = inner.width + 4;
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Unary: !expr, -expr, ~expr
// ------------------------------------------------------------------

static Candidates FormatUnary(const Node& node,
                              const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.empty() )
		return {{op, static_cast<int>(op.size())}};

	// Zeek style: space after "!".
	std::string prefix = op;
	if ( op == "!" )
		prefix += " ";

	int pw = static_cast<int>(prefix.size());
	auto operand_cs = FormatExpr(*kids[0],
	                             ctx.After(pw));
	const auto& operand = Best(operand_cs);

	std::string text = prefix + operand.text;
	int w = pw + operand.width;
	return {{text, w}};
	}

// ------------------------------------------------------------------
// Binary: lhs op rhs
// ------------------------------------------------------------------

static Candidates FormatBinary(const Node& node,
                               const FmtContext& ctx)
	{
	const auto& op = node.Arg();
	const auto& kids = node.Children();

	if ( kids.size() < 2 )
		return {{op, static_cast<int>(op.size())}};

	auto lhs_cs = FormatExpr(*kids[0], ctx);
	const auto& lhs = Best(lhs_cs);

	// " op " costs op.size() + 2.
	int op_w = static_cast<int>(op.size()) + 2;

	auto rhs_cs = FormatExpr(
		*kids[1], ctx.After(lhs.width + op_w));
	const auto& rhs = Best(rhs_cs);

	// Candidate 1: flat — lhs op rhs
	std::string flat = lhs.text + " " + op + " " +
	                   rhs.text;
	int flat_w = lhs.width + op_w + rhs.width;
	int flat_end = ctx.col + flat_w;
	int flat_ovf = Overflow(flat_end);

	Candidates result;

	if ( rhs.lines > 1 )
		{
		int last_w = LastLineLen(flat);
		result.push_back({flat, last_w,
		                  CountLines(flat), flat_ovf});
		}
	else
		result.push_back({flat, flat_w, 1, flat_ovf});

	// Candidate 2: split after operator, continuation
	// at next indent level.
	if ( flat_ovf > 0 )
		{
		FmtContext cont_ctx = ctx.Indented();

		auto rhs2_cs = FormatExpr(*kids[1], cont_ctx);
		const auto& rhs2 = Best(rhs2_cs);

		std::string cont_prefix = LinePrefix(
			cont_ctx.indent, cont_ctx.col);

		std::string split = lhs.text + " " + op +
		    "\n" + cont_prefix + rhs2.text;
		int line1_end = ctx.col + lhs.width + 1 +
		    static_cast<int>(op.size());
		int line2_end = cont_ctx.col + rhs2.width;
		int split_ovf = Overflow(line1_end) +
		    Overflow(line2_end);

		int split_lines = 1 + rhs2.lines;
		int last_w = rhs2.lines > 1 ?
			LastLineLen(split) :
			rhs2.width;

		result.push_back({split, last_w,
		                  split_lines, split_ovf});
		}

	// Candidate 3: split after operator, continuation
	// aligned to start of this expression (ctx.col).
	if ( flat_ovf > 0 &&
	     ctx.col > ctx.IndentCol() )
		{
		FmtContext cont_ctx = ctx.AtCol(ctx.col);

		auto rhs3_cs = FormatExpr(*kids[1], cont_ctx);
		const auto& rhs3 = Best(rhs3_cs);

		std::string cont_prefix = LinePrefix(
			ctx.indent, ctx.col);

		std::string split = lhs.text + " " + op +
		    "\n" + cont_prefix + rhs3.text;
		int line1_end = ctx.col + lhs.width + 1 +
		    static_cast<int>(op.size());
		int line2_end = ctx.col + rhs3.width;
		int split_ovf = Overflow(line1_end) +
		    Overflow(line2_end);

		int split_lines = 1 + rhs3.lines;
		int last_w = rhs3.lines > 1 ?
			LastLineLen(split) :
			rhs3.width;

		result.push_back({split, last_w,
		                  split_lines, split_ovf});
		}

	return result;
	}

// ------------------------------------------------------------------
// Expression dispatcher
// ------------------------------------------------------------------

static Candidates FormatExpr(const Node& node,
                             const FmtContext& ctx)
	{
	switch ( node.GetTag() ) {
	case Tag::Identifier:
		return FormatIdentifier(node, ctx);
	case Tag::Constant:
		return FormatConstant(node, ctx);
	case Tag::FieldAccess:
		return FormatFieldAccess(node, ctx);
	case Tag::FieldAssign:
		return FormatFieldAssign(node, ctx);
	case Tag::BinaryOp:
		return FormatBinary(node, ctx);
	case Tag::UnaryOp:
		return FormatUnary(node, ctx);
	case Tag::Call:
		return FormatCall(node, ctx);
	case Tag::Index:
		return FormatIndex(node, ctx);
	case Tag::IndexLiteral:
		return FormatIndexLiteral(node, ctx);
	case Tag::Slice:
		return FormatSlice(node, ctx);
	case Tag::Paren:
		return FormatParen(node, ctx);
	default:
		{
		const char* s = TagToString(node.GetTag());
		std::string text = std::string("/* ") +
		    s + " */";
		return {{text,
		         static_cast<int>(text.size())}};
		}
	}
	}

// ------------------------------------------------------------------
// Statements
// ------------------------------------------------------------------

static Candidates FormatExprStmt(const Node& node,
                                 const FmtContext& ctx)
	{
	const auto& kids = node.Children();
	if ( kids.empty() )
		return {{";", 1}};

	const Node* expr = nullptr;
	bool has_semi = false;

	for ( const auto& c : kids )
		{
		Tag t = c->GetTag();
		if ( t == Tag::Semi )
			has_semi = true;
		else if ( t != Tag::CommentLeading &&
		          t != Tag::CommentTrailing &&
		          t != Tag::CommentPrev )
			{
			if ( ! expr )
				expr = c.get();
			}
		}

	if ( ! expr )
		return {{";", 1}};

	int semi_cost = has_semi ? 1 : 0;

	FmtContext expr_ctx(ctx.indent, ctx.col,
	                    ctx.reserved + semi_cost);
	auto expr_cs = FormatExpr(*expr, expr_ctx);

	Candidates result;
	for ( const auto& ec : expr_cs )
		{
		std::string text = ec.text;
		int w = ec.width;

		if ( has_semi )
			{
			text += ";";
			w += 1;
			}

		int ovf = ec.overflow;
		if ( ec.lines == 1 )
			ovf = Overflow(ctx.col + w);

		result.push_back({text, w, ec.lines, ovf});
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
	FmtContext ctx(0, 0);
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

		if ( t == Tag::CommentLeading ||
		     t == Tag::CommentPrev )
			{
			result += FormatComment(node) + "\n";
			continue;
			}

		if ( t == Tag::CommentTrailing )
			{
			// Trailing comment attaches to prev line.
			if ( ! result.empty() &&
			     result.back() == '\n' )
				{
				result.pop_back();
				result += " " +
				    FormatComment(node) + "\n";
				}
			else
				result += FormatComment(node) +
				    "\n";

			continue;
			}

		Candidates cs;

		switch ( t ) {
		case Tag::ExprStmt:
			cs = FormatExprStmt(node, ctx);
			break;
		default:
			{
			const char* s = TagToString(t);
			std::string text = std::string(
				"/* TODO: ") + s + " */";
			cs.push_back({text,
			    static_cast<int>(text.size())});
			}
			break;
		}

		result += Best(cs).text + "\n";
		}

	return result;
	}

Candidates FormatNode(const Node& node,
                      const FmtContext& ctx)
	{
	switch ( node.GetTag() ) {
	case Tag::ExprStmt:
		return FormatExprStmt(node, ctx);
	default:
		return FormatExpr(node, ctx);
	}
	}
