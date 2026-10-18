#pragma once

#include "sim65.h"

// Initialise/reset the E: editor state.
void atari_editor_init(sim65 s);

// Device-handler callback for the E: editor.
int atari_editor_handle(sim65 s, struct sim65_reg *regs, unsigned addr, int data);
