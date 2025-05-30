##########################################
# Address
##########################################

const foo = {[2620:83:8000:140::3], [2620:83:8000:140::2], 127.0.0.1};

##########################################
# Subnet
##########################################

global a = 1.2.3.4  /  19;
global b = [2620:83:8000:140::3] /21;

##########################################
# Enum
##########################################

# We always break lines for enum labels.
type single_element_enum: enum { ZERO };
type multi_element_enum: enum { ZERO, ONE, TWO };
type enum_with_deprecated: enum { ZERO, ONE &deprecated, TWO &deprecated, };

# Enum with zeekygen comments.
type An::ID: enum {
  ## A Zeekygen comment
    ENUM_VAL1, ##< A Zeekygen post-comment
      ##< that continues on the next line 
    ## Another Zeekygen comment
    PRINTLOG
};

# Enum with assignments.
type AssigedEnum: enum {
  FOO=1,
  BAR=10,
};

# Enum with an empty minor comment (regression test for zeek/tree-sitter-zeek#9):
#
type WithEmptyCommentBefore: enum {
  FOO=1,
  BAR=10,
};

##########################################
# Record
##########################################

# We always break lines for record fields.
type empty_rec: record {};
type single_rec: record { one: count;};
type rec_with_optional: record { one: count; two: count &optional; };
