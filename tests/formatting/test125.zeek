event some_evt()
    {
    some_func([$aa=BB,
              $cc=fmt("Host uses a weak certificate with %d bits",
                      key_len),
              $dd=e, $ff=SSL::some_suppression_interval,
              $gg=cat(resp_h, c$id$resp_p, hash, key_len)]);
    }
