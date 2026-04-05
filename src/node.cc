#include "node.h"
#include "condition_block.h"
#include "expr.h"
#include "formatter.h"
#include "stmt.h"

#include <cstdio>
#include <stdexcept>
#include <unordered_map>

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

const Node* Node::Child(size_t i, Tag t) const
	{
	const Node* c = children[i].get();
	if ( c->GetTag() != t )
		throw std::runtime_error(std::string("internal error: ") +
					TagToString(tag) + " child " +
					std::to_string(i) + " is " +
					TagToString(c->GetTag()) + ", expected " +
					TagToString(t));
	return c;
	}

const Node* Node::FindOptChild(Tag t) const
	{
	for ( const auto& c : children )
		if ( c->GetTag() == t )
			return c.get();
	return nullptr;
	}

const Node* Node::FindChild(Tag t) const
	{
	const Node* n = FindOptChild(t);
	if ( ! n )
		throw std::runtime_error(std::string("internal error: ") +
					TagToString(tag) + " has no " +
					TagToString(t) + " child");
	return n;
	}

const Node* Node::FindChild(Tag t, const Node* after) const
	{
	bool past = false;
	for ( const auto& c : children )
		{
		if ( c.get() == after )
			past = true;
		else if ( past && c->GetTag() == t )
			return c.get();
		}

	return nullptr;
	}

Nodes Node::ContentChildren() const
	{
	Nodes result;
	for ( const auto& c : children )
		if ( ! c->IsToken() && ! c->IsMarker() )
			result.push_back(c.get());

	return result;
	}

Nodes Node::ContentChildren(const char* name, int n) const
	{
	auto result = ContentChildren();
	if ( static_cast<int>(result.size()) < n )
		throw FormatError(name + std::string(" node needs ") +
					std::to_string(n) + " children");

	return result;
	}

std::shared_ptr<Node> MakeNode(Tag tag)
	{
	switch ( tag ) {
	case Tag::IfNoElse: return std::make_shared<IfNoElseNode>();
	case Tag::IfElse: return std::make_shared<IfElseNode>();
	case Tag::For: return std::make_shared<ForNode>();
	case Tag::While: return std::make_shared<WhileNode>();
	case Tag::Identifier: return std::make_shared<AtomNode>(tag);
	case Tag::Constant: return std::make_shared<AtomNode>(tag);
	case Tag::TypeAtom: return std::make_shared<AtomNode>(tag);
	case Tag::Interval: return std::make_shared<IntervalNode>();
	case Tag::Cardinality: return std::make_shared<CardinalityNode>();
	case Tag::Negation: return std::make_shared<NegationNode>();
	case Tag::UnaryOp: return std::make_shared<UnaryNode>();
	case Tag::BinaryOp: return std::make_shared<BinaryNode>();
	case Tag::BoolChain: return std::make_shared<BoolChainNode>();
	case Tag::HasField: return std::make_shared<HasFieldNode>();
	case Tag::Div: return std::make_shared<DivNode>();
	case Tag::FieldAccess: return std::make_shared<FieldAccessNode>();
	case Tag::FieldAssign: return std::make_shared<FieldAssignNode>();
	case Tag::Call: return std::make_shared<CallNode>();
	case Tag::Constructor: return std::make_shared<ConstructorNode>();
	case Tag::Index: return std::make_shared<IndexNode>();
	case Tag::IndexLiteral: return std::make_shared<IndexLiteralNode>();
	case Tag::Slice: return std::make_shared<SliceNode>();
	case Tag::Paren: return std::make_shared<ParenNode>();
	case Tag::Schedule: return std::make_shared<ScheduleNode>();
	case Tag::Ternary: return std::make_shared<TernaryNode>();
	case Tag::Lambda: return std::make_shared<LambdaNode>();
	case Tag::LambdaCaptures: return std::make_shared<LambdaCapturesNode>();
	case Tag::TypeParameterized: return std::make_shared<TypeParamNode>();
	case Tag::Param: return std::make_shared<ParamNode>();
	case Tag::TypeFunc: return std::make_shared<TypeFuncNode>();
	case Tag::CommentLeading: return std::make_shared<CommentNode>();
	case Tag::ExprStmt: return std::make_shared<ExprStmtNode>();
	case Tag::Return: return std::make_shared<KeywordStmtNode>(tag);
	case Tag::Add: return std::make_shared<KeywordStmtNode>(tag);
	case Tag::Delete: return std::make_shared<KeywordStmtNode>(tag);
	case Tag::Assert: return std::make_shared<KeywordStmtNode>(tag);
	case Tag::EventStmt: return std::make_shared<EventStmtNode>();
	case Tag::Print: return std::make_shared<KeywordStmtNode>(Tag::Print);
	case Tag::GlobalDecl: return std::make_shared<DeclNode>(tag);
	case Tag::LocalDecl: return std::make_shared<DeclNode>(tag);
	case Tag::ModuleDecl: return std::make_shared<ModuleDeclNode>();
	case Tag::FuncDecl: return std::make_shared<FuncDeclNode>();
	case Tag::ExportDecl: return std::make_shared<ExportNode>();
	case Tag::Switch: return std::make_shared<SwitchNode>();
	case Tag::TypeDeclAlias: return std::make_shared<TypeDeclAliasNode>();
	case Tag::TypeDeclEnum: return std::make_shared<TypeDeclEnumNode>();
	case Tag::TypeDeclRecord: return std::make_shared<TypeDeclRecordNode>();
	case Tag::Preproc: return std::make_shared<PreprocNode>();
	case Tag::PreprocCond: return std::make_shared<PreprocCondNode>();
	default: return std::make_shared<Node>(tag);
	}
	}

static const std::unordered_map<Tag, const char*> token_syntax = {
	{Tag::Comma, ","},
	{Tag::LParen, "("},
	{Tag::RParen, ")"},
	{Tag::LBrace, "{"},
	{Tag::RBrace, "}"},
	{Tag::LBracket, "["},
	{Tag::RBracket, "]"},
	{Tag::Colon, ":"},
	{Tag::Dollar, "$"},
	{Tag::Question, "?"},
	{Tag::Semi, ";"},
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
