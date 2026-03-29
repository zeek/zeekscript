event some_evt()
    {
    si$successful_entities = si$successful_entities |
                             worker_si$successful_entities;
    }
