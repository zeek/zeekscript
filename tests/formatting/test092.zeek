event zeek_init()
    {
    register_handler(some_usecase, function(h: addr, si: SomeInfo)
        {
        print h;
        }, 6.0);
    }
