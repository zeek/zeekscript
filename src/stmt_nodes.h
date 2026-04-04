#pragma once

#include "formatter.h"
#include "node.h"

// Base class for statement nodes with virtual Format.

class StmtNode : public Node {
public:
	StmtNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Standalone comment at statement level.

class CommentNode : public StmtNode {
public:
	CommentNode() : StmtNode(Tag::CommentLeading) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Expression statement: expr ;

class ExprStmtNode : public StmtNode {
public:
	ExprStmtNode() : StmtNode(Tag::ExprStmt) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Keyword statements: return [expr], add expr, delete expr, assert

class KeywordStmtNode : public StmtNode {
public:
	KeywordStmtNode(Tag t) : StmtNode(t) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Event statement: event name(args);

class EventStmtNode : public StmtNode {
public:
	EventStmtNode() : StmtNode(Tag::EventStmt) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Print statement: print expr, expr, ...

class PrintNode : public StmtNode {
public:
	PrintNode() : StmtNode(Tag::Print) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Export declaration: export { decls }

class ExportNode : public StmtNode {
public:
	ExportNode() : StmtNode(Tag::ExportDecl) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Switch statement: switch expr { case val: body ... }

class SwitchNode : public StmtNode {
public:
	SwitchNode() : StmtNode(Tag::Switch) { }
	Candidates Format(const FmtContext& ctx) const override;
};
