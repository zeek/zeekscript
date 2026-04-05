#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

// A piece of formatted output: either a borrowed view (for string
// literals and other long-lived text) or an owned string.
class FmtPiece {
public:
	FmtPiece(std::string_view sv) : data(sv) {}
	FmtPiece(std::string s) : data(std::move(s)) {}

	std::string_view view() const
		{
		if ( auto* sv = std::get_if<std::string_view>(&data) )
			return *sv;
		return std::get<std::string>(data);
		}

	size_t size() const { return view().size(); }

	void pop_back()
		{
		if ( auto* sv = std::get_if<std::string_view>(&data) )
			sv->remove_suffix(1);
		else
			std::get<std::string>(data).pop_back();
		}

private:
	std::variant<std::string_view, std::string> data;
};

// A string of formatted output being assembled.  Internally a
// segmented cord: append is O(1), and string literals are stored
// as views (zero-copy).  Call Str() to materialize the final string.
class Formatting {
public:
	Formatting() = default;

	Formatting(const char* s)
		{
		std::string_view sv(s);

		if ( ! sv.empty() )
			{
			total = sv.size();
			pieces.emplace_back(sv);
			dirty = true;
			}
		}

	Formatting(const std::string& s)
		{
		if ( ! s.empty() )
			{
			total = s.size();
			pieces.emplace_back(s);
			dirty = true;
			}
		}

	Formatting(std::string&& s)
		{
		if ( ! s.empty() )
			{
			total = s.size();
			pieces.emplace_back(std::move(s));
			dirty = true;
			}
		}

	Formatting& operator+=(const Formatting& o)
		{
		if ( o.total > 0 )
			{
			pieces.insert(pieces.end(),
				o.pieces.begin(), o.pieces.end());
			total += o.total;
			dirty = true;
			}

		return *this;
		}

	Formatting& operator+=(const std::string& s)
		{
		if ( ! s.empty() )
			{
			pieces.emplace_back(s);
			total += s.size();
			dirty = true;
			}

		return *this;
		}

	Formatting& operator+=(const char* s)
		{
		std::string_view sv(s);
		if ( ! sv.empty() )
			{
			pieces.emplace_back(sv);
			total += sv.size();
			dirty = true;
			}

		return *this;
		}

	Formatting operator+(const Formatting& o) const
		{ Formatting r(*this); r += o; return r; }
	Formatting operator+(const std::string& s) const
		{ Formatting r(*this); r += s; return r; }
	Formatting operator+(const char* s) const
		{ Formatting r(*this); r += s; return r; }

	// Materialize the cord into a single string.
	const std::string& Str() const
		{
		if ( dirty )
			{
			cache.clear();
			cache.reserve(total);
			for ( const auto& p : pieces )
				cache += p.view();
			dirty = false;
			}

		return cache;
		}

	bool empty() const { return total == 0; }
	size_t size() const { return total; }
	char back() const { return Str().back(); }

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
