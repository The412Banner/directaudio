# MMDevAPI driver functions (Wine 10 ABI)
# mmdevapi load_driver() resolves these on the PE via GetProcAddress
# (see dlls/mmdevapi/mmdevapi_private.h DriverFuncs).
@ stdcall -private get_device_guid(long ptr ptr) get_device_guid
@ stdcall -private get_device_name_from_guid(ptr ptr ptr) get_device_name_from_guid
