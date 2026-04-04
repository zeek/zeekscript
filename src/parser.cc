#include "parser.h"

#include <cstdio>

// Attach pending pre-comments (and any held markers) to a node.
// If the node is a container, push down to its first concrete
// child so the formatter doesn't need special container logic.
static void AttachPreComments(std::vector<std::string>& pending,
                              NodeVec& markers, Node& node)
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
			if ( ! c->IsMarker() &&
			     c->GetTag() != Tag::LBrace &&
			     c->GetTag() != Tag::RBrace )
				{
				AttachPreComments(pending, markers, *c);
				return;
				}
			}
		}

	for ( auto& m : markers )
		node.AddPreMarker(std::move(m));
	for ( auto& c : pending )
		node.AddPreComment(std::move(c));

	markers.clear();
	pending.clear();
	}

NodeVec Parser::Parse(const std::string& input)
	{
	Parser p(input);
	return p.ParseFile();
	}

NodeVec Parser::ParseFile()
	{
	NodeVec nodes;
	std::vector<std::string> pending_pre;
	NodeVec pending_nodes;

	SkipWhitespace();

	while ( ! AtEnd() )
		{
		auto node = ParseNode();
		if ( ! node )
			return {};  // error already reported

		Tag t = node->GetTag();

		if ( t == Tag::CommentTrailing && ! nodes.empty() )
			// Attach COMMENT-TRAILING to the preceding node.
			nodes.back()->SetTrailingComment(node->Arg());

		else if ( t == Tag::CommentLeading )
			{
			// Merge a preceding standalone BLANK into
			// the comment as a leading '\n'.
			std::string text = node->Arg();
			if ( ! nodes.empty() &&
			     nodes.back()->GetTag() == Tag::Blank )
				{
				nodes.pop_back();
				text = "\n" + text;
				}
			pending_pre.push_back(std::move(text));
			}

		else if ( ! pending_pre.empty() && node->IsMarker() )
			{
			// Merge a BLANK between comments and their
			// target into the last comment as a trailing '\n'.
			if ( t == Tag::Blank )
				pending_pre.back() += "\n";
			else
				pending_nodes.push_back(std::move(node));
			}

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
		node->AddArg(ParseQuotedString());
		SkipWhitespace();
		}

	// Optionally parse a { children } block.
	if ( AtEnd() || Peek() != '{' )
		return node;

	node->SetHasBlock();
	Advance();  // consume '{'
	SkipWhitespace();

	std::vector<std::string> pending_pre;
	NodeVec pending_children;

	while ( ! AtEnd() && Peek() != /* { to balance */ '}' )
		{
		auto child = ParseNode();
		if ( ! child )
			return nullptr;

		Tag ct = child->GetTag();

		// Attach COMMENT-TRAILING to preceding child.
		if ( ct == Tag::CommentTrailing && node->HasChildren() )
			node->Children().back()->SetTrailingComment(child->Arg());

		// Save COMMENT-LEADING for the next child.
		else if ( ct == Tag::CommentLeading )
			{
			std::string text = child->Arg();
			if ( node->HasChildren() &&
			     node->Children().back()->GetTag() == Tag::Blank )
				{
				node->Children().pop_back();
				text = "\n" + text;
				}
			pending_pre.push_back(std::move(text));
			}

		// When pre-comments are pending, hold markers so they don't
		// separate comments from their target node.
		else if ( ! pending_pre.empty() && child->IsMarker() )
			{
			if ( ct == Tag::Blank )
				pending_pre.back() += "\n";
			else
				pending_children.push_back(std::move(child));
			}

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
		Error("unexpected end of input, expected '}'");
		return nullptr;
		}

	Advance();  // consume '}'

	return node;
	}

std::string Parser::ParseTag()
	{
	size_t start = pos;

	while ( ! AtEnd() )
		{
		char c = Peek();
		if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
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
		if ( Peek() != '\\' )
			{
			result += Peek();
			Advance();
			continue;
			}

		Advance();  // consume backslash
		if ( AtEnd() )
			{
			Error("unexpected end of input in string escape");
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
			result += '\\' + c;
			break;
		}

		Advance();
		}

	if ( AtEnd() )
		{
		Error("unexpected end of input, expected closing '\"'");
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
		if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' )
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
	fprintf(stderr, "parse error at line %d, col %d: %s\n",
	        line, col, msg);
	}
