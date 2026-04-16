# Ternary expressions: long condition, long branches, wrapping variants.

event some_event_abc(host: addr, svc: string, region: string)
    {
    local endpoints = some_long_variable_ab > 0 ? calls$some_fld :
                                                  "Disabled";
    }

local x = cond ? some_very_very_very_long_true_value_expression_here : some_very_very_very_long_false_value_expression_here;

local x = some_very_long_condition_expression_here ? some_very_long_true_value_expression_here : some_long_false;
