#include "node.h"
#include "condition_block.h"

#include <cstdio>
#include <stdexcept>
#include <unordered_map>

const std::string& Node::Arg(size_t i) const
	{
	static const std::string empty;
	return i < args.size() ? args[i] : empty;
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
		{
		Tag t = c->GetTag();
		if ( ! is_token(t) && ! is_comment(t) && ! is_marker(t) )
			result.push_back(c.get());
		}

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
	case Tag::If: return std::make_shared<IfNode>();
	case Tag::For: return std::make_shared<ForNode>();
	case Tag::While: return std::make_shared<WhileNode>();
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

static void PrintQuoted(const std::string& s)
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
		PrintQuoted(pc);
		printf("\n");
		}

	for ( const auto& pm : pre_markers )
		pm->Dump(indent);

	do_indent(indent);

	printf("%s", TagToString(tag));

	for ( const auto& a : args )
		{
		putchar(' ');
		PrintQuoted(a);
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
			PrintQuoted(trailing_comment.substr(1));
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
		PrintQuoted(trailing_comment.substr(1));
		printf("\n");
		}
	}
