#include "node.h"

#include <cstdio>
#include <unordered_map>

const std::string& Node::Arg(size_t i) const
	{
	static const std::string empty;
	return i < args.size() ? args[i] : empty;
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
