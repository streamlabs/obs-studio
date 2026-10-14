/* Link-only smoke test for the CEF Windows sandbox import target. */
#include "include/cef_sandbox_win.h"

int main()
{
	void *sandbox_info = cef_sandbox_info_create();
	if (!sandbox_info)
		return 1;

	cef_sandbox_info_destroy(sandbox_info);
	return 0;
}
