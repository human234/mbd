#pragma once

/**
 * control.h -- SVPWM 3-phase inverter control API (G474RE)
 *
 * Transplanted from the STM32H743ZI Zephyr project (control.c) to the
 * STM32G474RE. The timer constants and register values have been verified
 * against the corrected G474RETEST reference project.
 */

void init_tim1_pwm(void);
void init_tim1_irq(void);
void set_frequency(float);
void set_magnitude(float);
void svpwm_start(void);
void svpwm_stop(void);
