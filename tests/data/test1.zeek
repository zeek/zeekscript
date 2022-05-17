##! A test script with all kinds of formatting errors.
##!
##! This Zeekygen head comment has multiple lines with more detail  
##! about this module. It spans two lines.

@load foo/bar/baz.zeek    # A "preprocessor" line with comment
@load  blum/frub

@if(getenv("ZEEK_PORT") != "")
redef Broker::default_port =  to_port(getenv( "ZEEK_PORT"));
@endif

module  Test;  
	
  export {
	# A regular comment  
	type An::ID: enum {
	  ## A Zeekygen comment
		ENUM_VAL1, ##< A Zeekygen post-comment
		  ##< that continues on the next line 
		## Anoter Zeekygen comment
		PRINTLOG
	};

	# An enum, with assignments, on multiple lines, which should remain.
	type AssigedEnum: enum {
	  FOO=1,
	  BAR=10,
	};

	# Another one that we put on one line. That should also remain
	type SingeLineEnum: enum { FOO, BAR };
	
        ## A constant.
        const a_constant=T  &redef ;

        ## An option.
        option an_option: table[ string,count ] of string=table() &redef;

        ## A function.
	global a_function : function(foo: BAR) :bool;

	 ## A lambda.
	const a_lambda: function( foo: string ) = function (foo: string) {  
	} &redef;

}

# Another type of sequence where zeek-format considers existing linebreaks.
# This one should stay as-is...
const deltas1: vector of double = { 0.01, 0.01, 0.01, 0.01, 0.01, 0.01 };
# ... while this one gets fully line-broken:
const deltas2: vector of double = { 0.01, 0.02, # WHOA!
    0.01, 0.03, # DOUBLE whoa!
    0.01, 0.01 };

function a_function ( a: int, b: count, another_argument_for_linewrapping: string ) : a_long_boolean
	{
	if ( foo in bar )
		return somthing [ foo$bar ] (bar) ;
	else
		# A comment
		return T;

	# Mixed {}-blocks in these if-else statements:
	if ( foo ) {
		bar();
	} else baz();

	if ( !foo ) bar();
	else { baz(); }

	if ( a_long_var_a in a_long_var_b && ( c in d || e in f ) &&
		a_long_var_g in a_long_var_h )
		{
		return somthing [ foo$bar ] (bar) ;
		}
	else
		{
		# A comment
		return T;
		}

	# This shouldn't break the closing ")" onto a new line
	if ( another_very_long_access_to_some_member[foo] !in Some::other$nestedthing )
		{
		# Directives should neither get indented nor wrapped:
		@if ( VERY_LOOOOOOOOOOOOONG_FOO && VERY_LOOOOOOOOOOOOONG_BAR && VERY_LOOOOOOOOOOOOONG_BAZ )
		return T;
		@endif
		}

	# This shouldn't break the 0 index number onto a new line
	another_very_long_access_to_some_member += yetanotherveryveryveryverylongthing[0];

	if ( | foo | > 0 )
		print "foo";
	else if  (bar && baz) {
		print "bar";
	} else if ( baz)
		# This comment should not move. Also, the following should
		# _not_ wrap because the long string alone is too long for
		# the line limit.
		print fmt("%s", "Lovely patio around the fountain. Spent a lovely lunch on the patio.");
	else
		print "Lovely patio around the fountain. " + "Spent a lovely lunch on the patio. " + "The menu was inviting and lots of things I wanted to order. " + "Ordered the Eutropia pizza thin crust-YUM! " + "Will go back the next time I'm in Berkeley.";

	when [x]((local x=foo()) && x == 42)
	{ print x; } timeout 5sec
	{
        print "timeout";
	}
	}

function b_function ( a: int, b: count, another_argument_for_longer_linewrapping: string ) : string
	{
	# This should stay on one line:
	local r1 = SomeRecord($foo=some_foo(), $bar=some$long_bar());
	# This should not:
	local t2 = SomeRecord($foo=some_foo(),
	    $bar=some$long_bar()
	    );

	call( # with an interrupting comment
		arg1, arg2);

	switch (  foo  )  {
	case A: bar(); fallthrough;
	case B: bar(); break;
	default: bar(); baz();
	}

	while ( T )
		do_one_thing();

	while ( ! F ) { do_another_thing(); and_another(); }

	for ( i in some_set )
		do_one_thing();
	for ( i in some_set )
		{ do_one_thing(); and_another(); }

	{ a_block_for_the_sake_of_it(); }
	}

function blanklines() {

	foo();
	bar();
  
	# With one comment
	baz(); # and another comment

	# String-like directives:
	print @DIR,  @FILENAME;

}

# This verifies handling of comment-only content in a block
function comments()
       {
       # TODO: fix this
       # and this too
       }

function no_comments()
       {
       }


