#include "node.h"
#include "condition_block.h"

#include <cstdio>
#include <unordered_map>

const std::string& Node::Arg(size_t i) const
	{
	static const std::string empty;
	return i < args.size() ? args[i] : empty;
	}

const Node* Node::FindChild(Tag t) const
	{
	for ( const auto& c : children )
		if ( c->GetTag() == t )
			return c.get();
	return nullptr;
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

std::vector<const Node*> Node::ContentChildren() const
	{
	std::vector<const Node*> result;
	for ( const auto& c : children )
		{
		Tag t = c->GetTag();
		if ( ! is_token(t) && ! is_comment(t) && t != Tag::Semi &&
		     t != Tag::Blank && t != Tag::TrailingComma )
			result.push_back(c.get());
		}
	return result;
	}

std::shared_ptr<Node> MakeNode(Tag tag)
	{
	switch ( tag )
		{
		case Tag::If: return std::make_shared<IfNode>(tag);
		case Tag::For: return std::make_shared<ForNode>(tag);
		case Tag::While: return std::make_shared<WhileNode>(tag);
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
	{Tag::Question, "?"},
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

void Node::Dump(int indent) const
	{
	// Emit pre-comments as COMMENT-LEADING siblings before this node.
	for ( const auto& pc : pre_comments )
		{
		for ( int i = 0; i < indent; ++i )
			printf("  ");
		printf("COMMENT-LEADING ");
		PrintQuoted(pc);
		printf("\n");
		}

	for ( int i = 0; i < indent; ++i )
		printf("  ");

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
			for ( int i = 0; i < indent; ++i )
				printf("  ");
			printf("COMMENT-TRAILING ");
			PrintQuoted(trailing_comment.substr(1));
			printf("\n");
			}

		return;
		}

	if ( children.empty() && trailing_comment.empty() )
		{
		printf(" {\n");
		for ( int i = 0; i < indent; ++i )
			printf("  ");
		printf("}\n");
		return;
		}

	printf(" {\n");

	for ( const auto& child : children )
		child->Dump(indent + 1);

	for ( int i = 0; i < indent; ++i )
		printf("  ");
	printf("}\n");

	// Emit trailing comment as a sibling after the block.
	// Strip leading space added by SetTrailingComment.
	if ( ! trailing_comment.empty() )
		{
		for ( int i = 0; i < indent; ++i )
			printf("  ");
		printf("COMMENT-TRAILING ");
		PrintQuoted(trailing_comment.substr(1));
		printf("\n");
		}
	}
