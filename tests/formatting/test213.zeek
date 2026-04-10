# Non-first lambda arg gets a line break before it.
event zeek_init()
    {
    register_use_case(http_basic_usecase,
                      function(o_h: addr, si: ProxySourceInfo)
        {
        NOTICE(
               [$note=HTTP_Basic_Auth,
                $msg="HTTP basic authentication bruteforce attempt.",
                $identifier=cat(o_h), $src=o_h,
                $sub=fmt("Saw %d failed and %d successful HTTP authentication attempts.",
                         |si$failed_entities|,
                         |si$successful_entities|)]);
        }, 250.0);
    }
