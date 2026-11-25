#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "stepper.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int err = rotor_init();

	if (err) {
		LOG_ERR("rotor init failed: %d", err);
		return err;
	}

	LOG_INF("holo-rotor ready. Type 'rotor help' in this terminal.");
	return 0;
}
