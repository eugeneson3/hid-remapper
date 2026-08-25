#include "interval_override.h"

// C0 always uses the keyboard's advertised polling interval.
volatile uint8_t interval_override = 0;
