#pragma once

/**
 * spi_cmd.h -- SPI command link (master side, NUCLEO-G474RE)
 *
 * The H7S78-DK SPI slave sends the SVPWM reference (frequency + magnitude)
 * over an 8-byte full-duplex SPI frame, and raises a GPIO line on the master
 * (PC7) to request an exchange.  This module watches that line and applies the
 * command to the SVPWM controller via set_frequency()/set_magnitude().
 */

int spi_cmd_start(void);