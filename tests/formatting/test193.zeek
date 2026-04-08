const service_patterns = table(
    [/(.*\.)?(akamaized|akamaihd|akamai|akamaiapis)\.(com|net)/i] =
        "akamai",

    # Meta domains.
    [/(.*\.)?(meta|(oculus(cdn)?)|facebook(-dns|-hardware)?|fbcdn)\.(com|net)/i] =
        "facebook",
);
