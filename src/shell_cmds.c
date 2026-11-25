#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "stepper.h"

static int cmd_rpm(const struct shell *sh, size_t argc, char **argv)
{
	float rpm = strtof(argv[1], NULL);
	int err = rotor_set_rpm(rpm);

	if (err) {
		shell_error(sh, "rpm out of range");
		return err;
	}
	rotor_enable(true);
	shell_print(sh, "target %.1f rpm", (double)rpm);
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	rotor_set_rpm(0.0f);
	rotor_enable(false);
	shell_print(sh, "ramping down");
	return 0;
}

static int cmd_estop(const struct shell *sh, size_t argc, char **argv)
{
	rotor_estop();
	shell_warn(sh, "driver disabled immediately; rotor is coasting");
	return 0;
}

static int cmd_accel(const struct shell *sh, size_t argc, char **argv)
{
	float a = strtof(argv[1], NULL);

	if (a <= 0.0f || a > 5000.0f) {
		shell_error(sh, "accel must be in (0, 5000] rpm/s");
		return -EINVAL;
	}
	rotor_set_accel(a);
	shell_print(sh, "accel %.1f rpm/s", (double)a);
	return 0;
}

static int cmd_ustep(const struct shell *sh, size_t argc, char **argv)
{
	int err = rotor_set_microsteps(atoi(argv[1]));

	if (err == -EBUSY) {
		shell_error(sh, "stop the motor first");
	} else if (err) {
		shell_error(sh, "use 1,2,4,...,256 and match the MS1/MS2 wiring");
	}
	return err;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct rotor_status st;

	rotor_get_status(&st);
	shell_print(sh, "enabled:    %s", st.enabled ? "yes" : "no");
	shell_print(sh, "target:     %.1f rpm", (double)st.target_rpm);
	shell_print(sh, "current:    %.1f rpm", (double)st.current_rpm);
	shell_print(sh, "step rate:  %.0f Hz", (double)st.step_hz);
	shell_print(sh, "accel:      %.1f rpm/s", (double)st.accel_rpm_s);
	shell_print(sh, "microsteps: %d", st.microsteps);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(rotor_cmds,
	SHELL_CMD_ARG(rpm, NULL, "Spin at <rpm> (negative = reverse), ramped", cmd_rpm, 2, 0),
	SHELL_CMD(stop, NULL, "Ramp down to 0 and release the driver", cmd_stop),
	SHELL_CMD(estop, NULL, "Cut the driver immediately (rotor coasts)", cmd_estop),
	SHELL_CMD_ARG(accel, NULL, "Set ramp rate in rpm/s", cmd_accel, 2, 0),
	SHELL_CMD_ARG(ustep, NULL, "Set microsteps to match MS1/MS2 wiring", cmd_ustep, 2, 0),
	SHELL_CMD(status, NULL, "Show speed and settings", cmd_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(rotor, &rotor_cmds, "Rotor (NEMA 17 + TMC2209) control", NULL);
