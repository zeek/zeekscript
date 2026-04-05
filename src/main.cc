#include "layout.h"
#include "node.h"
#include "parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string ReadFile(const char* path)
	{
	std::ifstream f(path);

	if ( ! f )
		{
		fprintf(stderr, "cannot open %s\n", path);
		exit(1);
		}

	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
	}

static std::string ReadStdin()
	{
	std::ostringstream ss;
	ss << std::cin.rdbuf();
	return ss.str();
	}

int main(int argc, char** argv)
	{
	bool dump_mode = false;
	const char* file = nullptr;

	for ( int i = 1; i < argc; ++i )
		{
		if ( strcmp(argv[i], "--dump") == 0 )
			dump_mode = true;
		else if ( strcmp(argv[i], "-") == 0 )
			file = nullptr;
		else
			file = argv[i];
		}

	std::string input;

	if ( file )
		input = ReadFile(file);
	else
		input = ReadStdin();

	auto nodes = Parser::Parse(input);

	if ( nodes.empty() && ! input.empty() )
		{
		fprintf(stderr, "parse failed\n");
		return 1;
		}

	try
		{
		if ( dump_mode )
			{
			for ( const auto& node : nodes )
				node->Dump();
			}
		else
			{
			std::string out = Format(nodes);
			printf("%s", out.c_str());
			}
		}
	catch ( const FormatError& e )
		{
		fprintf(stderr, "format error: %s\n", e.what());
		return 1;
		}

	return 0;
	}
