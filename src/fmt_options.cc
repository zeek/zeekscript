#include <cstdio>
#include <cstdlib>

#include "fmt_options.h"

FmtOptions fmt_options;

void FmtOptions::ScanDirective(const std::string& comment)
	{
	static const std::string tag = "#@ FORMAT:";

	// Strip leading blank-line markers ('\n') that the parser
	// prepends for blank lines before comments.
	auto pos = comment.find(tag);
	if ( pos == std::string::npos )
		return;

	auto rest = comment.substr(pos + tag.size());

	// Trim leading whitespace.
	auto start = rest.find_first_not_of(" \t");
	if ( start == std::string::npos )
		return;

	rest = rest.substr(start);

	if ( rest == "FILL-ENUM" )
		fill_enum = true;
	else if ( rest == "FILL-SET" )
		fill_set = true;
	else
		{
		fprintf(stderr, "unknown format directive: %s %s\n",
			tag.c_str(), rest.c_str());
		exit(1);
		}
	}
