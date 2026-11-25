/*
 * STEP/DIR driver for a TMC2209 in standalone mode.
 *
 * Step pulses come from the nRF52840 PWM peripheral, so their timing is set by
 * the crystal and is not disturbed by USB, the shell or anything else running on
 * the CPU. A low-priority thread only adjusts the PWM period every few ms to
 * follow a linear speed ramp.
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "stepper.h"

LOG_MODULE_REGISTER(rotor, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec step = PWM_DT_SPEC_GET_BY_NAME(USER_NODE, step);
static const struct gpio_dt_spec dir = GPIO_DT_SPEC_GET(USER_NODE, dir_gpios);
static const struct gpio_dt_spec en = GPIO_DT_SPEC_GET(USER_NODE, en_gpios);
static const struct gpio_dt_spec vcc_en = GPIO_DT_SPEC_GET_OR(USER_NODE, vcc_en_gpios, {0});

#define RAMP_PERIOD_MS 2
#define MAX_RPM 1500.0f
/* Below this step rate the output is switched off instead of pulsing very slowly. */
#define MIN_STEP_HZ 20.0f

static K_MUTEX_DEFINE(lock);
static float target_rpm;
static float current_rpm;
static float accel_rpm_s = 100.0f; /* 0 -> 1000 rpm in 10 s: gentle on a spinning phone */
static int microsteps = 8;         /* TMC2209 standalone default with MS1 = MS2 = GND */
static bool enabled;
static uint32_t last_period_ns;
static int last_dir = -1;

static float rpm_to_step_hz(float rpm)
{
	return fabsf(rpm) / 60.0f * ROTOR_FULLSTEPS_PER_REV * microsteps;
}

static void apply_output(float rpm)
{
	float hz = rpm_to_step_hz(rpm);

	if (hz < MIN_STEP_HZ) {
		if (last_period_ns != 0) {
			pwm_set_pulse_dt(&step, 0);
			last_period_ns = 0;
		}
		return;
	}

	int d = rpm >= 0.0f ? 1 : 0;

	if (d != last_dir) {
		/* Only reached near zero speed, since the ramp passes through 0 to reverse. */
		gpio_pin_set_dt(&dir, d);
		last_dir = d;
		k_busy_wait(1); /* DIR setup time before the next STEP edge (datasheet: 20 ns) */
	}

	uint32_t period_ns = (uint32_t)(1e9f / hz);

	if (period_ns != last_period_ns) {
		int err = pwm_set_dt(&step, period_ns, period_ns / 2);

		if (err) {
			LOG_ERR("pwm_set failed (%d) for period %u ns", err, period_ns);
		}
		last_period_ns = period_ns;
	}
}

static void ramp_thread(void *a, void *b, void *c)
{
	const float dt = RAMP_PERIOD_MS / 1000.0f;

	while (true) {
		k_mutex_lock(&lock, K_FOREVER);

		float goal = enabled ? target_rpm : 0.0f;
		float max_delta = accel_rpm_s * dt;
		float diff = goal - current_rpm;

		if (fabsf(diff) <= max_delta) {
			current_rpm = goal;
		} else {
			current_rpm += diff > 0 ? max_delta : -max_delta;
		}

		/* Jump over the dead band near zero so starts are not sluggish. */
		float min_rpm = MIN_STEP_HZ * 60.0f / (ROTOR_FULLSTEPS_PER_REV * microsteps);

		if (goal != 0.0f && fabsf(current_rpm) < min_rpm &&
		    (goal > 0) == (current_rpm >= 0)) {
			current_rpm = goal > 0 ? min_rpm : -min_rpm;
		}

		apply_output(current_rpm);

		if (!enabled && current_rpm == 0.0f) {
			gpio_pin_set_dt(&en, 0); /* finished ramping down: release the coils */
		}
		k_mutex_unlock(&lock);

		k_msleep(RAMP_PERIOD_MS);
	}
}

K_THREAD_DEFINE(ramp_tid, 1024, ramp_thread, NULL, NULL, NULL, 5, K_FP_REGS, 0);

int rotor_init(void)
{
	if (!pwm_is_ready_dt(&step) || !gpio_is_ready_dt(&dir) || !gpio_is_ready_dt(&en)) {
		LOG_ERR("rotor hardware not ready");
		return -ENODEV;
	}

	if (vcc_en.port && gpio_is_ready_dt(&vcc_en)) {
		gpio_pin_configure_dt(&vcc_en, GPIO_OUTPUT_ACTIVE);
	}

	gpio_pin_configure_dt(&dir, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&en, GPIO_OUTPUT_INACTIVE); /* driver off at boot */
	pwm_set_pulse_dt(&step, 0);

	return 0;
}

int rotor_set_rpm(float rpm)
{
	if (fabsf(rpm) > MAX_RPM) {
		return -EINVAL;
	}
	k_mutex_lock(&lock, K_FOREVER);
	target_rpm = rpm;
	k_mutex_unlock(&lock);
	return 0;
}

void rotor_set_accel(float rpm_per_s)
{
	k_mutex_lock(&lock, K_FOREVER);
	accel_rpm_s = rpm_per_s;
	k_mutex_unlock(&lock);
}

int rotor_set_microsteps(int usteps)
{
	switch (usteps) {
	case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128: case 256:
		break;
	default:
		return -EINVAL;
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (current_rpm != 0.0f) {
		k_mutex_unlock(&lock);
		return -EBUSY;
	}
	microsteps = usteps;
	k_mutex_unlock(&lock);
	return 0;
}

void rotor_enable(bool on)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (on) {
		gpio_pin_set_dt(&en, 1);
		enabled = true;
	} else {
		/* Ramp down first; the ramp thread drops EN once stopped (see rotor_estop for instant). */
		enabled = false;
	}
	k_mutex_unlock(&lock);
}

void rotor_estop(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	enabled = false;
	target_rpm = 0.0f;
	current_rpm = 0.0f;
	pwm_set_pulse_dt(&step, 0);
	last_period_ns = 0;
	gpio_pin_set_dt(&en, 0); /* motor coils off: the cylinder will coast */
	k_mutex_unlock(&lock);
}

void rotor_get_status(struct rotor_status *st)
{
	k_mutex_lock(&lock, K_FOREVER);
	st->target_rpm = target_rpm;
	st->current_rpm = current_rpm;
	st->step_hz = last_period_ns ? 1e9f / last_period_ns : 0.0f;
	st->accel_rpm_s = accel_rpm_s;
	st->microsteps = microsteps;
	st->enabled = enabled;
	k_mutex_unlock(&lock);
}
