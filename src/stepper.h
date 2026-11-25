#pragma once

#include <stdbool.h>

/* Full steps per motor revolution (1.8 degree motor such as the 17HS4401). */
#define ROTOR_FULLSTEPS_PER_REV 200

int rotor_init(void);

/* Signed target speed in rpm; the ramp thread accelerates/decelerates toward it. */
int rotor_set_rpm(float rpm);
void rotor_set_accel(float rpm_per_s);
int rotor_set_microsteps(int usteps);

void rotor_enable(bool on);
void rotor_estop(void);

struct rotor_status {
	float target_rpm;
	float current_rpm;
	float step_hz;
	float accel_rpm_s;
	int microsteps;
	bool enabled;
};

void rotor_get_status(struct rotor_status *st);
