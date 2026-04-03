#pragma once

#include <string>

// Semantic node tags — one per distinct node type in
// the .rep format.
enum class Tag {
	// Expressions
	BinaryOp,
	UnaryOp,
	Ternary,
	Paren,
	Call,
	Index,
	IndexLiteral,
	Slice,
	FieldAccess,
	FieldAssign,
	BraceInit,
	Constructor,
	Schedule,
	KeywordExpr,   // hook, copy
	Lambda,
	WhenLocal,

	// Expression atoms
	Identifier,
	Constant,
	Interval,

	// Types
	TypeAtom,
	TypeParameterized,
	TypeFunc,
	TypeRecord,
	TypeEnum,

	// Declarations
	GlobalDecl,
	LocalDecl,
	TypeDecl,
	FuncDecl,
	ExportDecl,
	ModuleDecl,
	RedefRecord,
	RedefEnum,

	// Statements
	ExprStmt,
	If,
	For,
	While,
	Switch,
	When,
	Return,
	Print,
	EventStmt,
	Add,
	Delete,
	Assert,
	Block,

	// Statement/declaration parts
	Args,
	Subscripts,
	Params,
	Param,
	Returns,
	Body,
	Else,
	Iterable,
	Vars,
	Captures,
	Field,
	EnumValue,
	Case,
	Default,
	Values,
	Timeout,
	Of,

	// Attributes
	Attr,
	AttrList,

	// Comments
	CommentLeading,
	CommentTrailing,

	// Syntactic tokens
	Comma,
	LParen,
	RParen,
	LBrace,
	RBrace,
	LBracket,
	RBracket,
	Colon,
	Keyword,
	Op,
	Assign,
	Dollar,
	Question,
	Sp,

	// Markers
	Semi,
	Blank,
	Raw,
	TrailingComma,

	// Preprocessor
	Preproc,

	// Fallback
	Token,
	Unknown,
	UnknownExpr,
};

// Convert a tag string from the .rep format (e.g. "BINARY-OP")
// to the corresponding enum value.  Returns Tag::Unknown for
// unrecognized strings.
Tag TagFromString(const std::string& s);

// Convert a Tag enum value back to its .rep string form.
const char* TagToString(Tag t);

inline bool is_comment(Tag t)
	{
	return t == Tag::CommentLeading || t == Tag::CommentTrailing;
	}

inline bool is_type_tag(Tag t)
	{
	return t == Tag::TypeAtom || t == Tag::TypeParameterized ||
		t == Tag::TypeFunc;
	}

// Marker nodes: separators and whitespace that are not content.
inline bool is_marker(Tag t)
	{
	return t == Tag::Blank || t == Tag::Semi || t == Tag::TrailingComma;
	}

bool is_token(Tag t);
