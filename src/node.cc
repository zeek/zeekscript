#include <cstdio>
#include <stdexcept>
#include <unordered_map>

#include "node.h"
#include "expr.h"
#include "layout.h"
#include "stmt.h"

Candidates Node::Format(const FmtContext& ctx) const
	{
	auto fallback = std::string("/* ") + TagToString(tag) + " */";
	return {Candidate(fallback, ctx)};
	}

const std::string& Node::Arg(size_t i) const
	{
	static const std::string empty;
	return i < args.size() ? args[i] : empty;
	}

const NodePtr null_node;

const NodePtr& Node::Child(size_t i, Tag t) const
	{
	const auto& c = children[i];
	if ( c->GetTag() != t )
		throw std::runtime_error(std::string("internal error: ") +
					TagToString(tag) + " child " +
					std::to_string(i) + " is " +
					TagToString(c->GetTag()) + ", expected " +
					TagToString(t));
	return c;
	}

const NodePtr& Node::FindOptChild(Tag t) const
	{
	for ( const auto& c : children )
		if ( c->GetTag() == t )
			return c;
	return null_node;
	}

const NodePtr& Node::FindChild(Tag t) const
	{
	const auto& n = FindOptChild(t);
	if ( ! n )
		throw std::runtime_error(std::string("internal error: ") +
					TagToString(tag) + " has no " +
					TagToString(t) + " child");
	return n;
	}

const NodePtr& Node::FindChild(Tag t, const NodePtr& after) const
	{
	bool past = false;
	for ( const auto& c : children )
		{
		if ( c == after )
			past = true;
		else if ( past && c->GetTag() == t )
			return c;
		}

	return null_node;
	}

NodeVec Node::ContentChildren() const
	{
	NodeVec result;
	for ( const auto& c : children )
		if ( ! c->IsToken() && ! c->IsMarker() )
			result.push_back(c);

	return result;
	}

NodeVec Node::ContentChildren(const char* name, int n) const
	{
	auto result = ContentChildren();
	if ( static_cast<int>(result.size()) < n )
		throw FormatError(name + std::string(" node needs ") +
					std::to_string(n) + " children");

	return result;
	}

// Tag-to-layout table for purely declarative nodes.  Uses LIKind
// enum values directly instead of the soft_sp/indent_up/indent_down
// globals to avoid cross-TU static initialization order issues.
static const std::unordered_map<Tag, LayoutItems> layout_table = {
	{Tag::Identifier, {arg(0)}},
	{Tag::Constant, {arg(0)}},
	{Tag::TypeAtom, {arg(0)}},
	{Tag::Interval, {arg(0), " ", arg(1)}},
	{Tag::Cardinality, {0U, expr(1), 2}},
	{Tag::Negation, {0U, " ", expr(1)}},
	{Tag::UnaryOp, {0U, expr(1)}},
	{Tag::FieldAccess, {expr(0), 1, 2}},
	{Tag::FieldAssign, {0U, arg(0), 1, expr(2)}},
	{Tag::HasField, {expr(0), 1, 2}},
	{Tag::Paren, {0U, expr(1), 2}},
	{Tag::Schedule, {0U, {Sp}, expr(2), {Sp}, 3, {Sp}, expr(4), {Sp}, 5}},
	{Tag::Param, {arg(0), compute(&Node::ComputeParamType)}},
	{Tag::Call, {expr(0), arglist(1,
		AL_TrailingCommaVertical | AL_VerticalUpgrade)}},
	{Tag::Constructor, {0U, arglist(1,
		AL_TrailingCommaVertical | AL_FlatOrVertical)}},
	{Tag::IndexLiteral, {arglist(0,
		AL_AllCommentsVertical | AL_TrailingCommaFill)}},
	{Tag::Index, {expr(0), arglist(1)}},
	{Tag::TypeParameterized, {arg(0), arglist(0, &Node::ComputeOfType)}},
	{Tag::TypeOf, {arg(0), " ", 0U, " ", expr(2)}},
	{Tag::TypeFunc, {arg(0), arglist(0)}},
	{Tag::TypeFuncRet, {arg(0), arglist(0, &Node::ComputeRetType)}},
	{Tag::CommentLeading, {arg(0)}},
	{Tag::ExprStmt, {expr(0), last()}},
	{Tag::ReturnVoid, {0U, last()}},
	{Tag::Return, {0U, {Sp}, expr(2), last()}},
	{Tag::Add, {0U, {Sp}, expr(2), last()}},
	{Tag::Delete, {0U, {Sp}, expr(2), last()}},
	{Tag::Assert, {0U, {Sp}, expr(2), last()}},
	{Tag::Print, {fill_list(), last()}},
	{Tag::EventStmt, {0U, " ", arg(0), arglist(2), 3}},
	{Tag::ExportDecl, {0U, {Sp}, 2, {IndentUp},
		stmt_body(), {IndentDown}, last()}},
	{Tag::ModuleDecl, {0U, {Sp}, 2, 3}},
	{Tag::TypeDeclAlias, {0U, {Sp}, 2, 3, {Sp}, expr(5), 6}},
	{Tag::TypeDeclEnum, {0U, {Sp}, 2, 3, {Sp}, {5, 0U}, {Sp}, {5, 2},
		compute(&Node::ComputeEnumBody), last()}},
	{Tag::TypeDeclRecord, {0U, {Sp}, 2, 3, {Sp}, {5, 0U}, {Sp}, {5, 2},
		compute(&Node::ComputeRecordBody), last()}},
	{Tag::IfNoElse, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5)}},
	{Tag::IfElse, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5),
		compute(&Node::ComputeElseFollowOn)}},
	{Tag::While, {0U, " ", 2, " ", expr(3), " ", 4, body_text(5)}},
	{Tag::For, {0U, {Sp}, 2, {Sp},
		compute(&Node::ComputeForCond), body_text(8)}},
	{Tag::Slice, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1),
		 FmtStep::EI(2),
		 FmtStep::L(" "), FmtStep::TI(3), FmtStep::S(),
		 FmtStep::EI(4),
		 FmtStep::TI(5)},
		{{4, SplitAt::AlignWith, 2}}, true)}},
	{Tag::SlicePartial, {expr(0), 1, expr(2), 3, expr(4), 5}},
	{Tag::Div, {flat_split(
		{FmtStep::EI(0), FmtStep::TI(1),
		 FmtStep::S(""), FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame, true}})}},
	{Tag::BinaryOp, {flat_split(
		{FmtStep::EI(0), FmtStep::L(" "),
		 FmtStep::TI(1), FmtStep::S(),
		 FmtStep::EI(2)},
		{{2, SplitAt::IndentedOrSame}})}},
	{Tag::BoolChain, {op_fill()}},
	{Tag::Ternary, {flat_split(
		{FmtStep::EI(0),
		 FmtStep::L(" "), FmtStep::TI(1),
		 FmtStep::S(),
		 FmtStep::EI(2),
		 FmtStep::L(" "), FmtStep::TI(3),
		 FmtStep::S(),
		 FmtStep::EI(4)},
		{{6, SplitAt::AlignWith, 4},
		 {2, SplitAt::SameCol}})}},
};

NodePtr MakeNode(Tag tag)
	{
	auto it = layout_table.find(tag);
	if ( it != layout_table.end() )
		return std::make_shared<LayoutNode>(tag, it->second);

	switch ( tag ) {
	case Tag::Lambda: return std::make_shared<LambdaNode>();
	case Tag::LambdaCaptures: return std::make_shared<LambdaCapturesNode>();
	case Tag::GlobalDecl: return std::make_shared<DeclNode>(tag);
	case Tag::LocalDecl: return std::make_shared<DeclNode>(tag);
	case Tag::FuncDecl: return std::make_shared<FuncDeclNode>();
	case Tag::FuncDeclRet: return std::make_shared<FuncDeclNode>(tag);
	case Tag::Switch: return std::make_shared<SwitchNode>();
	case Tag::Preproc: return std::make_shared<PreprocNode>();
	case Tag::PreprocCond: return std::make_shared<PreprocCondNode>();
	default: return std::make_shared<Node>(tag);
	}
	}

static const std::unordered_map<Tag, const char*> token_syntax = {
	{Tag::Comma, ","}, {Tag::LParen, "("}, {Tag::RParen, ")"},
	{Tag::LBrace, "{"}, {Tag::RBrace, "}"}, {Tag::LBracket, "["},
	{Tag::RBracket, "]"}, {Tag::Colon, ":"}, {Tag::Dollar, "$"},
	{Tag::Question, "?"}, {Tag::Semi, ";"},
};

std::string Node::Text() const
	{
	auto it = token_syntax.find(tag);
	if ( it != token_syntax.end() )
		return std::string(it->second) + trailing_comment;

	if ( ! args.empty() )
		return args.back() + trailing_comment;

	return trailing_comment;
	}

static void print_quoted(const std::string& s)
	{
	putchar('"');

	for ( char c : s )
		{
		switch ( c ) {
		case '"': printf("\\\""); break;
		case '\\': printf("\\\\"); break;
		case '\n': printf("\\n"); break;
		case '\t': printf("\\t"); break;
		case '\r': printf("\\r"); break;
		default: putchar(c); break;
		}
		}

	putchar('"');
	}

static void do_indent(int n)
	{
	for ( int i = 0; i < n; ++i )
		printf("  ");
	}

void Node::Dump(int indent) const
	{
	// Emit pre-comments as COMMENT-LEADING siblings, then any
	// interleaved markers (BLANK etc.), before this node.
	for ( const auto& pc : pre_comments )
		{
		do_indent(indent);
		printf("COMMENT-LEADING ");
		print_quoted(pc);
		printf("\n");
		}

	for ( const auto& pm : pre_markers )
		pm->Dump(indent);

	do_indent(indent);

	printf("%s", TagToString(tag));

	for ( const auto& a : args )
		{
		putchar(' ');
		print_quoted(a);
		}

	if ( ! has_block )
		{
		printf("\n");

		// Emit trailing comment as a sibling COMMENT-TRAILING line.
		// Strip leading space added by SetTrailingComment.
		if ( ! trailing_comment.empty() )
			{
			do_indent(indent);
			printf("COMMENT-TRAILING ");
			print_quoted(trailing_comment.substr(1));
			printf("\n");
			}

		return;
		}

	if ( children.empty() && trailing_comment.empty() )
		{
		printf(" {\n");
		do_indent(indent);
		printf("}\n");
		return;
		}

	printf(" {\n");

	for ( const auto& child : children )
		child->Dump(indent + 1);

	do_indent(indent);
	printf("}\n");

	// Emit trailing comment as a sibling after the block.
	// Strip leading space added by SetTrailingComment.
	if ( ! trailing_comment.empty() )
		{
		do_indent(indent);
		printf("COMMENT-TRAILING ");
		print_quoted(trailing_comment.substr(1));
		printf("\n");
		}
	}
