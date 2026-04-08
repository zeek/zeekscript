function some_fn()
	{
	if ( some_condition )
		{
		local info =
			SomeLongModule::SomeFn($ts=ts, $uid=uid,
			                       $use_case=usecase,
			                    $use_case_description=usecase_desc,
			          $entity_training_items=entity_training_items,
			                       $entity=entity,
			                       $original_entity=original_entity,
			                       $item=item,
			             $first_seen_type=some_enum_map[some_type],
			                       $history_days=history_days,
			                       $history=history);
		}
	}
