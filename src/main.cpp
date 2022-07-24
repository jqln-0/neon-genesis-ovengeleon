#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include <sstream>
#include <string>
#include <iostream>
#include <iomanip>

#include <Fonts/FreeSerif18pt7b.h>

#define DISPLAY_CS (17)
#define DISPLAY_DC (16)
#define DISPLAY_SCLK (18)
#define DISPLAY_MOSI (19)
#define DISPLAY_BACKLIGHT_EN (20)

#define BUTTON_TOP_LEFT (12)
#define BUTTON_TOP_RIGHT (13)
#define BUTTON_BOTTOM_LEFT (14)
#define BUTTON_BOTTOM_RIGHT (15)

#define BUZZER (26)

#define TOP_ELEMENT (26)
#define BOTTOM_ELEMENT (26)

#define HEADER_FOOTER_SIZE (12)
#define TEMPERATURE_WARM (50)
#define TEMPERATURE_HOT (85)

using std::string;
using std::ostringstream;

// ** GLOBALS ** //

Adafruit_ST7789 display = Adafruit_ST7789(DISPLAY_CS, DISPLAY_DC, DISPLAY_MOSI, DISPLAY_SCLK);

// State machine
enum State {
	MAIN_MENU,
	CALIBRATE_1,
	CALIBRATE_2,
	CALIBRATE_3,
	PICK_PROFILE,
	BAKE,
	FINISHED_BAKE,
	FINISHED_CALIBRATE
};
State current_state = MAIN_MENU;
bool has_state_changed = false;

// Temperature
int current_temp = -1;
int last_temp = -1;
uint16_t current_temp_color = 0x0000;

// Calibration
unsigned long last_drawn_time = 0;
unsigned long calibrate_1_start_time = 0;
unsigned long calibrate_2_start_time = 0;
unsigned long calibrate_3_start_time = 0;
unsigned long calibration_cool_lag_time = -1;
unsigned long calibration_heat_lag_time = -1;
int calibration_lag_degrees = -1;

// Baking
enum ReflowState {
	PREHEAT,
	SOAK,
	REFLOW,
	COOL
};
ReflowState reflow_state = PREHEAT;
int holding_at_temp = 0;
int holding_at_time = 0;
int holding_at_reheat_time = 0;
int desired_temp = 0;

// Menus
int last_drawn_selection = -1;
int selection = 0;
int num_items = 0;

// ** UTIL FUNCTIONS ** //

enum Justification {
	LEFT,
	CENTER,
	RIGHT,
};

void justified_text(const string& text, int x, int y, Justification j) {
	int16_t xx, yy;
	uint16_t w, h;
	display.getTextBounds(text.c_str(), 0, 0, &xx, &yy, &w, &h);

	if (j == CENTER) x -= w / 2;
	if (j == RIGHT) x -= w;

	display.fillRect(x, y, w, h, 0x0000);
	display.setCursor(x, y);
	display.print(text.c_str());
}

string get_time_string(unsigned long millis) {
	ostringstream str;
	str << std::setfill('0') << std::setw(2);
	str << millis / 1000 / 60 << ":" << millis / 1000 % 60;
	return str.str();
}

void draw_temperature() {
	string temp_sign = "";
	if (current_temp != -1 && last_temp != -1) {
		if (current_temp > last_temp) temp_sign = "+++";
		if (current_temp < last_temp) temp_sign = "---";
	}

	ostringstream temp_string;
	temp_string << "TEMP: ";
	if (current_temp == -1) temp_string << "???";
	else temp_string << "???";
	temp_string << "C " << temp_sign;
	string text = temp_string.str();

	int16_t x, y;
	uint16_t w, h;
	display.getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);

	x = (320 / 2) - (w / 2);
	y = 240 - HEADER_FOOTER_SIZE + 2;

	display.setCursor(x, y);
	display.fillRect(x, y, w, h, current_temp_color);
	display.print(text.c_str());
}

void draw_header() {
	uint16_t colour_fg = ST77XX_WHITE;
	uint16_t colour_bg = current_temp_color;

	display.fillRect(0, 0, 320, HEADER_FOOTER_SIZE, colour_bg);
	display.setTextColor(colour_fg);
	display.setTextSize(1);

	string l_action = "";
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			l_action = "PICK";
			break;
		case FINISHED_BAKE:
		case FINISHED_CALIBRATE:
			l_action = "DONE";
			break;
		default:
			break;
	}
	display.setCursor(0, 2);
	display.print(l_action.c_str());

	string title = "";
	switch (current_state) {
		case MAIN_MENU:
			title = "";
			break;
		case CALIBRATE_1:
		case CALIBRATE_2:
		case CALIBRATE_3:
			title = "CALIBRATING";
			break;
		case PICK_PROFILE:
			title = "PROFILE?";
			break;
		case BAKE:
			title = "REFLOWING";
			// TODO: use profile name?
			break;
		case FINISHED_BAKE:
		case FINISHED_CALIBRATE:
			title = "FINISHED";
			break;
		default:
			break;
	}
	justified_text(title, 320 / 2, 2, CENTER);

	string r_action = "";
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			r_action = "UP";
			break;
		default:
			break;
	}
	justified_text(r_action, 320, 2, RIGHT);
}

void draw_footer() {
	uint16_t colour_fg = ST77XX_WHITE;
	uint16_t colour_bg = current_temp_color;

	display.fillRect(0, 240 - HEADER_FOOTER_SIZE, 320, HEADER_FOOTER_SIZE, colour_bg);
	display.setTextColor(colour_fg);
	display.setTextSize(1);

	string l_action = "";
	switch (current_state) {
		case CALIBRATE_1:
		case CALIBRATE_2:
		case CALIBRATE_3:
		case BAKE:
			l_action = "CANCEL";
			break;
		case PICK_PROFILE:
			l_action = "BACK";
			break;
		default:
			break;
	}
	display.setCursor(0, 240 - HEADER_FOOTER_SIZE + 2);
	display.print(l_action.c_str());

	string r_action = "";
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			r_action = "DOWN";
			break;
		default:
			break;
	}
	justified_text(r_action, 320, 240 - HEADER_FOOTER_SIZE + 2, RIGHT);

	draw_temperature();
}

bool test_elements_state = false;
int test_temperature = 24;

void set_elements_state(bool on_or_off) {
	test_elements_state = on_or_off;
	if (on_or_off) {
		digitalWrite(TOP_ELEMENT, HIGH);
		digitalWrite(BOTTOM_ELEMENT, HIGH);
	} else {
		digitalWrite(TOP_ELEMENT, LOW);
		digitalWrite(BOTTOM_ELEMENT, LOW);
	}
}

void change_state(State new_state) {
	display.fillScreen(ST77XX_BLACK);
	draw_header();
	draw_footer();

	current_state = new_state;
	last_drawn_selection = -1;
	selection = 0;
	unsigned long current_time = millis();
	switch (current_state) {
		case MAIN_MENU:
			set_elements_state(false);
			display.setFont(&FreeSerif18pt7b);
			display.setTextSize(1);
			display.setCursor(0, 50);
			display.print("NEON\nGENESIS\nOVENGELION");
			num_items = 2;
			break;
		case PICK_PROFILE:
			// TODO.
			num_items = 1;
			break;
		case CALIBRATE_1:
			display.setCursor(0, 20);
			display.setTextSize(2);
			display.print("STAGE 1: HEATING to 240C");
			break;
		case CALIBRATE_2:
			display.setCursor(0, 20);
			display.setTextSize(2);
			display.print("STAGE 2: WAIT FOR COOL");
			break;
		case CALIBRATE_3:
			display.setCursor(0, 20);
			display.setTextSize(2);
			display.print("STAGE 3: WAIT FOR HEAT");
			break;
		case FINISHED_CALIBRATE:
			set_elements_state(false);
			display.setCursor(0, 20);
			display.setTextSize(2);
			display.print("CALIBRATION COMPLETE!\n");
			display.print("TOTAL TIME: ");
			display.println(get_time_string(current_time - calibrate_1_start_time).c_str());
			display.print("COOL LAG TIME: ");
			display.println(get_time_string(calibration_cool_lag_time).c_str());
			display.print("HEAT LAG TIME: ");
			display.println(get_time_string(calibration_heat_lag_time).c_str());
			display.print("LAG DEGREES: ");
			display.println(calibration_lag_degrees);

			display.print("\nWRITING TO FLASH... ");
			// TODO: write to filesystem.
			delay(1000);
			display.print("OK!");
			break;
		default:
			break;
	}
	has_state_changed = true;
}

uint16_t get_temperature_color() {
	if (current_temp == -1 || last_temp == -1) {
		return 0x0000;
	}
	if (current_temp > TEMPERATURE_HOT) {
		return 0x8082;
	}
	if (current_temp > TEMPERATURE_WARM) {
		return 0xBDA3;
	}
	return 0x1423;
}

void update_temperature() {
	last_temp = current_temp;

	// TODO: read the sensor
	if (test_elements_state) {
		test_temperature++;
	} else if (test_temperature > 24) {
		test_temperature--;
	}
	
	current_temp = test_temperature;
}

void main_menu_loop() {
	display.setTextSize(2);
	display.setFont();

	if (last_drawn_selection != selection) {
		if (selection == 0) display.setTextColor(0x0000, 0xffff);
		else display.setTextColor(0xffff, 0x0000);
		justified_text("BAKE", 320 / 2, 165, CENTER);
		if (selection == 1) display.setTextColor(0x0000, 0xffff);
		else display.setTextColor(0xffff, 0x0000);
		justified_text("CALIBRATE", 320 / 2, 195, CENTER);
		display.setTextColor(0xffff, 0x0000);
	}
}

/**
 * First stage of calibration. Turn on both elements and wait until the
 * temperature is high.
 */
void calibrate_1_loop() {
	if (current_temp == -1) {
		// Wait for the temperature to be available. Shouldn't normally
		// happen.
		return;
	}

	// Keep the elements on until we get to a reasonable reflow temp.
	set_elements_state(true);

	unsigned long current_time = millis();
	if (current_temp >= 240) {
		calibrate_2_start_time = current_time;
		change_state(CALIBRATE_2);
		return;
	}

	if (last_drawn_time = 0 || last_drawn_time - current_time >= 1000) {
		justified_text( get_time_string(current_time - calibrate_1_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

/**
 * Second stage of calibration. Disable the elements, and wait and see how long
 * until the temperature begins to drop. Also keep an eye on what temperature
 * we end up reaching.
 */
void calibrate_2_loop() {
	// Disable both heaters.
	set_elements_state(false);

	unsigned long current_time = millis();
	if (last_temp > current_temp) {
		// Temperature is falling! Record things.
		calibration_cool_lag_time = current_time - calibrate_2_start_time;
		calibration_lag_degrees = last_temp;

		calibrate_3_start_time = current_time;
		change_state(CALIBRATE_3);
		return;
	}

	if (last_drawn_time = 0 || last_drawn_time - current_time >= 1000) {
		justified_text( get_time_string(current_time - calibrate_2_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

/**
 * Third and final stage of calibration. Enable both elements again, and see
 * how long until the temperature starts rising again.
 */
void calibrate_3_loop() {
	// Enable both heaters.
	set_elements_state(true);

	unsigned long current_time = millis();
	if (last_temp < current_temp) {
		// Temperature is rising!
		calibration_heat_lag_time = current_time - calibrate_3_start_time;

		change_state(FINISHED_CALIBRATE);
		return;
	}

	if (last_drawn_time = 0 || last_drawn_time - current_time >= 1000) {
		justified_text( get_time_string(current_time - calibrate_3_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

void pick_profile_loop() {
	// TODO: redraw items if selection changed
}

void reflow_loop() {
	unsigned long current_time = millis();
	// TODO: incorporate calibration data. turn off at lag degrees, for
	// lag seconds?
	switch (reflow_state) {
		case PREHEAT:
			set_elements_state(true);
			if (current_temp > 12) {
				reflow_state = SOAK;
			}
			break;
		case SOAK:
			break;
		case REFLOW:
			break;
		case COOL:
			set_elements_state(false);
			if (current_temp < 12) {
				change_state(FINISHED_BAKE);
			}
			break;
	}

	if (desired_temp - current_temp < calibration_lag_degrees && holding_at_time == -1) {
		// We are within lag_temp of the temperature we should be aiming for.
		// Time to start turning the elements off!
		holding_at_temp = desired_temp;
		holding_at_time = current_time;
	}

	if (holding_at_temp == desired_temp) {
		// We are currently holding the elements off. But what's the state of
		// things?
		if (current_temp > desired_temp) {
			// We overshot! Keep holding the elements off.
			set_elements_state(false);
		} else if (current_temp < desired_temp - calibration_lag_degrees) {
			// We have held off for too long. This hopefully shouldn't happen,
			// but if it does we immediately stop holding.
			holding_at_temp = -1;
			holding_at_time = -1;
			holding_at_reheat_time = -1;
			set_elements_state(true);
		} else if (current_time - holding_at_time >= calibration_cool_lag_time
				|| last_temp > current_temp) {
			// We have been holding the element off for long enough that it
			// should be dropping very soon, or temp is already dropping.
			if (holding_at_reheat_time == -1) {
				// Turn the elements back on for a bit.
				set_elements_state(true);
				holding_at_reheat_time = current_time;
			} else if (current_time - holding_at_reheat_time < calibration_heat_lag_time) {
				// We have held them on for long enough. Turn them back off,
				// and begin the cycle again.
				set_elements_state(false);
				holding_at_time = current_time;
				holding_at_reheat_time = -1;
			}
		}
		// Otherwise, we are holding the element off and everything looks okay.
		// Stay the course.
	}
}

bool check_pin_with_debounce(int pin) {
	delay(10);
	return digitalRead(pin);
}

void top_left_pushed() {
	if (!check_pin_with_debounce(BUTTON_TOP_LEFT)) return;
	switch (current_state) {
		case MAIN_MENU:
			calibrate_1_start_time = millis();
			change_state(CALIBRATE_1);
			break;
		default:
			break;
	}
}

void top_right_pushed() {
	if (!check_pin_with_debounce(BUTTON_TOP_RIGHT)) return;
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			selection = (selection + 1) % num_items;
			break;
		default:
			break;
	}
}

void bottom_left_pushed() {
	if (!check_pin_with_debounce(BUTTON_BOTTOM_LEFT)) return;
	switch (current_state) {
		case FINISHED_BAKE:
		case FINISHED_CALIBRATE:
			change_state(MAIN_MENU);
			break;
		default:
			break;
	}
}

void bottom_right_pushed() {
	if (!check_pin_with_debounce(BUTTON_BOTTOM_RIGHT)) return;
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			selection = (selection - 1) % num_items;
			break;
		default:
			break;
	}
}

void setup() {
	pinMode(BUTTON_TOP_LEFT, INPUT_PULLUP);
	pinMode(BUTTON_TOP_RIGHT, INPUT_PULLUP);
	pinMode(BUTTON_BOTTOM_LEFT, INPUT_PULLUP);
	pinMode(BUTTON_BOTTOM_RIGHT, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(BUTTON_TOP_LEFT), top_left_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_TOP_RIGHT), top_right_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_BOTTOM_LEFT), bottom_left_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_BOTTOM_RIGHT), bottom_right_pushed, FALLING);

	pinMode(TOP_ELEMENT, OUTPUT);
	pinMode(BOTTOM_ELEMENT, OUTPUT);
	digitalWrite(TOP_ELEMENT, LOW);
	digitalWrite(BOTTOM_ELEMENT, LOW);

	pinMode(DISPLAY_BACKLIGHT_EN, OUTPUT);
	digitalWrite(DISPLAY_BACKLIGHT_EN, HIGH);

	display.init(240, 320);
	display.setSPISpeed(62'500'000);
	display.setRotation(3);

	change_state(MAIN_MENU);
	display.setFont();
	display.setTextSize(2);
	display.fillScreen(ST77XX_BLACK);
	display.setTextColor(ST77XX_WHITE);

	main_menu_loop();
}

void loop() {
	update_temperature();

	uint16_t temp_color = get_temperature_color();
	if (temp_color != current_temp_color) {
		current_temp_color = temp_color;
		draw_header();
		draw_footer();
	}

	draw_temperature();


	int delay_ms = 1000;
	switch (current_state) {
		case MAIN_MENU:
			main_menu_loop();
			break;
		case CALIBRATE_1:
		case CALIBRATE_2:
		case CALIBRATE_3:
			// Update very frequently so that our lag times are accurate.
			delay_ms = 100;
			break;
		case PICK_PROFILE:
			break;
		case BAKE:
			break;
		case FINISHED_BAKE:
			break;
		case FINISHED_CALIBRATE:
			break;
	}

	if (!has_state_changed) {
		has_state_changed = false;
		delay(delay_ms);
	}
}
