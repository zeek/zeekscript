#include "parser.h"

#include <cstdio>

Node::NodeVec Parser::Parse(const std::string& input)
	{
	Parser p(input);
	return p.ParseFile();
	}

Node::NodeVec Parser::ParseFile()
	{
	Node::NodeVec nodes;

	SkipWhitespace();

	while ( ! AtEnd() )
		{
		auto node = ParseNode();
		if ( ! node )
			return {};  // error already reported

		// Attach COMMENT-TRAILING to the preceding node.
		if ( node->GetTag() == Tag::CommentTrailing &&
		     ! nodes.empty() )
			nodes.back()->SetTrailingComment(node->Arg());
		else
			nodes.push_back(std::move(node));

		SkipWhitespace();
		}

	return nodes;
	}

std::shared_ptr<Node> Parser::ParseNode()
	{
	SkipWhitespace();

	if ( AtEnd() )
		{
		Error("unexpected end of input, expected node");
		return nullptr;
		}

	std::string t = ParseTag();
	if ( t.empty() )
		{
		Error("expected tag");
		return nullptr;
		}

	auto node = MakeNode(TagFromString(t));

	// Parse zero or more quoted-string arguments.
	SkipWhitespace();

	while ( ! AtEnd() && Peek() == '"' )
		{
		std::string arg = ParseQuotedString();
		node->AddArg(std::move(arg));
		SkipWhitespace();
		}

	// Optionally parse a { children } block.
	if ( ! AtEnd() && Peek() == '{' )
		{
		node->SetHasBlock();
		Advance();  // consume '{'
		SkipWhitespace();

		while ( ! AtEnd() && Peek() != '}' )
			{
			auto child = ParseNode();
			if ( ! child )
				return nullptr;

			// Attach COMMENT-TRAILING to the preceding child node.
			if ( child->GetTag() == Tag::CommentTrailing
			     && node->HasChildren() )
				node->Children().back()->SetTrailingComment(
								child->Arg());
			else
				node->AddChild(std::move(child));

			SkipWhitespace();
			}

		if ( AtEnd() )
			{
			Error("unexpected end of input, "
			      "expected '}'");
			return nullptr;
			}

		Advance();  // consume '}'
		}

	return node;
	}

std::string Parser::ParseTag()
	{
	size_t start = pos;

	while ( ! AtEnd() )
		{
		char c = Peek();
		if ( c == ' ' || c == '\t' ||
		     c == '\n' || c == '\r' ||
		     c == '"' || c == '{' || c == '}' )
			break;
		Advance();
		}

	return input.substr(start, pos - start);
	}

std::string Parser::ParseQuotedString()
	{
	if ( AtEnd() || Peek() != '"' )
		{
		Error("expected '\"'");
		return {};
		}

	Advance();  // consume opening '"'
	std::string result;

	while ( ! AtEnd() && Peek() != '"' )
		{
		if ( Peek() == '\\' )
			{
			Advance();  // consume backslash
			if ( AtEnd() )
				{
				Error("unexpected end of "
				      "input in string escape");
				return {};
				}

			char c = Peek();
			switch ( c ) {
			case '"': result += '"'; break;
			case '\\': result += '\\'; break;
			case 'n': result += '\n'; break;
			case 't': result += '\t'; break;
			case 'r': result += '\r'; break;
			case '/': result += '/'; break;
			default:
				// Keep backslash for unknown escapes.
				result += '\\';
				result += c;
				break;
			}

			Advance();
			}
		else
			{
			result += Peek();
			Advance();
			}
		}

	if ( AtEnd() )
		{
		Error("unexpected end of input, "
		      "expected closing '\"'");
		return {};
		}

	Advance();  // consume closing '"'
	return result;
	}

void Parser::SkipWhitespace()
	{
	while ( ! AtEnd() )
		{
		char c = Peek();
		if ( c == ' ' || c == '\t' ||
		     c == '\n' || c == '\r' )
			Advance();
		else
			break;
		}
	}

char Parser::Advance()
	{
	char c = input[pos++];

	if ( c == '\n' )
		{
		++line;
		col = 1;
		}
	else
		++col;

	return c;
	}

void Parser::Error(const char* msg) const
	{
	fprintf(stderr,
	        "parse error at line %d, col %d: %s\n",
	        line, col, msg);
	}
