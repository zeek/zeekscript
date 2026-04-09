function some_func(src_host: addr, services: int,
                           regions: int, endpoints: string)
    {
    NOTICE([$note=Some_Notice_Type,
            $msg=fmt("Some long format string describing something with multiple parameters (%d items across %d groups within the last %s).",
                     services, regions, some_window),
            $sub=endpoints, $src=src_host, $identifier=cat(src_host)]);
    }
