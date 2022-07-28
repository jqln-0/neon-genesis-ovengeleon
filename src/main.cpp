#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>
#include <LittleFS.h>

#include <sstream>
#include <string>
#include <iostream>
#include <iomanip>

#include <Fonts/FreeSerif18pt7b.h>

#include <pico/util/queue.h>

#define DISPLAY_CS (17)
#define DISPLAY_DC (16)
#define DISPLAY_SCLK (18)
#define DISPLAY_MOSI (19)
#define DISPLAY_BACKLIGHT_EN (20)

#define TEMP_DO (27)
#define TEMP_CS (26)
#define TEMP_CLK (22)

#define BUTTON_TOP_LEFT (12)
#define BUTTON_TOP_RIGHT (14)
#define BUTTON_BOTTOM_LEFT (13)
#define BUTTON_BOTTOM_RIGHT (15)

#define LED_RED (6)
#define LED_GREEN (7)
#define LED_BLUE (8)

#define BUZZER (26)

#define TOP_ELEMENT (11)
#define BOTTOM_ELEMENT (10)

#define HEADER_FOOTER_SIZE (12)
#define TEMPERATURE_WARM (50)
#define TEMPERATURE_HOT (85)

using std::string;
using std::ostringstream;

// ** GLOBALS ** //

Adafruit_MAX31855 thermocouple(TEMP_CLK, TEMP_CS, TEMP_DO);

class NoArgsType {};
class RectType {
	public:
		int x,y,w,h;
		uint16_t color;
};
enum Justification {
	LEFT,
	CENTER,
	RIGHT,
};
class TextType {
	public:
		int x,y;
		uint16_t fg_color, bg_color;
		Justification justify;
};
class CursorType {
	public:
		int x,y;
};
class ConfigType {
	public:
		int textSize;
		uint16_t textColor;
		const GFXfont *font;
};
class DrawMessage {
	public:
		enum { CLEAR,RECT,TEXT,CURSOR,PRINT,CONFIG } type;
		union {
			NoArgsType nothing;
			RectType rect;
			TextType text;
			CursorType cursor;
			ConfigType config;
		};
		// Delcared separately due to containing a string.
		string *str = nullptr;
};


queue_t drawing_queue;

// Drawing
uint16_t text_bg_color = 0x0000;

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
State next_state = MAIN_MENU;

// Temperature
int current_temp = -1;
int last_temp = -1;
uint16_t current_temp_color = 0x0000;

// Calibration
bool is_calibrated = false;
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

void send_clear() {
	DrawMessage msg{ DrawMessage::CLEAR, NoArgsType{} };
	queue_add_blocking(&drawing_queue, &msg);
}

void send_text(string text, int x, int y, Justification j, uint16_t fg=0xFFFF, uint16_t bg=0x0000) {
	DrawMessage msg{
		DrawMessage::TEXT,
		{
			.text=TextType{
				x, y,
				fg, bg,
				j,
			}
		},
		new string(text)
	};
	queue_add_blocking(&drawing_queue, &msg);
}

void send_config(int textSize=1, uint16_t textColor=0xFFFF, const GFXfont *font=NULL) {
	DrawMessage msg{
		DrawMessage::CONFIG,
		{
			.config=ConfigType{
				textSize,
				textColor,
				font
			}
		}
	};
	queue_add_blocking(&drawing_queue, &msg);
}

void send_rect(int x, int y, int w, int h, uint16_t color) {
	DrawMessage msg{
		DrawMessage::RECT,
		{
			.rect=RectType{x,y,w,h,color}
		}
	};
	queue_add_blocking(&drawing_queue, &msg);
}

void send_print(string text, int x=-1, int y=-1) {
	if (x != -1 && y != -1) {
		DrawMessage msg{
			DrawMessage::CURSOR,
			{
				.cursor=CursorType{x, y}
			}
		};
		queue_add_blocking(&drawing_queue, &msg);
	}

	DrawMessage msg{
		DrawMessage::PRINT,
		{ NoArgsType{} },
		new string(text)
	};
	queue_add_blocking(&drawing_queue, &msg);
}

string get_time_string(unsigned long millis) {
	ostringstream str;
	str << std::setfill('0') << std::setw(2) << millis / 1000 / 60;
	str << ":";
	str << std::setfill('0') << std::setw(2) << millis / 1000 % 60;
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
	else temp_string << current_temp;
	temp_string << "C " << temp_sign;
	string text = temp_string.str();

	int x = (320 / 2);
	int y = 240 - HEADER_FOOTER_SIZE + 2;

	send_config();
	send_text(text, x, y, CENTER, 0xFFFF, current_temp_color);
}

void draw_header() {
	uint16_t color_fg = ST77XX_WHITE;
	uint16_t color_bg = current_temp_color;
	text_bg_color = color_bg;

	send_rect(0, 0, 320, HEADER_FOOTER_SIZE, color_bg);
	send_config();

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
	send_text(l_action, 0, 2, LEFT, color_fg, color_bg);

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
	send_text(title, 320 / 2, 2, CENTER, color_fg, color_bg);

	string r_action = "";
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			r_action = "UP";
			break;
		default:
			break;
	}
	send_text(r_action, 320, 2, RIGHT, color_fg, color_bg);
}

void draw_footer() {
	uint16_t color_fg = ST77XX_WHITE;
	uint16_t color_bg = current_temp_color;
	text_bg_color = color_bg;

	send_rect(0, 240 - HEADER_FOOTER_SIZE, 320, HEADER_FOOTER_SIZE, color_bg);
	send_config();

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
	send_text(l_action, 0, 240 - HEADER_FOOTER_SIZE + 2, LEFT, color_fg, color_bg);

	string r_action = "";
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			r_action = "DOWN";
			break;
		default:
			break;
	}
	send_text(r_action, 320, 240 - HEADER_FOOTER_SIZE + 2, RIGHT, color_fg, color_bg);

	draw_temperature();
}

bool test_elements_state = false;
int test_temperature = 24;
unsigned long last_temp_time = 0;

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

	double raw_value = thermocouple.readCelsius();
	if (isnan(raw_value)) {
		current_temp = -1;
	} else {
		current_temp = (int) (raw_value + 0.5);
	}

	/*
	unsigned long t = millis();
	if (t - last_temp_time > 250) {
		if (test_elements_state) {
			test_temperature++;
		} else if (test_temperature > 24) {
			test_temperature--;
		}
		last_temp_time = t;
	}
	current_temp = test_temperature;
	*/
}

void main_menu_setup() {
	set_elements_state(false);

	send_config(1, 0xFFFF, &FreeSerif18pt7b);
	send_print("NEON\nGENESIS\nOVENGELION", 0, 50);

	send_config();
	if (is_calibrated) {
		send_text("CALIBRATION OK", 320, 220, RIGHT, 0x0000, 0x0F00);
	} else {
		send_text("NO CALIBRATION", 320, 220, RIGHT, 0x0000, 0xF000);
	}

	num_items = 2;
}

void main_menu_loop() {
	send_config(2);

	if (last_drawn_selection != selection) {
		int fg, bg;

		if (selection == 0) { fg=0x0000; bg=0xFFFF; }
		else { fg=0xFFFF; bg=0x0000; }
		send_text("BAKE", 320 / 2, 165, CENTER, fg, bg);

		if (selection == 1) { fg=0x0000; bg=0xFFFF; }
		else { fg=0xFFFF; bg=0x0000; }
		send_text("CALIBRATE", 320 / 2, 195, CENTER, fg, bg);

		last_drawn_selection = selection;
	}
}

void calibrate_1_setup() {
	calibrate_1_start_time = millis();

	send_config(2);
	send_print("STAGE 1: HEATING to 240C", 0, 20);
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
		calibration_lag_degrees = current_temp;
		next_state = CALIBRATE_2;
		return;
	}

	if (last_drawn_time == 0 || current_time - last_drawn_time >= 1000) {
		send_config(3);
		send_text(get_time_string(current_time - calibrate_1_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

void calibrate_2_setup() {
	// Start time was set by stage 1 already.
	send_config(2);
	send_print("STAGE 2: WAIT FOR COOL", 0, 20);
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
		calibration_lag_degrees = last_temp - calibration_lag_degrees;

		calibrate_3_start_time = current_time;
		next_state = CALIBRATE_3 ;
		return;
	}

	if (last_drawn_time == 0 || current_time - last_drawn_time >= 1000) {
		send_config(3);
		send_text( get_time_string(current_time - calibrate_2_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

void calibrate_3_setup() {
	// Start time was set by stage 3 already.
	send_config(2);
	send_print("STAGE 3: WAIT FOR REHEAT", 0, 20);
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

		next_state = FINISHED_CALIBRATE;
		return;
	}

	if (last_drawn_time == 0 || current_time - last_drawn_time >= 1000) {
		send_config(3);
		send_text( get_time_string(current_time - calibrate_3_start_time),
				320 / 2,
				240 / 2,
				CENTER);
		last_drawn_time = current_time;
	}
}

void finished_calibrate_setup() {
	set_elements_state(false);

	unsigned long current_time = millis();

	send_config(2);
	send_print("CALIBRATION COMPLETE!\n", 0, 20);
	send_print("TOTAL TIME: ");
	send_print(get_time_string(current_time - calibrate_1_start_time));
	send_print("\nCOOL LAG TIME: ");
	send_print(get_time_string(calibration_cool_lag_time));
	send_print("\nHEAT LAG TIME: ");
	send_print(get_time_string(calibration_heat_lag_time));
	send_print("\nLAG DEGREES: ");
	send_print(std::to_string(calibration_lag_degrees));

	send_print("\nWRITING TO FLASH... ");
	
	bool r = LittleFS.begin();
	if (!r) {
		digitalWrite(LED_BLUE, LOW);
	} else {
		File f = LittleFS.open("CALIBRATION", "w");
		if (!f) {
			digitalWrite(LED_RED, LOW);
		} else {
			f.println(calibration_cool_lag_time);
			f.println(calibration_heat_lag_time);
			f.println(calibration_lag_degrees);
			f.close();
		}
		LittleFS.end();
	}

	// Hooray! :)
	is_calibrated = true;
	send_print("OK!");
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
				next_state = FINISHED_BAKE;
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

bool read_debounced(int pin) {
	delay(10);
	return digitalRead(pin);
}

void top_left_pushed() {
	if (read_debounced(BUTTON_TOP_LEFT)) return;
	switch (current_state) {
		case MAIN_MENU:
			if (selection == 1) {
				next_state = CALIBRATE_1;
			}
			break;
		case FINISHED_BAKE:
		case FINISHED_CALIBRATE:
			next_state = MAIN_MENU;
			break;
		default:
			break;
	}
}

void top_right_pushed() {
	if (read_debounced(BUTTON_TOP_RIGHT)) return;
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			selection = (selection + num_items - 1) % num_items;
			break;
		default:
			break;
	}
}

void bottom_left_pushed() {
	if (read_debounced(BUTTON_BOTTOM_LEFT)) return;
	switch (current_state) {
		case FINISHED_BAKE:
		case FINISHED_CALIBRATE:
			next_state = MAIN_MENU;
			break;
		default:
			break;
	}
}

void bottom_right_pushed() {
	if (read_debounced(BUTTON_BOTTOM_RIGHT)) return;
	switch (current_state) {
		case MAIN_MENU:
		case PICK_PROFILE:
			selection = (selection + 1) % num_items;
			break;
		default:
			break;
	}
}

void change_state(State new_state) {
	current_state = new_state;

	send_clear();
	draw_header();
	draw_footer();
	text_bg_color = 0x0000;

	// Any menu would want to be reset anyway.
	last_drawn_selection = -1;
	selection = 0;

	unsigned long current_time = millis();
	File f;
	switch (current_state) {
		case MAIN_MENU:
			main_menu_setup();
			break;
		case PICK_PROFILE:
			// TODO.
			num_items = 1;
			break;
		case CALIBRATE_1:
			calibrate_1_setup();
			break;
		case CALIBRATE_2:
			calibrate_2_setup();
			break;
		case CALIBRATE_3:
			calibrate_3_setup();
			break;
		case FINISHED_CALIBRATE:
			finished_calibrate_setup();
			break;
		default:
			break;
	}
}

void setup() {
	// Ensure the elements are off immediately, for safety.
	pinMode(TOP_ELEMENT, OUTPUT);
	pinMode(BOTTOM_ELEMENT, OUTPUT);
	digitalWrite(TOP_ELEMENT, LOW);
	digitalWrite(BOTTOM_ELEMENT, LOW);

	// Next set up our multicore comms, then signal to the other core that
	// it can proceed.
	queue_init(&drawing_queue, sizeof(DrawMessage), 16);
	rp2040.fifo.push(0xDEADBEEF);

	// Now onto the rest of our init...

	pinMode(BUTTON_TOP_LEFT, INPUT_PULLUP);
	pinMode(BUTTON_TOP_RIGHT, INPUT_PULLUP);
	pinMode(BUTTON_BOTTOM_LEFT, INPUT_PULLUP);
	pinMode(BUTTON_BOTTOM_RIGHT, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(BUTTON_TOP_LEFT), top_left_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_TOP_RIGHT), top_right_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_BOTTOM_LEFT), bottom_left_pushed, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_BOTTOM_RIGHT), bottom_right_pushed, FALLING);

	pinMode(DISPLAY_BACKLIGHT_EN, OUTPUT);
	digitalWrite(DISPLAY_BACKLIGHT_EN, HIGH);

	pinMode(LED_RED, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
	pinMode(LED_BLUE, OUTPUT);
	digitalWrite(LED_RED, HIGH);
	digitalWrite(LED_GREEN, HIGH);
	digitalWrite(LED_BLUE, HIGH);

	bool r = LittleFS.begin();
	if (!r) {
		digitalWrite(LED_BLUE, LOW);
	} else {
		File f = LittleFS.open("CALIBRATION", "r");
		if (!f) {
			digitalWrite(LED_RED, LOW);
		} else {
			calibration_cool_lag_time = f.parseInt();
			calibration_heat_lag_time = f.parseInt();
			calibration_lag_degrees = f.parseInt();
			f.close();
			is_calibrated = true;
		}
		LittleFS.end();
	}

	change_state(MAIN_MENU);

	// Init the temperature sensor whilst we wait for the initial screen draw.
	for (int i = 0; i < 5; i++) {
		delay(100);
		update_temperature();
	}
}

void loop() {
	update_temperature();

	uint16_t temp_color = get_temperature_color();
	if (temp_color != current_temp_color) {
		current_temp_color = temp_color;
		draw_header();
		draw_footer();
	} else if (last_temp != current_temp){
		draw_temperature();
	}

	text_bg_color = 0x0000;

	int delay_ms = 100;
	switch (current_state) {
		case MAIN_MENU:
			main_menu_loop();
			break;
		case CALIBRATE_1:
			calibrate_1_loop();
			break;
		case CALIBRATE_2:
			calibrate_2_loop();
			break;
		case CALIBRATE_3:
			calibrate_3_loop();
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

	if (next_state != current_state) {
		change_state(next_state);
	} else {
		delay(delay_ms);
	}
}

/** SECOND CORE **/
Adafruit_ST7789 core1_display = Adafruit_ST7789(DISPLAY_CS, DISPLAY_DC, DISPLAY_MOSI, DISPLAY_SCLK);

void core1_draw_text(const TextType& text, const string& actual_text) {
	auto str = actual_text.c_str();

	int16_t x, y;
	uint16_t w, h;
	core1_display.getTextBounds(str, 0, 0, &x, &y, &w, &h);

	x = text.x;
	y = text.y;

	if (text.justify == CENTER) x -= w / 2;
	if (text.justify == RIGHT) x -= w;

	core1_display.setTextColor(text.fg_color);
	core1_display.fillRect(x, y, w, h, text.bg_color);
	core1_display.setCursor(x, y);
	core1_display.print(str);
}

void setup1() {
	core1_display.init(240, 320);
	core1_display.setSPISpeed(62'500'000);
	core1_display.setRotation(3);

	core1_display.setFont();
	core1_display.setTextSize(2);
	core1_display.setTextColor(ST77XX_WHITE);

	// Wait for the other core to signal us before starting our loop.
	rp2040.fifo.pop();
}

void loop1() {
	DrawMessage message{DrawMessage::CLEAR, { NoArgsType{} }};
	queue_remove_blocking(&drawing_queue, &message);

	string copied_text;
	if (message.str != nullptr) {
		copied_text = *message.str;
		delete message.str;
	}

	switch (message.type) {
		case DrawMessage::CLEAR:
			core1_display.fillScreen(0x0000);
			break;
		case DrawMessage::RECT:
			core1_display.fillRect(
					message.rect.x,
					message.rect.y,
					message.rect.w,
					message.rect.h,
					message.rect.color);
			break;
		case DrawMessage::CURSOR:
			core1_display.setCursor(
					message.cursor.x,
					message.cursor.y);
			break;
		case DrawMessage::PRINT:
			core1_display.print(copied_text.c_str());
			break;
		case DrawMessage::TEXT:
			core1_draw_text(message.text, copied_text);
			break;
		case DrawMessage::CONFIG:
			core1_display.setTextSize(message.config.textSize);
			core1_display.setTextColor(message.config.textColor);
			core1_display.setFont(message.config.font);
		default:
			break;
	}
}
