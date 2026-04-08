event test_fn() {
    if ( some_condition ) {
        if ( [date, ip_mask, resp_p, p, corrected_services, is_server] in ns_svc_cache )
            return;
    }
}
