#include "formatting.h"
#include "node.h"

#include <cassert>

Formatting::Formatting(const NodePtr& n)
	{
	assert(n);
	assert(n->IsToken() || ! n->Args().empty());
	auto t = n->Text();
	total = t.size();
	dirty = total > 0;
	if ( total > 0 )
		pieces.emplace_back(std::move(t));
	}

// FmtPiece methods (need complete Formatting type).

size_t FmtPiece::Size() const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		return sv->size();
	if ( auto* s = std::get_if<std::string>(&data) )
		return s->size();
	return std::get<std::shared_ptr<Formatting>>(data)->Size();
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

	return std::get<std::shared_ptr<Formatting>>(data)->Find(c);
	}

void FmtPiece::AppendTo(std::string& out) const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		out += *sv;
	else if ( auto* s = std::get_if<std::string>(&data) )
		out += *s;
	else
		out += std::get<std::shared_ptr<Formatting>>(data)->Str();
	}

void FmtPiece::PopBack()
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		sv->remove_suffix(1);
	else if ( auto* s = std::get_if<std::string>(&data) )
		s->pop_back();
	else
		{
		// Materialize the shared Formatting and replace
		// with an owned string.
		auto& fp = std::get<std::shared_ptr<Formatting>>(data);
		std::string materialized = fp->Str();
		materialized.pop_back();
		data = std::move(materialized);
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
