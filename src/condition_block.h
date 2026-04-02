#pragma once

#include "formatter.h"
#include "node.h"

// Base class for condition-block statements (if, for, while).
// Provides shared formatting: keyword ( condition ) body.

class ConditionBlockNode : public Node {
public:
	using Node::Node;

	Candidates Format(const FmtContext& ctx) const;

protected:
	// Returns formatted text for the condition between parens.
	// cond_ctx is positioned at the condition's start column
	// with space reserved for the closing " )".
	virtual std::string BuildCondition(const FmtContext& cond_ctx) const;

	// Returns any follow-on text (e.g., else clause for if).
	// comments holds formatted interstitial comments between
	// body and follow-on; has_blank indicates a preceding blank.
	virtual std::string BuildFollowOn(const FmtContext& /* ctx */,
		const std::string& /* comments */, bool /* has_blank */) const
		{ return ""; }
};

class IfNode : public ConditionBlockNode {
public:
	using ConditionBlockNode::ConditionBlockNode;

protected:
	std::string BuildFollowOn(const FmtContext& ctx,
		const std::string& comments, bool has_blank) const override;
};

class ForNode : public ConditionBlockNode {
public:
	using ConditionBlockNode::ConditionBlockNode;

protected:
	std::string BuildCondition(
		const FmtContext& cond_ctx) const override;
};

class WhileNode : public ConditionBlockNode {
public:
	using ConditionBlockNode::ConditionBlockNode;
};
