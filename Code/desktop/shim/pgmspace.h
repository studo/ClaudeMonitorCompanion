#pragma once

// Flash and RAM share one address space on a desktop, so PROGMEM is nothing.
#define PROGMEM
#define PGM_P const char *
