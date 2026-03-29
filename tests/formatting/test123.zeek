@ifdef ( SomeFeature )

    module SomeMod;

    const some_var = "some-value";

    event zeek_init()
        {
        print some_var;
        }
@endif
