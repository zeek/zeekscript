type Foo: record {
	aa: string &default="";
	bb: count &default=0; # a comment

	cc: bool;
	dd: bool &default=F;
};
