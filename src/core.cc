#include "fmt_util.h"

// Line prefix: tabs for indent, spaces for remaining offset
std::string line_prefix(int indent, int col)
	{
	std::string s(indent, '\t');
	int space_col = indent * INDENT_WIDTH;

	if ( col > space_col )
		s.append(col - space_col, ' ');

	return s;
	}

// Pre-comment / pre-marker emission
FmtPtr Node::EmitPreComments(const std::string& pad) const
	{
	auto result = std::make_shared<Formatting>();

	for ( const auto& pc : PreComments() )
		{
		// Leading '\n' = blank line before this comment.
		size_t start = 0;
		while ( start < pc.size() && pc[start] == '\n' )
			{
			*result += "\n";
			++start;
			}

		// The comment text itself.
		size_t end = pc.size();
		while ( end > start && pc[end - 1] == '\n' )
			--end;

		*result += pad + pc.substr(start, end - start) + "\n";

		// Trailing '\n' = blank line after this comment.
		for ( size_t j = end; j < pc.size(); ++j )
			*result += "\n";
		}

	for ( const auto& pm : PreMarkers() )
		if ( pm->GetTag() == Tag::Blank )
			*result += "\n";

	return result;
	}

