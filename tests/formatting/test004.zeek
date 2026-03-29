if ( age >= 1 day )
            age_d = interval_to_double(network_time() -
                                       cert$not_valid_before) /
                    86400.0;