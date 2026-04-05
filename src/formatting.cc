#include "formatting.h"
#include "layout.h"
#include "node.h"

#include <cassert>

Formatting::Formatting(const NodePtr& n)
	{
	assert(n);
	assert(n->IsToken() || ! n->Args().empty());
	pieces.emplace_back(n);
	total = pieces.back().Size();
	dirty = total > 0;
	}

// FmtPiece methods (need complete Formatting/Node types).

const std::string& FmtPiece::NodeText() const
	{
	if ( node_cache.empty() )
		node_cache = std::get<NodePtr>(data)->Text();
	return node_cache;
	}

size_t FmtPiece::Size() const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		return sv->size();
	if ( auto* s = std::get_if<std::string>(&data) )
		return s->size();
	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		return (*fp)->Size();
	return NodeText().size();
	}

int FmtPiece::Find(char c) const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		{
		auto pos = sv->find(c);
		return pos == std::string_view::npos
			? -1 : static_cast<int>(pos);
		}

	if ( auto* s = std::get_if<std::string>(&data) )
		{
		auto pos = s->find(c);
		return pos == std::string::npos
			? -1 : static_cast<int>(pos);
		}

	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		return (*fp)->Find(c);

	auto pos = NodeText().find(c);
	return pos == std::string::npos ? -1 : static_cast<int>(pos);
	}

void FmtPiece::AppendTo(std::string& out) const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		out += *sv;
	else if ( auto* s = std::get_if<std::string>(&data) )
		out += *s;
	else if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		out += (*fp)->Str();
	else
		out += NodeText();
	}

void FmtPiece::PopBack()
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		sv->remove_suffix(1);
	else if ( auto* s = std::get_if<std::string>(&data) )
		s->pop_back();
	else
		{
		// Materialize and replace with an owned string.
		std::string materialized;
		if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
			materialized = (*fp)->Str();
		else
			materialized = NodeText();
		materialized.pop_back();
		data = std::move(materialized);
		node_cache.clear();
		}
	}

// Formatting methods.

Formatting& Formatting::operator+=(const Formatting& o)
	{
	pieces.emplace_back(std::make_shared<Formatting>(o));
	total += o.total;
	dirty = true;
	return *this;
	}

Formatting& Formatting::operator+=(Formatting&& o)
	{
	auto n = o.total;
	pieces.emplace_back(
		std::make_shared<Formatting>(std::move(o)));
	total += n;
	dirty = true;
	return *this;
	}

Formatting& Formatting::operator+=(const std::shared_ptr<Formatting>& p)
	{
	pieces.emplace_back(p);
	total += p->total;
	dirty = true;
	return *this;
	}

Formatting& Formatting::operator+=(const NodePtr& n)
	{
	pieces.emplace_back(n);
	total += pieces.back().Size();
	dirty = true;
	return *this;
	}

Formatting& Formatting::operator+=(const std::string& s)
	{
	pieces.emplace_back(s);
	total += s.size();
	dirty = true;
	return *this;
	}

Formatting& Formatting::operator+=(const char* s)
	{
	std::string_view sv(s);
	pieces.emplace_back(sv);
	total += sv.size();
	dirty = true;
	return *this;
	}

const std::string& Formatting::Str() const
	{
	if ( dirty )
		{
		cache.clear();
		cache.reserve(total);
		for ( const auto& p : pieces )
			p.AppendTo(cache);
		dirty = false;
		}

	return cache;
	}

int Formatting::Find(char c) const
	{
	int offset = 0;
	for ( const auto& p : pieces )
		{
		int pos = p.Find(c);
		if ( pos >= 0 )
			return offset + pos;
		offset += static_cast<int>(p.Size());
		}
	return -1;
	}

void Formatting::PopBack()
	{
	while ( ! pieces.empty() && pieces.back().Size() == 0 )
		pieces.pop_back();

	if ( ! pieces.empty() )
		{
		pieces.back().PopBack();
		--total;
		dirty = true;
		}
	}

Formatting Formatting::Substr(size_t pos, size_t len) const
	{
	return {Str().substr(pos, len)};
	}

int Formatting::CountLines() const
	{
	const auto& s = Str();
	int n = 1;
	for ( char c : s )
		if ( c == '\n' )
			++n;
	return n;
	}

int Formatting::LastLineLen() const
	{
	const auto& s = Str();
	auto n = s.size();
	auto pos = s.rfind('\n');
	if ( pos != std::string::npos )
		n -= (pos + 1);
	return static_cast<int>(n);
	}

int Formatting::TextOverflow(int start_col, int max_col) const
	{
	const auto& s = Str();
	int ovf = 0;
	int pos = 0;
	int line_start_col = start_col;

	for ( size_t j = 0; j < s.size(); ++j )
		if ( s[j] == '\n' )
			{
			int line_w = static_cast<int>(j) - pos + line_start_col;
			if ( line_w > max_col )
				ovf += line_w - max_col;
			pos = static_cast<int>(j) + 1;
			line_start_col = 0;
			}

	int final_w = static_cast<int>(s.size()) - pos + line_start_col;
	if ( final_w > max_col )
		ovf += final_w - max_col;

	return ovf;
	}

int Formatting::MaxLineOverflow(int start_col, int max_col) const
	{
	const auto& s = Str();
	int max_ovf = 0;
	int col = start_col;

	for ( char c : s )
		{
		if ( c == '\n' )
			{
			int ovf = std::max(0, col - max_col);
			if ( ovf > max_ovf )
				max_ovf = ovf;
			col = 0;
			}
		else if ( c == '\t' )
			col = (col / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++col;
		}

	int ovf = std::max(0, col - max_col);
	if ( ovf > max_ovf )
		max_ovf = ovf;

	return max_ovf;
	}
