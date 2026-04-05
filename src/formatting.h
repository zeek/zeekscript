#pragma once

#include <string>

// A string of formatted output being assembled.  Currently wraps
// std::string; exists as a distinct type so we can later change the
// internal representation (e.g. rope, tracked columns) without
// touching every call site.
class Formatting {
public:
	Formatting() = default;
	Formatting(const std::string& s) : text(s) {}
	Formatting(const char* s) : text(s) {}
	Formatting(std::string&& s) : text(std::move(s)) {}

	Formatting& operator+=(const Formatting& o)
		{ text += o.text; return *this; }
	Formatting& operator+=(const std::string& s)
		{ text += s; return *this; }
	Formatting& operator+=(const char* s)
		{ text += s; return *this; }

	Formatting operator+(const Formatting& o) const
		{ return {text + o.text}; }
	Formatting operator+(const std::string& s) const
		{ return {text + s}; }
	Formatting operator+(const char* s) const
		{ return {text + s}; }

	// Access to the underlying string.
	const std::string& Str() const { return text; }

	bool empty() const { return text.empty(); }
	size_t size() const { return text.size(); }
	char back() const { return text.back(); }
	void pop_back() { text.pop_back(); }

	Formatting substr(size_t pos, size_t len = std::string::npos) const
		{ return {text.substr(pos, len)}; }

private:
	std::string text;
};

// Allow "string" + Formatting and std::string + Formatting.
inline Formatting operator+(const std::string& lhs, const Formatting& rhs)
	{ return Formatting(lhs) += rhs; }
inline Formatting operator+(const char* lhs, const Formatting& rhs)
	{ return Formatting(lhs) += rhs; }
