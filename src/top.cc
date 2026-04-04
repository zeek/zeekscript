#include <cstdio>
#include <unordered_map>

#include "expr_nodes.h"
#include "fmt_internal.h"

static Candidates FormatExprNode(const Node& node, const FmtContext& ctx)
	{
	return static_cast<const ExprNode&>(node).Format(ctx);
	}

// Dispatch table
const std::unordered_map<Tag, FormatFunc>& FormatDispatch()
	{
	static const std::unordered_map<Tag, FormatFunc> table = {
		{Tag::Identifier, FormatExprNode},
		{Tag::Constant, FormatExprNode},
		{Tag::FieldAccess, FormatExprNode},
		{Tag::FieldAssign, FormatExprNode},
		{Tag::BinaryOp, FormatExprNode},
		{Tag::BoolChain, FormatExprNode},
		{Tag::Div, FormatExprNode},
		{Tag::HasField, FormatExprNode},
		{Tag::Cardinality, FormatExprNode},
		{Tag::Negation, FormatExprNode},
		{Tag::UnaryOp, FormatExprNode},
		{Tag::Call, FormatExprNode},
		{Tag::Constructor, FormatExprNode},
		{Tag::Schedule, FormatExprNode},
		{Tag::Index, FormatExprNode},
		{Tag::IndexLiteral, FormatExprNode},
		{Tag::Slice, FormatExprNode},
		{Tag::Paren, FormatExprNode},
		{Tag::Interval, FormatExprNode},
		{Tag::TypeAtom, FormatExprNode},
		{Tag::TypeParameterized, FormatTypeParam},
		{Tag::Param, FormatParam},
		{Tag::TypeFunc, FormatTypeFunc},
		{Tag::Ternary, FormatExprNode},
		{Tag::Lambda, FormatExprNode},
		{Tag::GlobalDecl, FormatDecl},
		{Tag::LocalDecl, FormatDecl},
		{Tag::ModuleDecl, FormatModuleDecl},
		{Tag::ExprStmt, FormatExprStmt},
		{Tag::Return, FormatKeywordStmt},
		{Tag::Print, FormatPrint},
		{Tag::Add, FormatKeywordStmt},
		{Tag::Delete, FormatKeywordStmt},
		{Tag::Assert, FormatKeywordStmt},
		{Tag::EventStmt, FormatEventStmt},
		{Tag::FuncDecl, FormatFuncDecl},
		{Tag::IfNoElse, FormatCondBlock},
		{Tag::IfElse, FormatCondBlock},
		{Tag::For, FormatCondBlock},
		{Tag::While, FormatCondBlock},
		{Tag::ExportDecl, FormatExport},
		{Tag::TypeDecl, FormatTypeDecl},
		{Tag::Switch, FormatSwitch},
	};

	return table;
	}

Candidates FormatExpr(const Node& node, const FmtContext& ctx)
	{
	auto it = FormatDispatch().find(node.GetTag());
	if ( it != FormatDispatch().end() )
		return it->second(node, ctx);

	auto fallback = std::string("/* ") + TagToString(node.GetTag()) + " */";
	return {Candidate(fallback, ctx)};
	}

Candidates FormatNode(const Node& node, const FmtContext& ctx)
	{
	return FormatExpr(node, ctx);
	}

// Collect all trailing comments from node fields.
static void CollectTrailing(const Node& node,
                            std::vector<std::string>& out)
	{
	if ( ! node.TrailingComment().empty() )
		out.push_back(node.TrailingComment());
	for ( const auto& c : node.Children() )
		CollectTrailing(*c, out);
	}

// Check that every trailing comment appears on a line that has
// preceding content - never as a standalone line.
static void WarnStandaloneTrailing(const std::string& output,
                                   const NodeVec& nodes)
	{
	std::vector<std::string> trailing;
	for ( const auto& n : nodes )
		CollectTrailing(*n, trailing);

	for ( const auto& text : trailing )
		{
		auto pos = output.find(text);
		if ( pos == std::string::npos )
			{
			fprintf(stderr, "warning: trailing comment dropped: "
			        "%s\n", text.c_str());
			continue;
			}

		// Find the start of this line.
		auto sol = output.rfind('\n', pos);
		sol = (sol == std::string::npos) ? 0 : sol + 1;

		// Check whether there is non-whitespace before the comment.
		bool has_content = false;
		for ( auto i = sol; i < pos; ++i )
			if ( output[i] != ' ' && output[i] != '\t' )
				{
				has_content = true;
				break;
				}

		if ( ! has_content )
			fprintf(stderr, "warning: trailing comment on its own "
			        "line: %s\n", text.c_str());
		}
	}

std::string Format(const NodeVec& nodes)
	{
	static constexpr int MAX_WIDTH = 80;
	FmtContext ctx(0, 0, MAX_WIDTH);

	auto result = FormatStmtList(nodes, ctx);
	WarnStandaloneTrailing(result, nodes);
	return result;
	}
