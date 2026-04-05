#include "formatting.h"

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
	auto pos = Str().find(c);
	return pos == std::string::npos ? -1 : static_cast<int>(pos);
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

// FmtPiece methods (need complete Formatting type).

size_t FmtPiece::Size() const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		return sv->size();
	if ( auto* s = std::get_if<std::string>(&data) )
		return s->size();
	return std::get<std::shared_ptr<Formatting>>(data)->Size();
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
