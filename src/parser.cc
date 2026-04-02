#include "parser.h"

#include <cstdio>

// Attach pending pre-comments (and any held markers) to a node.
// If the node is a container, push down to its first concrete
// child so the formatter doesn't need special container logic.
static void AttachPreComments(std::vector<std::string>& pending,
                              Node::NodeVec& markers, Node& node)
	{
	if ( pending.empty() )
		return;

	// Push through transparent containers (e.g. BODY) to
	// the first concrete child inside them.
	Tag nt = node.GetTag();
	if ( nt == Tag::Body && node.HasChildren() )
		{
		for ( auto& c : node.Children() )
			{
			Tag t = c->GetTag();
			if ( t != Tag::Blank && t != Tag::Semi &&
			     t != Tag::TrailingComma &&
			     t != Tag::LBrace && t != Tag::RBrace )
				{
				AttachPreComments(pending, markers, *c);
				return;
				}
			}
		}

	for ( auto& m : markers )
		node.AddPreMarker(std::move(m));
	markers.clear();

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
	Node::NodeVec pending_nodes;

	SkipWhitespace();

	while ( ! AtEnd() )
		{
		auto node = ParseNode();
		if ( ! node )
			return {};  // error already reported

		Tag t = node->GetTag();

		// Attach COMMENT-TRAILING to the preceding node.
		if ( t == Tag::CommentTrailing && ! nodes.empty() )
			nodes.back()->SetTrailingComment(node->Arg());

		// Save COMMENT-LEADING for the next node.
		else if ( t == Tag::CommentLeading )
			pending_pre.push_back(node->Arg());

		// When pre-comments are pending, hold BLANK/SEMI/
		// TrailingComma so they don't separate the comments
		// from the node they belong to.
		else if ( ! pending_pre.empty() &&
		          (t == Tag::Blank || t == Tag::Semi ||
		           t == Tag::TrailingComma) )
			pending_nodes.push_back(std::move(node));

		else
			{
			AttachPreComments(pending_pre, pending_nodes, *node);
			nodes.push_back(std::move(node));
			}

		SkipWhitespace();
		}

	// Flush any held markers and trailing comments that
	// had no following node.
	for ( auto& pn : pending_nodes )
		nodes.push_back(std::move(pn));

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
		Node::NodeVec pending_children;

		while ( ! AtEnd() && Peek() != '}' )
			{
			auto child = ParseNode();
			if ( ! child )
				return nullptr;

			Tag ct = child->GetTag();

			// Attach COMMENT-TRAILING to preceding child.
			if ( ct == Tag::CommentTrailing
			     && node->HasChildren() )
				node->Children().back()->SetTrailingComment(
								child->Arg());

			// Save COMMENT-LEADING for the next child.
			else if ( ct == Tag::CommentLeading )
				pending_pre.push_back(child->Arg());

			// When pre-comments are pending, hold markers
			// so they don't separate comments from their
			// target node.
			else if ( ! pending_pre.empty() &&
			          (ct == Tag::Blank || ct == Tag::Semi ||
			           ct == Tag::TrailingComma) )
				pending_children.push_back(std::move(child));

			else
				{
				AttachPreComments(pending_pre, pending_children, *child);
				node->AddChild(std::move(child));
				}

			SkipWhitespace();
			}

		// Flush any held markers and trailing comments.
		for ( auto& pc : pending_children )
			node->AddChild(std::move(pc));

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
