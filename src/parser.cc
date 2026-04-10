#include "parser.h"

#include <cstdio>

// Attach pending pre-comments (and any held markers) to a node.
// If the node is a container, push down to its first concrete
// child so the formatter doesn't need special container logic.
static void AttachPreComments(std::vector<std::string>& pending,
                              LayoutVec& markers, Layout& node)
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

// Find the deepest last leaf descendant of a node.
static Layout& last_leaf(Layout& node)
	{
	if ( ! node.HasChildren() )
		return node;

	return last_leaf(*node.Children().back());
	}

// Process a single parsed node, handling comment attachment and
// marker accumulation.  Returns true if the node was consumed
// internally (not added to 'out').
static bool process_comment(LayoutPtr& node, LayoutVec& out,
                            std::vector<std::string>& pending_pre,
                            LayoutVec& pending_markers)
	{
	Tag t = node->GetTag();

	if ( t == Tag::CommentTrailing && ! out.empty() )
		{
		auto& target = *out.back();

		if ( target.HasLayout() && target.HasChildren() )
			{
			last_leaf(target).SetTrailingComment(node->Arg());
			target.SetMustBreakAfter();
			}
		else
			target.SetTrailingComment(node->Arg());

		return true;
		}

	if ( t == Tag::CommentLeading )
		{
		std::string text = node->Arg();
		if ( ! out.empty() &&
		     out.back()->GetTag() == Tag::Blank )
			{
			out.pop_back();
			text = "\n" + text;
			}
		pending_pre.push_back(std::move(text));
		return true;
		}

	if ( ! pending_pre.empty() && node->IsMarker() )
		{
		if ( t == Tag::Blank )
			pending_pre.back() += "\n";
		else
			pending_markers.push_back(std::move(node));
		return true;
		}

	AttachPreComments(pending_pre, pending_markers, *node);
	return false;
	}

// Flush any held markers and trailing comments that had no
// following node.
static void flush_comments(LayoutVec& out,
                           std::vector<std::string>& pending_pre,
                           LayoutVec& pending_markers)
	{
	for ( auto& m : pending_markers )
		out.push_back(std::move(m));

	for ( auto& c : pending_pre )
		{
		auto cn = MakeNode(Tag::CommentLeading);
		cn->AddArg(std::move(c));
		cn->ComputeRender();
		out.push_back(std::move(cn));
		}
	}

std::pair<LayoutVec, bool> Parser::Parse(const std::string& input)
	{
	Parser p(input);
	return {p.ParseFile(), p.had_error};
	}

LayoutVec Parser::ParseFile()
	{
	LayoutVec nodes;
	std::vector<std::string> pending_pre;
	LayoutVec pending_nodes;

	SkipWhitespace();

	while ( ! AtEnd() )
		{
		auto node = ParseNode();
		if ( ! node )
			return {};  // error already reported

		if ( ! process_comment(node, nodes, pending_pre,
		                       pending_nodes) )
			nodes.push_back(std::move(node));

		SkipWhitespace();
		}

	flush_comments(nodes, pending_pre, pending_nodes);

	return nodes;
	}

LayoutPtr Parser::ParseNode()
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
		{
		node->ComputeRender();
		return node;
		}

	Advance();  // consume '{'
	SkipWhitespace();

	std::vector<std::string> pending_pre;
	LayoutVec pending_children;

	while ( ! AtEnd() && Peek() != /* { to balance */ '}' )
		{
		auto child = ParseNode();
		if ( ! child )
			return nullptr;

		if ( ! process_comment(child, node->Children(),
		                       pending_pre, pending_children) )
			node->AddChild(std::move(child));

		SkipWhitespace();
		}

	flush_comments(node->Children(), pending_pre,
	               pending_children);

	if ( AtEnd() )
		{
		Error("unexpected end of input, expected '}'");
		return nullptr;
		}

	Advance();  // consume '}'

	node->ComputeRender();
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

void Parser::Error(const char* msg)
	{
	fprintf(stderr, "parse error at line %d, col %d: %s\n",
	        line, col, msg);
	had_error = true;
	}
