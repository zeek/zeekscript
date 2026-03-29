#include "node.h"
#include "parser.h"

#include <cstdio>
#include <cstdlib>
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
	std::string input;

	if ( argc > 1 && std::string(argv[1]) != "-" )
		input = ReadFile(argv[1]);
	else
		input = ReadStdin();

	auto nodes = Parser::Parse(input);

	if ( nodes.empty() && ! input.empty() )
		{
		fprintf(stderr, "parse failed\n");
		return 1;
		}

	// For now, dump the tree to verify round-tripping.
	for ( const auto& node : nodes )
		node->Dump();

	return 0;
	}
