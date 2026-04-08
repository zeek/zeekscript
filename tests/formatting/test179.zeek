export {
    redef enum Log::ID += { LOG };
}

type CertInfo: record {
    cn: string &log &optional;
    sans: set[string] &log &optional;
};
