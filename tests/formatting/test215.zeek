# Global with wrapped attrs inside export block.
export {
    global recently_validated_certs: table[string] of X509::Result
                        &read_expire = 5 mins &redef;
}
