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

// Keyword statements with expression list:
//   return [expr], add expr, delete expr, assert expr[, msg],
//   print expr, ...

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


// Export declaration: export { decls }

class ExportNode : public StmtNode {
public:
	ExportNode() : StmtNode(Tag::ExportDecl) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Type declarations: type name : alias/enum/record ;

class TypeDeclAliasNode : public StmtNode {
public:
	TypeDeclAliasNode() : StmtNode(Tag::TypeDeclAlias) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Braced type declarations (enum, record): shared head/close framing,
// virtual FormatBody for the inner content.
class TypeDeclBracedNode : public StmtNode {
public:
	TypeDeclBracedNode(Tag t) : StmtNode(t) { }
	Candidates Format(const FmtContext& ctx) const override;
protected:
	virtual std::string FormatBody(const Node* inner,
	                               const FmtContext& ctx) const = 0;
};

class TypeDeclEnumNode : public TypeDeclBracedNode {
public:
	TypeDeclEnumNode() : TypeDeclBracedNode(Tag::TypeDeclEnum) { }
protected:
	std::string FormatBody(const Node* inner,
	                        const FmtContext& ctx) const override;
};

class TypeDeclRecordNode : public TypeDeclBracedNode {
public:
	TypeDeclRecordNode() : TypeDeclBracedNode(Tag::TypeDeclRecord) { }
protected:
	std::string FormatBody(const Node* inner,
	                        const FmtContext& ctx) const override;
};

// Preprocessor directives.  Not StmtNodes (no Candidates), but
// provide FormatText() for FormatStmtList and depth-control queries.

class PreprocBaseNode : public Node {
public:
	PreprocBaseNode(Tag t) : Node(t) { }
	virtual std::string FormatText() const = 0;
	virtual bool OpensDepth() const = 0;
	virtual bool ClosesDepth() const = 0;
	virtual bool AtColumnZero() const = 0;
};

class PreprocNode : public PreprocBaseNode {
public:
	PreprocNode() : PreprocBaseNode(Tag::Preproc) { }
	std::string FormatText() const override;
	bool OpensDepth() const override;
	bool ClosesDepth() const override;
	bool AtColumnZero() const override;
};

class PreprocCondNode : public PreprocBaseNode {
public:
	PreprocCondNode() : PreprocBaseNode(Tag::PreprocCond) { }
	std::string FormatText() const override;
	bool OpensDepth() const override { return true; }
	bool ClosesDepth() const override { return false; }
	bool AtColumnZero() const override { return true; }
};

// Switch statement: switch expr { case val: body ... }

class SwitchNode : public StmtNode {
public:
	SwitchNode() : StmtNode(Tag::Switch) { }
	Candidates Format(const FmtContext& ctx) const override;
};
