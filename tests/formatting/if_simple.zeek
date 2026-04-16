# Simple if/else: one-liners, inline if sequences, !in, negated conditions, empty bodies, else with comment.
# test034.zeek
# test056.zeek
# test057.zeek
# test109.zeek
# test110.zeek
# test111.zeek
# test196.zeek
# test210.zeek
# test226.zeek
# test268.zeek

function some_func()
	{
	if ( some_long_var_aa && some_var$some_field == "ok" &&
	     other?$some_chain && |other$some_chain| > 2 )
		{ }
	}

function some_func()
	{
	if ( ! aa?$bb_chain || |aa$bb_chain| == 0 ||
	     ! aa$bb_chain[0]?$cc )
		return dd;
	}

function some_func(a: int)
	{
	if ( some_very_long_access_to_a_member[foo] !in SomeModule::other$nested_field )
		{
		print "hi";
		}
	}

function some_func()
	{
	if ( x !in y )
		print "hi";
	}

# 3+ inline ifs: all fit, should inline.
function f(val: string): string
    {
    if ( /aaa/i in val ) return "aaa";
    if ( /bbb/i in val ) return "bbb";
    if ( /ccc/i in val ) return "ccc";
    if ( /ddd/i in val ) return "ddd";
    if ( /eee/ in val ) return "eee";

    return val;
    }

# Only 2 ifs: should NOT inline.
function g()
    {
    if ( x ) return "a";
    if ( y ) return "b";
    return "c";
    }

# 3 ifs but one too long: should NOT inline any.
function h()
    {
    if ( x ) return "a";
    if ( y ) return "b";
    if ( some_very_long_condition_that_pushes_past_the_limit ) return "this_long_value";
    }

# Sequence interrupted by non-if: 2 + 3.
function i()
    {
    if ( x ) return "a";
    if ( y ) return "b";
    print "mid";
    if ( a ) return "c";
    if ( b ) return "d";
    if ( c ) return "e";
    }

# if with comment before body: should NOT inline.
function j()
    {
    if ( x )
        # comment before body
        print "done";
    if ( y ) print "two";
    if ( z ) print "three";
    }

# 3+ wrapped ifs that fit: author chose wrapped, preserve it.
function k()
    {
    if ( x )
        return "a";
    if ( y )
        return "b";
    if ( z )
        return "c";
    }

# Author chose no-wrap, but one of them violates it.
function l()
    {
    if ( x ) return "a";
    if ( y ) return "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    if ( z ) return "c";
    }

# 3+ inline ifs with trailing comment: should still inline.
function f(val: string): string
    {
    if ( /aaa/i in val ) return "aaa";
    if ( /bbb/i in val ) return "bbb";
    if ( /ccc/i in val ) return "ccc"; # foo
    if ( /ddd/i in val ) return "ddd";
    if ( /eee/ in val ) return "eee";

    return val;
    }

function foo()
    {
    # A comment.
    if ( fi?$md5 ) fi_red$md5 = fi$md5;
    if ( fi?$sha1 ) fi_red$sha1 = fi$sha1;
    if ( fi?$sha256 ) fi_red$sha256 = fi$sha256;
    }

function foo()
	{
	if ( s0 < 0 || s1 > 0 || s2 < 0 || s3 > 0 || s4 < 0 )
		foo();
	}

if ( first_condition && second_condition && third_condition && some_object$long_field > another_object$other_field ) do_something();

if ( some_long_bool_variable_a && result$result_string == "ok" && result?$chain_certs && |result$chain_certs| > 2 ) print "x";
