#pragma once

#include <string>

// Global formatting options set by #@ FORMAT: directives in comments.
// Each directive is a one-shot: it applies to the next applicable
// construct, then the formatter calls Consume() to clear it.
//
// Directives are detected by format_stmt_list when processing
// pre-comments, and consumed by the specific formatter that handles
// the targeted construct.

class FmtOptions {
public:
	bool FillEnum() const { return fill_enum; }
	bool FillSet() const { return fill_set; }

	// Mark a directive as consumed after formatting the construct
	// it targeted.
	void ConsumeFillEnum() { fill_enum = false; }
	void ConsumeFillSet() { fill_set = false; }

	// Scan a pre-comment for FORMAT directives and activate them.
	void ScanDirective(const std::string& comment);

private:
	bool fill_enum = false;
	bool fill_set = false;
};

// The single global instance, accessible to all formatters.
extern FmtOptions fmt_options;
