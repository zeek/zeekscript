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
	else if  (bar && baz)
		print "bar";
	else if ( baz)
		# This comment should not move. Also, the following should
		# _not_ wrap because the long string alone is too long for
		# the line limit.
		print fmt("%s", "Lovely patio around the fountain. Spent a lovely lunch on the patio.");
	else
		print "Lovely patio around the fountain. " + "Spent a lovely lunch on the patio. " + "The menu was inviting and lots of things I wanted to order. " + "Ordered the Eutropia pizza thin crust-YUM! " + "Will go back the next time I'm in Berkeley.";
	}

function b_function ( a: int, b: count, another_argument_for_longer_linewrapping: string ) : string
	{
	# Ensure we don't break around the "$bar=" here and align every field assignment.
	local foo = SomeRecord($foo=some_foo_making_function(), $bar=some$long_bar_field);

	call( # with an interrupting comment
		arg1, arg2);

	switch (  foo  )  {
	case A: bar();
	case B: bar();
	default: bar();
	}
	
	}

function blanklines() {

	foo();
	bar();
  
	# With one comment
	baz(); # and another comment

	# String-like directives:
	print @DIR,  @FILENAME;

}


