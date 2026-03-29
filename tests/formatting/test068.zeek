@ifdef ( SOME_FEATURE )
event SomeModule::some_esp_message(c: connection, is_orig: bool, msg: SomeModule::SomeEspMsg) &group=MyPkg_SomeEvtGroup { shuntit(c); }
@endif
