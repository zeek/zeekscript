#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Formatting;
class Layout;
using LayoutPtr = std::shared_ptr<Layout>;
using LayoutVec = std::vector<LayoutPtr>;

// A piece of formatted output: a borrowed view (for string literals),
// an owned string, a shared reference to another Formatting, or a
// lazy node reference (text materialized on first access).
class FmtPiece {
public:
	FmtPiece(std::string_view sv) : data(sv) {}
	FmtPiece(std::string s) : data(std::move(s)) {}
	FmtPiece(std::shared_ptr<Formatting> f) : data(std::move(f)) {}
	FmtPiece(LayoutPtr n) : data(std::move(n)) {}

	// Defined in formatting.cc (needs complete Formatting type).
	size_t Size() const;
	int Find(char c) const;
	void AppendTo(std::string& out) const;
	void PopBack();
	int CountNewlines() const;
	int AfterLastNewline() const;

private:
	friend class Formatting;

	const std::string& NodeText() const;
	std::string_view TextView() const;
	void AccumOverflow(int& col, int max_col, int& ovf) const;
	void AccumMaxOverflow(int& col, int max_col, int& max_ovf) const;

	std::variant<std::string_view, std::string,
	             std::shared_ptr<Formatting>, LayoutPtr> data;
	mutable std::string node_cache;
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

	// Construct from a leaf node (token or atom).  Asserts the
	// node is not compound.
	Formatting(const LayoutPtr& n);

	Formatting& operator+=(const Formatting& o);
	Formatting& operator+=(Formatting&& o);
	Formatting& operator+=(const std::shared_ptr<Formatting>& p);
	Formatting& operator+=(const LayoutPtr& n);
	Formatting& operator+=(const std::string& s);
	Formatting& operator+=(const char* s);

	Formatting operator+(const Formatting& o) const
		{ Formatting r(*this); r += o; return r; }
	Formatting operator+(const std::string& s) const
		{ Formatting r(*this); r += s; return r; }
	Formatting operator+(const char* s) const
		{ Formatting r(*this); r += s; return r; }
	Formatting operator+(const std::shared_ptr<Formatting>& p) const
		{ Formatting r(*this); r += p; return r; }
	Formatting operator+(const LayoutPtr& n) const
		{ Formatting r(*this); r += n; return r; }

	// Materialize the cord into a single string.
	const std::string& Str() const;

	bool Empty() const { return total == 0; }
	int Size() const { return static_cast<int>(total); }
	char Back() const { return Str().back(); }

	// Search for a character in the materialized string.
	// Returns position as int, or -1 if not found.
	int Find(char c) const;
	bool Contains(char c) const { return Find(c) >= 0; }

	// Multi-line text metrics (walk pieces directly).
	int LastLineLen() const;
	int CountLines() const;
	int TextOverflow(int start_col, int max_col) const;
	int MaxLineOverflow(int start_col, int max_col) const;

	void PopBack();
	Formatting Substr(size_t pos, size_t len = std::string::npos) const;

private:
	friend class FmtPiece;

	void AccumOverflow(int& col, int max_col, int& ovf) const;
	void AccumMaxOverflow(int& col, int max_col, int& max_ovf) const;

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

// Allow mixing LayoutPtr in concatenation chains.
inline Formatting operator+(const std::string& lhs, const LayoutPtr& rhs)
	{ Formatting r(lhs); r += rhs; return r; }
inline Formatting operator+(const char* lhs, const LayoutPtr& rhs)
	{ Formatting r(lhs); r += rhs; return r; }

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

