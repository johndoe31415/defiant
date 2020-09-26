/**
 *	defiant - Modded Bobby Car toy for toddlers
 *	Copyright (C) 2020-2020 Johannes Bauer
 *
 *	This file is part of defiant.
 *
 *	defiant is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; this program is ONLY licensed under
 *	version 3 of the License, later versions are explicitly excluded.
 *
 *	defiant is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with defiant; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	Johannes Bauer <JohannesBauer@gmx.de>
**/

#include <stdio.h>
#include <stdbool.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_usart.h>

#include "usart_terminal.h"
#include "system.h"
#include "main.h"
#include "audio.h"
#include "ws2812.h"
#include "adc.h"
#include "debounce.h"
#include "time.h"

enum ignition_state_t {
	IGNITION_UNDEFINED,
	IGNITION_ON,
	IGNITION_OFF,
	IGNITION_CRANK,
	IGNITION_CCW,
};

enum turn_signal_state_t {
	TURN_OFF,
	TURN_LEFT,
	TURN_RIGHT,
	TURN_EMERGENCY,
};

enum engine_state_t {
	ENGINE_OFF,
	ENGINE_CRANKING,
	ENGINE_ON,
	ENGINE_SHUTTING_OFF,
};

enum led_color_t {
	LED_OFF,
	LED_WHITE_DARK,
	LED_WHITE_BRIGHT,
	LED_RED,
	LED_BLUE,
	LED_ORANGE,
};

struct uistate_t {
	enum engine_state_t engine_state;
	bool siren;

	enum turn_signal_state_t turn_signal;
	unsigned int turn_signal_tick;
	bool turn_signal_blink;

	unsigned int siren_tick;
	bool siren_blink;

	unsigned int undervoltage_tick;
	unsigned int audio_trigger_point_index;

	struct debounce_t button_left;
	struct debounce_t button_right;
	struct debounce_t button_siren;
	struct debounce_t button_parent;
	struct debounce_t button_ignition_crank;
	struct debounce_t button_ignition_ccw;
	struct debounce_t ignition_state;

	bool disable_ui;

	unsigned int no_action_tick;		/* Nothing happened for a period of time, regardless of ignition state */
	unsigned int shutoff_tick;			/* Ignition off, turn off device */
};

static volatile unsigned int timectr = 0;
static const struct debounce_config_t default_button_config = {
	.fire_threshold = 10,
};
static struct uistate_t ui = {
	.button_left.config = &default_button_config,
	.button_right.config = &default_button_config,
	.button_siren.config = &default_button_config,
	.button_parent.config = &default_button_config,
	.button_ignition_crank.config = &default_button_config,
	.button_ignition_ccw.config = &default_button_config,
	.ignition_state.config = &default_button_config,
};

static void enter_error_mode(unsigned int error_code) {
	printf("%u: error code %u\n", timectr, error_code);
	led_red_set_active();
	led_green_set_to(error_code & 1);
	led_yellow_set_to(error_code & 2);
	led_siren_set_inactive();
	audio_shutoff();
	while (true) {
		__WFI();
	}
}

static void ws2812_convert_state(enum led_color_t color, uint8_t *data) {
	switch (color) {
		case LED_OFF:
			data[0] = 0x00;
			data[1] = 0x00;
			data[2] = 0x00;
			break;

		case LED_WHITE_DARK:
			data[0] = 0x26;
			data[1] = 0x26;
			data[2] = 0x26;
			break;

		case LED_WHITE_BRIGHT:
			data[0] = 0xff;
			data[1] = 0xff;
			data[2] = 0xff;
			break;

		case LED_RED:
			data[0] = 0x00;
			data[1] = 0xff;
			data[2] = 0x00;
			break;

		case LED_BLUE:
			data[0] = 0x00;
			data[1] = 0x00;
			data[2] = 0xff;
			break;

		case LED_ORANGE:
//			data[0] = 0x59;
//			data[1] = 0x97;
//			data[2] = 0x70;
			data[0] = 0x3e;
			data[1] = 0xd3;
			data[2] = 0x00;
			break;
	}
}

static void ws2812_set_state(enum led_color_t left, enum led_color_t right) {
	uint8_t led_data[6];
	ws2812_convert_state(left, led_data + 3);
	ws2812_convert_state(right, led_data + 0);
	ws2812_sendbits(ws2812_PORT, ws2812_PIN, 2, led_data);
}

static enum ignition_state_t determine_ignition_state(void) {
	enum ignition_state_t ignition_state = IGNITION_UNDEFINED;
	bool have_power = pwr_sense_is_active();
	if (have_power) {
		/* Either "ignition on" or "ignition crank" */
		if ((ui.button_ignition_crank.last_state == 0) && (ui.button_ignition_ccw.last_state == 1)) {
			ignition_state = IGNITION_ON;
		} else if ((ui.button_ignition_crank.last_state == 1) && (ui.button_ignition_ccw.last_state == 0)) {
			ignition_state = IGNITION_CRANK;
		}
	} else {
		/* Either "ignition off" or "ignition CCW" */
		if ((ui.button_ignition_crank.last_state == 0) && (ui.button_ignition_ccw.last_state == 0)) {
			ignition_state = IGNITION_OFF;
		} else if ((ui.button_ignition_crank.last_state == 0) && (ui.button_ignition_ccw.last_state == 1)) {
			ignition_state = IGNITION_CCW;
		}
	}
	return ignition_state;
}

void audio_trigger_end_of_sample(unsigned int fileno) {
	if (fileno == FILENO_ENGINE_START) {
		/* Start of engine is finished */
		ui.engine_state = ENGINE_ON;
	} else if (fileno == FILENO_ENGINE_STOP) {
		ui.engine_state = ENGINE_OFF;
	}
}

void audio_trigger_point(void) {
	ui.turn_signal_tick = 0;
	ui.turn_signal_blink = !ui.turn_signal_blink;
}

static bool is_turn_signal_audible(void) {
	int audio_fileno = audio_current_fileno();
	return (audio_fileno == FILENO_TURN_SIGNAL_NO_ENGINE) || (audio_fileno == FILENO_TURN_SIGNAL_WITH_ENGINE);
}

static void ui_set_counters(void) {
	ui.siren_tick++;
	if (ui.siren_tick >= 20) {
		ui.siren_tick = 0;
		ui.siren_blink = !ui.siren_blink;
	}

	if (!is_turn_signal_audible()) {
		ui.turn_signal_tick++;
		if (ui.turn_signal_tick >= 37) {
			ui.turn_signal_tick = 0;
			ui.turn_signal_blink = !ui.turn_signal_blink;
		}
	}

	if (ui.ignition_state.last_state == IGNITION_OFF) {
		ui.shutoff_tick++;
		if (ui.shutoff_tick >= 500) {
			/* about 5 seconds */
			pwr_keepalive_set_inactive();
		}
	} else {
		ui.shutoff_tick = 0;
	}
}

static void ui_handle_undervoltage(void) {
	uint32_t voltage_millivolts = adc_get_ext_voltage_millivolts();
	if (voltage_millivolts < 3500 * 3) {
		ui.undervoltage_tick++;
	} else {
		ui.undervoltage_tick = 0;
	}
	if (ui.undervoltage_tick >= 100) {
		/* One second of straight undervoltage, go into error mode. */
		enter_error_mode(0);
	}
}

static void ui_set_turn_signal(enum turn_signal_state_t signal_state) {
	ui.turn_signal = signal_state;
	ui.turn_signal_blink = false;
	ui.turn_signal_tick = 0;
}

static void ui_handle_turn_signal_buttons(void) {
	if (debounce_button_active(&ui.button_left, button_left_is_active())) {
		if (ui.button_right.last_state) {
			/* Both pressed at the same time */
			ui_set_turn_signal(TURN_EMERGENCY);
		} else {
			/* Left only */
			if (ui.turn_signal == TURN_LEFT) {
				ui_set_turn_signal(TURN_OFF);
			} else {
				ui_set_turn_signal(TURN_LEFT);
			}
		}
	}
	if (debounce_button_active(&ui.button_right, button_right_is_active())) {
		if (ui.button_left.last_state) {
			/* Both pressed at the same time */
			ui_set_turn_signal(TURN_EMERGENCY);
		} else {
			if (ui.turn_signal == TURN_RIGHT) {
				ui_set_turn_signal(TURN_OFF);
			} else {
				ui_set_turn_signal(TURN_RIGHT);
			}
		}
	}
}

static void ui_handle_parent_button(void) {
	if (debounce_button_active(&ui.button_parent, button_parent_is_active())) {
		printf("Parent\n");
	}
}

static void ui_handle_siren_button(void) {
	if (debounce_button_active(&ui.button_siren, button_siren_is_active())) {
		ui.siren = !ui.siren;
		if (ui.siren) {
			ui.siren_blink = false;
			ui.siren_tick = 0;
		}
	}
}

static void ui_handle_ignition_switch(void) {
	debounce_button(&ui.button_ignition_crank, ignition_crank_is_active());
	debounce_button(&ui.button_ignition_ccw, ignition_ccw_is_active());
	enum ignition_state_t current_ignition_state = determine_ignition_state();
	bool ignition_state_changed = debounce_button(&ui.ignition_state, current_ignition_state);
	if (ignition_state_changed) {
		if ((ui.engine_state == ENGINE_OFF) && (ui.ignition_state.last_state == IGNITION_CRANK)) {
			ui.engine_state = ENGINE_CRANKING;
		} else if (((ui.engine_state == ENGINE_ON) || (ui.engine_state == ENGINE_CRANKING)) && ((ui.ignition_state.last_state == IGNITION_OFF) || (ui.ignition_state.last_state == IGNITION_CCW))) {
			ui.engine_state = ENGINE_SHUTTING_OFF;
		}
		if (ui.ignition_state.last_state != IGNITION_OFF) {
			pwr_keepalive_set_active();
		}
	}
}

static bool is_left_blinker_active(void) {
	return ((ui.turn_signal != TURN_OFF) && (ui.turn_signal_blink) && ((ui.turn_signal == TURN_EMERGENCY) || (ui.turn_signal == TURN_LEFT)));
}

static bool is_right_blinker_active(void) {
	return ((ui.turn_signal != TURN_OFF) && (ui.turn_signal_blink) && ((ui.turn_signal == TURN_EMERGENCY) || (ui.turn_signal == TURN_RIGHT)));
}

static void ui_set_headlights(void) {
	enum led_color_t left, right;

	/* By default, white color if not doing anything */
	if (ui.ignition_state.last_state == IGNITION_OFF) {
		left = LED_OFF;
		right = LED_OFF;
	} else {
		if (ui.engine_state == ENGINE_OFF) {
			left = LED_WHITE_DARK;
			right = LED_WHITE_DARK;
		} else {
			left = LED_WHITE_BRIGHT;
			right = LED_WHITE_BRIGHT;
		}
	}

	/* When we're blinking, then this overrides the light */
	if (is_left_blinker_active()) {
		left = LED_ORANGE;
	}
	if (is_right_blinker_active()) {
		right = LED_ORANGE;
	}

	/* But siren has greatest precedence */
	if (ui.siren) {
		if (ui.siren_blink) {
			left = LED_RED;
			right = LED_BLUE;
		} else {
			left = LED_BLUE;
			right = LED_RED;
		}
	}

	ws2812_set_state(left, right);
}

static void ui_set_top_leds(void) {
	uln2003_ledleft_set_to(is_left_blinker_active());
	uln2003_ledright_set_to(is_right_blinker_active());
	led_siren_set_to(ui.siren && ui.siren_blink);
}

void ui_shutoff(void) {
	ui.disable_ui = true;
}

static void ui_check_audio(void) {
	int play_fileno = -1;
	if (ui.engine_state == ENGINE_CRANKING) {
		play_fileno = FILENO_ENGINE_START;
	} else if (ui.engine_state == ENGINE_SHUTTING_OFF) {
		play_fileno = FILENO_ENGINE_STOP;
	} else if (ui.engine_state == ENGINE_ON) {
		if (ui.siren) {
			play_fileno = FILENO_SIREN_WITH_ENGINE;
		} else if (ui.turn_signal != TURN_OFF) {
			play_fileno = FILENO_TURN_SIGNAL_WITH_ENGINE;
		} else {
			play_fileno = FILENO_ENGINE_IDLE;
		}
	} else if (ui.engine_state == ENGINE_OFF) {
		if (ui.siren) {
			play_fileno = FILENO_SIREN_NO_ENGINE;
		} else if (ui.turn_signal != TURN_OFF) {
			play_fileno = FILENO_TURN_SIGNAL_NO_ENGINE;
		} else {
			play_fileno = -1;
		}
	}

	if (play_fileno == -1) {
		audio_shutoff();
	} else {
		audio_playback_fileno(play_fileno, true);
	}
}

int main(void) {
	led_green_set_active();
	pwr_keepalive_set_active();
	printf("Device cold start complete.\n");
//	while (timectr < 125);		/* Wait 250 ms before flash settles */
	audio_init();

	while (!ui.disable_ui) {
		/* Execute roughly 100 Hz */
		systick_wait();

		ui_set_counters();
		ui_handle_undervoltage();
		ui_handle_turn_signal_buttons();
		ui_handle_parent_button();
		ui_handle_siren_button();
		ui_handle_ignition_switch();
		ui_set_headlights();
		ui_set_top_leds();
		ui_check_audio();
	}

	while (true);
}
