#include "formatting.h"
#include "fmt_context.h"
#include "layout.h"

#include <cassert>

Formatting::Formatting(const LayoutPtr& n)
	{
	assert(n);
	assert(! n->HasChildren());
	pieces.emplace_back(n);
	total = pieces.back().Size();
	dirty = total > 0;
	}

// FmtPiece methods (need complete Formatting/Layout types).

const std::string& FmtPiece::NodeText() const
	{
	if ( node_cache.empty() )
		node_cache = std::get<LayoutPtr>(data)->Text();
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
		return pos == std::string_view::npos ?
				-1 : static_cast<int>(pos);
		}

	if ( auto* s = std::get_if<std::string>(&data) )
		{
		auto pos = s->find(c);
		return pos == std::string::npos ? -1 : static_cast<int>(pos);
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

std::string_view FmtPiece::TextView() const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		return *sv;
	if ( auto* s = std::get_if<std::string>(&data) )
		return *s;

	return NodeText();
	}

int FmtPiece::CountNewlines() const
	{
	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		return (*fp)->CountLines() - 1;

	int count = 0;
	for ( char c : TextView() )
		if ( c == '\n' )
			++count;

	return count;
	}

int FmtPiece::AfterLastNewline() const
	{
	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		{
		int ll = (*fp)->LastLineLen();
		int sz = (*fp)->Size();
		return (ll == sz) ? -1 : ll;
		}

	auto sv = TextView();
	auto pos = sv.rfind('\n');
	return pos == std::string_view::npos ?
		-1 : static_cast<int>(sv.size() - pos - 1);
	}

void FmtPiece::AccumOverflow(int& col, int max_col, int& ovf) const
	{
	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		{
		(*fp)->AccumOverflow(col, max_col, ovf);
		return;
		}

	for ( char c : TextView() )
		if ( c == '\n' )
			{
			if ( col > max_col )
				ovf += col - max_col;
			col = 0;
			}
		else if ( c == '\t' )
			col = (col / INDENT_WIDTH + 1) * INDENT_WIDTH;
		else
			++col;
	}

void FmtPiece::AccumMaxOverflow(int& col, int max_col, int& max_ovf) const
	{
	if ( auto* fp = std::get_if<std::shared_ptr<Formatting>>(&data) )
		{
		(*fp)->AccumMaxOverflow(col, max_col, max_ovf);
		return;
		}

	for ( char c : TextView() )
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

Formatting& Formatting::operator+=(const LayoutPtr& n)
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
	int n = 0;
	for ( const auto& p : pieces )
		n += p.CountNewlines();
	return n + 1;
	}

int Formatting::LastLineLen() const
	{
	int len = 0;

	for ( int i = static_cast<int>(pieces.size()) - 1; i >= 0; --i )
		{
		int aln = pieces[i].AfterLastNewline();
		if ( aln >= 0 )
			return len + aln;
		len += static_cast<int>(pieces[i].Size());
		}

	return len;
	}

void Formatting::AccumOverflow(int& col, int max_col, int& ovf) const
	{
	for ( const auto& p : pieces )
		p.AccumOverflow(col, max_col, ovf);
	}

int Formatting::TextOverflow(int start_col, int max_col) const
	{
	int col = start_col;
	int ovf = 0;
	AccumOverflow(col, max_col, ovf);

	if ( col > max_col )
		ovf += col - max_col;

	return ovf;
	}

void Formatting::AccumMaxOverflow(int& col, int max_col,
                                  int& max_ovf) const
	{
	for ( const auto& p : pieces )
		p.AccumMaxOverflow(col, max_col, max_ovf);
	}

int Formatting::MaxLineOverflow(int start_col, int max_col) const
	{
	int col = start_col;
	int max_ovf = 0;
	AccumMaxOverflow(col, max_col, max_ovf);

	int ovf = std::max(0, col - max_col);
	if ( ovf > max_ovf )
		max_ovf = ovf;

	return max_ovf;
	}
