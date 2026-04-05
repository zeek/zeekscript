#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Formatting;

// A piece of formatted output: a borrowed view (for string literals),
// an owned string, or a shared reference to another Formatting.
class FmtPiece {
public:
	FmtPiece(std::string_view sv) : data(sv) {}
	FmtPiece(std::string s) : data(std::move(s)) {}
	FmtPiece(std::shared_ptr<Formatting> f) : data(std::move(f)) {}

	// Defined after Formatting (needs complete type).
	size_t size() const;
	void append_to(std::string& out) const;
	void pop_back();

private:
	std::variant<std::string_view, std::string,
	             std::shared_ptr<Formatting>> data;
};

// A string of formatted output being assembled.  Internally a
// segmented cord: append is O(1), and string literals are stored
// as views (zero-copy).  Appending a Formatting stores a shared
// reference rather than copying pieces.  Call Str() to materialize
// the final string.
class Formatting {
public:
	Formatting() = default;

	Formatting(const char* s)
		: total(std::string_view(s).size()), dirty(total > 0)
		{
		if ( total > 0 )
			pieces.emplace_back(std::string_view(s));
		}

	Formatting(const std::string& s)
		: total(s.size()), dirty(total > 0)
		{
		if ( total > 0 )
			pieces.emplace_back(s);
		}

	Formatting(std::string&& s)
		: total(s.size()), dirty(total > 0)
		{
		if ( total > 0 )
			pieces.emplace_back(std::move(s));
		}

	Formatting& operator+=(const Formatting& o)
		{
		pieces.emplace_back(std::make_shared<Formatting>(o));
		total += o.total;
		dirty = true;
		return *this;
		}

	Formatting& operator+=(Formatting&& o)
		{
		auto n = o.total;
		pieces.emplace_back(
			std::make_shared<Formatting>(std::move(o)));
		total += n;
		dirty = true;
		return *this;
		}

	Formatting& operator+=(const std::shared_ptr<Formatting>& p)
		{
		pieces.emplace_back(p);
		total += p->total;
		dirty = true;
		return *this;
		}

	Formatting& operator+=(const std::string& s)
		{
		pieces.emplace_back(s);
		total += s.size();
		dirty = true;
		return *this;
		}

	Formatting& operator+=(const char* s)
		{
		std::string_view sv(s);
		pieces.emplace_back(sv);
		total += sv.size();
		dirty = true;
		return *this;
		}

	Formatting operator+(const Formatting& o) const
		{ Formatting r(*this); r += o; return r; }
	Formatting operator+(const std::string& s) const
		{ Formatting r(*this); r += s; return r; }
	Formatting operator+(const char* s) const
		{ Formatting r(*this); r += s; return r; }
	Formatting operator+(const std::shared_ptr<Formatting>& p) const
		{ Formatting r(*this); r += p; return r; }

	// Materialize the cord into a single string.
	const std::string& Str() const
		{
		if ( dirty )
			{
			cache.clear();
			cache.reserve(total);
			for ( const auto& p : pieces )
				p.append_to(cache);
			dirty = false;
			}

		return cache;
		}

	bool empty() const { return total == 0; }
	size_t size() const { return total; }
	int Size() const { return static_cast<int>(total); }
	char back() const { return Str().back(); }

	// Search for a character in the materialized string.
	// Returns position as int, or -1 if not found.
	int Find(char c) const
		{
		auto pos = Str().find(c);
		return pos == std::string::npos ? -1 : static_cast<int>(pos);
		}

	bool Contains(char c) const { return Find(c) >= 0; }

	void pop_back()
		{
		while ( ! pieces.empty() && pieces.back().size() == 0 )
			pieces.pop_back();

		if ( ! pieces.empty() )
			{
			pieces.back().pop_back();
			--total;
			dirty = true;
			}
		}

	Formatting substr(size_t pos, size_t len = std::string::npos) const
		{ return {Str().substr(pos, len)}; }

private:
	std::vector<FmtPiece> pieces;
	size_t total = 0;
	mutable std::string cache;
	mutable bool dirty = false;
};

// Allow "string" + Formatting and std::string + Formatting.
inline Formatting operator+(const std::string& lhs, const Formatting& rhs)
	{ return Formatting(lhs) += rhs; }
inline Formatting operator+(const char* lhs, const Formatting& rhs)
	{ return Formatting(lhs) += rhs; }

// Allow mixing shared_ptr<Formatting> in concatenation chains.
using FmtPtr = std::shared_ptr<Formatting>;

inline Formatting operator+(const std::string& lhs, const FmtPtr& rhs)
	{ Formatting r(lhs); r += rhs; return r; }
inline Formatting operator+(const char* lhs, const FmtPtr& rhs)
	{ Formatting r(lhs); r += rhs; return r; }
inline Formatting operator+(const FmtPtr& lhs, const std::string& rhs)
	{ Formatting r; r += lhs; r += rhs; return r; }
inline Formatting operator+(const FmtPtr& lhs, const char* rhs)
	{ Formatting r; r += lhs; r += rhs; return r; }

// Deferred FmtPiece method implementations (need complete Formatting).

inline size_t FmtPiece::size() const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		return sv->size();
	if ( auto* s = std::get_if<std::string>(&data) )
		return s->size();
	return std::get<std::shared_ptr<Formatting>>(data)->size();
	}

inline void FmtPiece::append_to(std::string& out) const
	{
	if ( auto* sv = std::get_if<std::string_view>(&data) )
		out += *sv;
	else if ( auto* s = std::get_if<std::string>(&data) )
		out += *s;
	else
		out += std::get<std::shared_ptr<Formatting>>(data)->Str();
	}

inline void FmtPiece::pop_back()
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
