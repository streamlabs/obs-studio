#include <obs-module.h>

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
	obs_module_set_load_error(obs_current_module(), "TEST_DEVICE_UNAVAILABLE", "Test device is unavailable.");
	return false;
}
