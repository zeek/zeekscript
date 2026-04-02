#include "parser.h"

#include <cstdio>

// Attach pending pre-comments to a node and clear the vector.
static void AttachPreComments(std::vector<std::string>& pending,
                              Node& node)
	{
	for ( auto& c : pending )
		node.AddPreComment(std::move(c));
	pending.clear();
	}

Node::NodeVec Parser::Parse(const std::string& input)
	{
	Parser p(input);
	return p.ParseFile();
	}

Node::NodeVec Parser::ParseFile()
	{
	Node::NodeVec nodes;
	std::vector<std::string> pending_pre;

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

		// Save COMMENT-LEADING for the next node.
		else if ( node->GetTag() == Tag::CommentLeading )
			pending_pre.push_back(node->Arg());

		else
			{
			AttachPreComments(pending_pre, *node);
			nodes.push_back(std::move(node));
			}

		SkipWhitespace();
		}

	// Flush any trailing COMMENT-LEADING that had no
	// following node - keep as standalone nodes.
	for ( auto& c : pending_pre )
		{
		auto cn = MakeNode(Tag::CommentLeading);
		cn->AddArg(std::move(c));
		nodes.push_back(std::move(cn));
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

		std::vector<std::string> pending_pre;

		while ( ! AtEnd() && Peek() != '}' )
			{
			auto child = ParseNode();
			if ( ! child )
				return nullptr;

			// Attach COMMENT-TRAILING to preceding child.
			if ( child->GetTag() == Tag::CommentTrailing
			     && node->HasChildren() )
				node->Children().back()->SetTrailingComment(
								child->Arg());

			// Save COMMENT-LEADING for the next child.
			else if ( child->GetTag() == Tag::CommentLeading )
				pending_pre.push_back(child->Arg());

			else
				{
				AttachPreComments(pending_pre, *child);
				node->AddChild(std::move(child));
				}

			SkipWhitespace();
			}

		// Flush any trailing COMMENT-LEADING that had no
		// following sibling - keep as standalone children.
		for ( auto& c : pending_pre )
			{
			auto cn = MakeNode(Tag::CommentLeading);
			cn->AddArg(std::move(c));
			node->AddChild(std::move(cn));
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
