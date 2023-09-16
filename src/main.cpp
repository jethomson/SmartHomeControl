//TODO
// use raspbery pi and hdmi-cec to get tv power state, play/pause state?
//      command_on: ssh pi@pi0.example.com "echo 'on 0' | cec-client -s"
//      command_off: ssh pi@pi0.example.com "echo 'standby 0' | cec-client -s"
//      command_state: ssh pi@pi0.example.com "echo 'pow 0' | cec-client -s |grep 'power status:'"
//      value_template: '{{ value == "power status: on" }}'


//HiLetgo ESP8266
// D0 = pin 16 --LED
// D1 = pin 5
// D2 = pin 4
// D3 = pin 0
// D4 = pin 2 --LED (in TASMOTA template, set GPIO 2 to Relay_i 1 for testing)
// D5 = pin 14
// D6 = pin 12
// D7 = pin 13
// D8 = pin 15
// D9 = pin 3
// D10 = pin 1


//these were supposed to correcty FastLED's glitchy behavior but didn't work for me
//#define FASTLED_ALLOW_INTERRUPTS 0
//#define FASTLED_INTERRUPT_RETRY_COUNT 1

#include <Arduino.h>

#include "credentials.h" // set const char *ssid and const char *password in include/credentials.h
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncHttpClient.h>

#include <time.h>
#include <TZ.h>

#include <AsyncPing.h>

#include <WebSerial.h>
#include <ESP_Mail_Client.h>

#ifdef ESP8266
#include <Updater.h>
//#include <ESP8266mDNS.h>
#define U_PART U_FS
#else
#include <Update.h>
#include <ESPmDNS.h>
#define U_PART U_SPIFFS
#endif
#include <ArduinoOTA.h>

#include <FastLED.h>
#include <NeoPixelBus.h>

// had to copy RCSwitch library folder to project's lib folder and
// change
// void RCSwitch::handleInterrupt() {
// to
// ICACHE_RAM_ATTR void RCSwitch::handleInterrupt() {
#include <RCSwitch.h>

#include <fauxmoESP.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>

#include <iostream>
#include <FS.h>
#include <LittleFS.h>

#include <string.h>
using namespace std;

#define RTC_UTC_TEST 1510592825  // 1510592825 = Monday 13 November 2017 17:07:05 UTC

//adjust these to your requirements
#define LOCAL_IP IPAddress(192, 168, 1, 60)
#define GATEWAY IPAddress(192, 168, 1, 1)
#define SUBNET_MASK IPAddress(255, 255, 255, 0)
#define DNS1 IPAddress(1, 1, 1, 1)
#define DNS2 IPAddress(9, 9, 9, 9)
const uint32_t PD_Phone_Check_Interval = 900000; // 15 minutes in ms. how often we look for presence of phone.
const uint32_t PD_On_Away_Power_Off_Delay = 600000; // 10 minutes in ms. only want to power off if gone for an extended period of time.
//const uint32_t PD_Phone_Check_Interval = 180000; // DEBUG. 3 minutes in ms. how often we look for presence of phone.
//const uint32_t PD_On_Away_Power_Off_Delay = 60000; // DEBUG. 1 minute in ms. only want to power off if gone for an extended period of time.
const uint32_t PD_WiFi_Connect_Delay = 120000; // 2 minutes in ms. allow time for phone to connect to WiFi.
const IPAddress phone_ip(192, 168, 1, 142);
const IPAddress tv_ip(192, 168, 1, 65);


//#define DEBUG_CONSOLE Serial
#define DEBUG_CONSOLE WebSerial
#ifdef DEBUG_CONSOLE
 #define DEBUG_PRINT(x)     DEBUG_CONSOLE.print (x)
 #define DEBUG_PRINTDEC(x)     DEBUG_CONSOLE.print (x, DEC)
 #define DEBUG_PRINTLN(x)  DEBUG_CONSOLE.println (x)
 #define DEBUG_PRINTF(...) DEBUG_CONSOLE.printf(__VA_ARGS__)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTDEC(x)
 #define DEBUG_PRINTLN(x)
 #define DEBUG_PRINTF(...)
#endif 

#define MAX_PARAMETER_SIZE 100 // maximum number of characters in command and arguments combined

#define LED 2 // onboard LED

#define DATA_PIN 2  // this project doesn't connect LEDs to this pin but setting DATA_PIN is still necessary for FastLED to run correctly
#define NUM_LEDS 12 // ring has 12 LEDs
#define OFFSET 6
#define COLOR_ORDER GRB

#define LED_STRIP_VOLTAGE 5
#define LED_STRIP_MILLIAMPS 20

#define PERIOD 50
#define TICS_PER_SEC (1000 / PERIOD)
#define WEEKDAY_WAKEUP_TIME 61200 //seconds after midnight
#define WEEKEND_WAKEUP_TIME 28800

#define BREATH 1

#define NUM_PINGS 5
#define PING_TIMEOUT 1000

#define TV_QUERY_URL "http://192.168.1.65:8060/query/device-info"

struct tm *local_now;
uint32_t tic = 0;
uint32_t num_tics_to_wakeup = 0;

bool clock_running = true;
bool time_update_needed = false;
bool set_sleep_time_flag = false;

CRGB leds[NUM_LEDS];
//NeoPixelBus ignores DATA_PIN for ESP8266
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(NUM_LEDS); //NeoPixelBus uses RX pin for ESP8266
uint8_t minute_colors[5] = {HUE_RED, HUE_YELLOW, HUE_GREEN, HUE_BLUE, HUE_PURPLE};
uint8_t max_brightness = 255;
const uint8_t min_brightness = 2;

RCSwitch rf_switch = RCSwitch();

AsyncPing PingPhone;
AsyncPing PingTV;

enum class PhoneStates {HERE, AWAY};
enum PhoneStates previous_phone_state = PhoneStates::AWAY;
enum PhoneStates phone_state = PhoneStates::AWAY;
bool phone_state_fresh = false;

//enum class PD_Triggers {NONE = 0, TIMER = 1, DOOR = 2};
enum class PD_Triggers {NONE = 0, PHONE = 1, DOOR = 2};
enum PD_Triggers pd_trigger = PD_Triggers::NONE;

SMTPSession smtp;

fauxmoESP fauxmo;
enum SmartCommand {SC_DO_NOTHING, SC_TV_ON, SC_TV_OFF, SC_PLAYPAUSE, SC_CLOCK_ON, SC_CLOCK_OFF};
SmartCommand sc = SC_DO_NOTHING;
bool flag_get_tv_state = true;
bool tv_state = false;
bool pause_state = false;

const uint16_t IR_LED_pin = 4;  // ESP8266 GPIO pin to use. Recommended: 4 (D2).
IRsend irsend(IR_LED_pin);  // Set the GPIO to be used to sending the message.
string ir_data = "";
uint8_t ir_repeat = 10;

AsyncWebServer web_server(80);
const char *PARAM_INPUT_1 = "cmnd";
string CORS_value = "";
string smart_devices_JSON_string;
AsyncHttpClient ahClient;
AsyncWebSocket *websocket;

int8_t group_power_flag = -1;
bool group_power_states[10] = {true}; // ten (0-9) groups max

bool restart_needed = false;


void handle_clock(void);
void handle_RF_command(void);
void handle_presence_detection(void);
void handle_smart_speaker_command(void);
void handle_group_power_command(void);
void handle_tv_command(void);
void get_tv_state(void);


void write_log(String);
void smtp_notify(const char*);
static void cb_tv_query_offline(void);
static void cb_tv_query_data(void *, AsyncClient *, void *, size_t);
void transmit_IR_data(void);
void http_cmnd(String);
size_t simple_JSON_parse(string, string, size_t, vector<string> &);
bool update_time(int8_t);
void set_sleep_time(void);
void show_leds(void);
void handle_flash_bin(AsyncWebServerRequest *, const String &, size_t, uint8_t *, size_t, bool);
string process_cmnd(char[]);


void OTA_server_initiate(void);
void smart_devices_config_load(void);
void web_server_initiate(void);
void webserial_initiate(void);
void fauxmo_initiate(void);
void clock_initiate(void);
void ping_initiate(void);


void handle_clock() {
	static uint16_t interval = PERIOD;
	static uint32_t pm = millis(); // previous millis
	uint32_t dt = 0;

	uint8_t hi = 0;
	uint8_t mi = 0;

	uint8_t br = 255;
	uint8_t bg = 255;

	//calling update_time() based on a flag allows async function to exit quicker and creates some consistency
	if (time_update_needed) {
		update_time(2);
	}

	if (set_sleep_time_flag) {
		set_sleep_time_flag = false;
		set_sleep_time();
	}

	dt = millis() - pm; // even when millis() overflows this still gives the correct time elapsed
	if (dt >= interval) {
		pm = millis();
		interval = PERIOD - (dt - interval);
		if (interval > PERIOD) {
			interval = PERIOD; // prevent underflow
		}
		tic++;

		if (clock_running) {
			// every 60 seconds call update_time()
			if (tic >= (60 * TICS_PER_SEC)) {
				// tic gets synced to seconds when update_time() succeeds but if update_time() fails tic will keep meeting the if condition
				// therefore we should reset tic here to prevent this block from running repeatedly
				tic = 0;
				update_time(2);

				// turn off clock display at midnight
				if (local_now->tm_hour == 0 && local_now->tm_min == 0) {
					clock_running = false;
					set_sleep_time();
				}
			}

			mi = ((local_now->tm_min / 5) + OFFSET) % NUM_LEDS;
			hi = (local_now->tm_hour + OFFSET) % NUM_LEDS;

			if (BREATH) {
				br = ((max_brightness - min_brightness) * abs((int8_t)(tic % TICS_PER_SEC) - (TICS_PER_SEC / 2))) / (TICS_PER_SEC / 2) + min_brightness;
				bg = (((80 * (uint16_t)max_brightness) / 100 - min_brightness) * abs((int8_t)(tic % TICS_PER_SEC) - (TICS_PER_SEC / 2))) / (TICS_PER_SEC / 2) + min_brightness;
			}
			else {
				if ((tic % TICS_PER_SEC) < (TICS_PER_SEC / 2)) {
					br = max_brightness;
					bg = (90 * (uint16_t)max_brightness) / 100;
				}
				else {
					br = 0;
					bg = 0;
				}
			}

			FastLED.clear();
			for (uint8_t i = 0; i < NUM_LEDS; i++) {
				// set 6, 9, 12, and 3 o'clock positions to white to act as a reference for reading the time in the dark
				// except when the minute hand is in that position to prevent its color mixing with white
				if ((i == 0 || i == 3 || i == 6 || i == 9) && (i != mi)) {
					leds[i] = CHSV(0, 0, max_brightness / 3);
				}
			}

			leds[hi] = CHSV(HUE_RED, 255, br);

			//use += so the color of the hour hand mixes with the minute hand
			leds[mi] += CHSV(HUE_GREEN, 255, bg);

			show_leds();
		}
		else {
			if (tic >= num_tics_to_wakeup) {
				//tic = 0; //what if update_time() fails?
				clock_running = true;
				time_update_needed = true;
			}
			FastLED.clear();
			show_leds();
		}
	}
}

void handle_RF_command() {
	static int last_good_value = 0;
	static uint32_t pm = millis(); // previous millis
	uint32_t dt = 0;

	if (rf_switch.available()) {
		int value = rf_switch.getReceivedValue();

		dt = millis() - pm;
		// compare to previous value to prevent repeats, override repeat check if enough time has passed
		if (value != last_good_value || dt >= 1500) {
			pm = millis();

			switch (value) {
			case 6902449: //left button
				last_good_value = value;
				http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20TOGGLE"); //kitchen
				break;
			case 6902456: //middle button
				last_good_value = value;
				//http_cmnd("http://192.168.1.51/cm?cmnd=POWER%20TOGGLE"); //hallway
				//http_cmnd("http://192.168.1.62/cm?cmnd=POWER%20TOGGLE"); //dining
				//http_cmnd("http://192.168.1.61/cm?cmnd=POWER%20TOGGLE"); //den
				http_cmnd("http://192.168.1.66/cm?cmnd=POWER%20TOGGLE"); //lamp
				break;
			case 6902450: //right button
				last_good_value = value;
				group_power_flag = 1;
				group_power_states[group_power_flag] = !group_power_states[group_power_flag];
				break;
			case 15723561: //front door - Sonoff DW1
				last_good_value = value;
				pd_trigger = PD_Triggers::DOOR;
				DEBUG_CONSOLE.print("DOOR: ");
				DEBUG_CONSOLE.println(value);
				break;
			case 0:
				break;
			default:
				DEBUG_CONSOLE.print("unknown RF: ");
				DEBUG_CONSOLE.println(value);
				break;
			}
		}
		rf_switch.resetAvailable();
	}
}


/*
void handle_presence_detection_OLD() {
	// We check for a phone by pinging its IP address at regular intervals (TIMER trigger).
	// If the phone is HERE we assume its owner is here.
	// We do not ping for the phone right when the door is opened because pinging can be slow and the phone may not be connected to WiFi yet.
	// Since we cannot rely on pinging to always give an immediate, correct phone state, we have to work with stale data, which makes the code tricky.

	//enum class PhoneStates {HERE, AWAY};
	static enum PhoneStates previous_phone_state = PhoneStates::AWAY;
	static enum PhoneStates phone_state = PhoneStates::AWAY;

	static bool door_opened_while_away = false;
	static bool enable_welcome_light = false;

	static uint32_t t1_interval = 0; // default to zero so check for phone happens on first call
	static uint32_t t1_pm = millis(); // timer 1 previous millis
	//static uint32_t t2_pm = millis();
	static uint32_t door_opened_pm = 0;
	static bool enable_away_power_off = false;
	uint32_t dt = 0;

	dt = millis() - t1_pm; // even when millis() overflows this still gives the correct time elapsed
	if (pd_trigger == PD_Triggers::NONE && dt >= t1_interval) {
		// normally we check every PD_Phone_Check_Interval but if the DOOR is opened we override the normal check cycle
		t1_pm = millis();
		t1_interval = PD_Phone_Check_Interval;
		pd_trigger = PD_Triggers::TIMER;
	}

	dt = millis() - door_opened_pm;
	if (enable_away_power_off && dt >= PD_On_Away_Power_Off_Delay) {
		enable_away_power_off = false;
		door_opened_pm = 0;
		//http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20OFF"); //kitchen. for testing.

		phone_state = check_for_phone();
		if (phone_state == PhoneStates::AWAY) {
			char buf[] = "Group0 OFF"; //everything off
			process_cmnd(buf);
			DEBUG_PRINTLN("pd: away power off");
			write_log("pd: away power off");
		}
	}

	if (pd_trigger == PD_Triggers::TIMER) {
		previous_phone_state = phone_state;
		phone_state = check_for_phone();
		if (phone_state == PhoneStates::AWAY) {
			enable_welcome_light = true;

			// phone was here but now away so turn everything off.
			// checking previous state ensures everything is only turned off once.
			if (previous_phone_state == PhoneStates::HERE) {
				// it is possible to miss a door open trigger
				// if that happens set door_opened_pm to current millis()
				if (door_opened_pm == 0) {
					door_opened_pm = millis();
				}
				enable_away_power_off = true;
				DEBUG_PRINTLN("pd: away");
				write_log("pd: away");
			}

			if (door_opened_while_away) {
				door_opened_while_away = false; // clear flag, so multiple entries can be logged while away.
				DEBUG_PRINTLN("pd: DOOR OPENED WHILE AWAY");
				write_log("pd: DOOR OPENED WHILE AWAY");
				smtp_notify(smtp_host, smtp_port, author_email, author_password, recipient_email, "pd: DOOR OPENED WHILE AWAY");

			}
		}
		else if (phone_state == PhoneStates::HERE) {
			door_opened_while_away = false;
			if (previous_phone_state == PhoneStates::AWAY) {
				// cannot do much here because we cannot be certain if phone was detected as HERE before entering (triggering DOOR)
				DEBUG_PRINTLN("pd: here");
				write_log("pd: here");
			}
			else {
				enable_welcome_light = false;
				enable_away_power_off = false;
			}
		}
	}
	else if (pd_trigger == PD_Triggers::DOOR) {
		door_opened_pm = millis();

		// door was opened so check for phone using a TIMER trigger after PD_WiFi_Connect_Delay.
		// the delay gives time for phone to connect/drop WiFi (arriving/leaving).
		// if the delay is not long enough false DOOR OPENED WHILE AWAY messages will be logged
		t1_pm = millis();
		t1_interval = PD_WiFi_Connect_Delay;

		// cannot use (phone_state == PhoneStates::AWAY) reliably to detect entering because the phone may have been detected before entering
		if (enable_welcome_light) {
			enable_welcome_light = false;
			http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20ON"); //kitchen
			http_cmnd("http://192.168.1.66/cm?cmnd=POWER%20ON"); //lamp
			http_cmnd("http://192.168.1.64/cm?cmnd=POWER%20ON"); //raspberry pi outlet


			//process_cmnd("POWERCLOCK ON"); //compiles but does not execute. process_cmnd manipulates buf in place so it cannot be used on a constant string
			char buf[] = "POWERCLOCK ON";
			process_cmnd(buf);
		}

		if (phone_state == PhoneStates::AWAY) {
			// this will only log a warning message if phone is not detected as HERE the next time it is pinged
			door_opened_while_away = true;
		}

		DEBUG_PRINTLN("pd: door opened");
		write_log("pd: door opened");
		//http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20TOGGLE"); //for testing
	}

	pd_trigger = PD_Triggers::NONE;
}
*/

/*
void handle_presence_detection_WORKS() {
	// We check for a phone by pinging its IP address at regular intervals (TIMER trigger).
	// If the phone is HERE we assume its owner is here.
	// We do not ping for the phone right when the door is opened because pinging can be slow and the phone may not be connected to WiFi yet.
	// Since we cannot rely on pinging to always give an immediate, correct phone state, we have to work with stale data, which makes the code tricky.

	static bool door_opened_while_away = false;
	static bool enable_welcome_light = false;

	static uint32_t t1_interval = 0; // default to zero so check for phone happens on first call
	static uint32_t t1_pm = millis(); // timer 1 previous millis
	//static uint32_t t2_pm = millis();
	static uint32_t door_opened_pm = 0;
	static bool enable_away_power_off = false;
	uint32_t dt = 0;


	dt = millis() - t1_pm; // even when millis() overflows this still gives the correct time elapsed
	//if (dt >= t1_interval) {
	if (pd_trigger == PD_Triggers::NONE && dt >= t1_interval) {
		t1_pm = millis();
		// if the DOOR is opened t1_interval was shortened, so need to restore t1_interval in case it was changed
		t1_interval = PD_Phone_Check_Interval;
		//pd_trigger = PD_Triggers::TIMER;

		// try to ping up to 5 times with a timeout of 1 second.
		// if a response is received any pings requests remaining in the queue will be canceled
		// trying up to 5 times assures the phone is actually not here instead of a ping being missed
		PingPhone.begin(phone_ip, 5, 1000);
	}

	dt = millis() - door_opened_pm;
	if (enable_away_power_off && dt >= PD_On_Away_Power_Off_Delay) {
		enable_away_power_off = false;
		door_opened_pm = 0;
		//http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20OFF"); //kitchen. for testing.

		//phone_state = check_for_phone();
		if (phone_state == PhoneStates::AWAY) {
			char buf[] = "Group0 OFF"; //everything off
			process_cmnd(buf);
			DEBUG_PRINTLN("pd: away power off");
			write_log("pd: away power off");
		}
	}

	if (pd_trigger == PD_Triggers::PHONE) {
		if (phone_state == PhoneStates::AWAY) {
			enable_welcome_light = true;

			// phone was here but now away so turn everything off.
			// checking previous state ensures everything is only turned off once.
			if (previous_phone_state == PhoneStates::HERE) {
				// it is possible to miss a door open trigger
				// if that happens set door_opened_pm to current millis()
				if (door_opened_pm == 0) {
					door_opened_pm = millis();
				}
				enable_away_power_off = true;
				DEBUG_PRINTLN("pd: away");
				write_log("pd: away");
			}

			if (door_opened_while_away) {
				door_opened_while_away = false; // clear flag, so multiple entries can be logged while away.
				DEBUG_PRINTLN("pd: DOOR OPENED WHILE AWAY");
				write_log("pd: DOOR OPENED WHILE AWAY");
				smtp_notify("pd: DOOR OPENED WHILE AWAY");
			}
		}
		else if (phone_state == PhoneStates::HERE) {
			door_opened_while_away = false;
			if (previous_phone_state == PhoneStates::AWAY) {
				// cannot do much here because we cannot be certain if phone was detected as HERE before entering (triggering DOOR)
				DEBUG_PRINTLN("pd: here");
				write_log("pd: here");
			}
			else {
				enable_welcome_light = false;
				enable_away_power_off = false;
			}
		}
	}
	else if (pd_trigger == PD_Triggers::DOOR) {
		door_opened_pm = millis();

		// door was opened so check for phone using a TIMER trigger after PD_WiFi_Connect_Delay.
		// the delay gives time for phone to connect/drop WiFi (arriving/leaving).
		// if the delay is not long enough false DOOR OPENED WHILE AWAY messages will be logged
		t1_pm = millis();
		t1_interval = PD_WiFi_Connect_Delay;

		// cannot use (phone_state == PhoneStates::AWAY) reliably to detect entering because the phone may have been detected before entering
		if (enable_welcome_light) {
			enable_welcome_light = false;
			http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20ON"); //kitchen
			http_cmnd("http://192.168.1.66/cm?cmnd=POWER%20ON"); //lamp
			http_cmnd("http://192.168.1.64/cm?cmnd=POWER%20ON"); //raspberry pi outlet


			//process_cmnd("POWERCLOCK ON"); //compiles but does not execute. process_cmnd manipulates buf in place so it cannot be used on a constant string
			char buf[] = "POWERCLOCK ON";
			process_cmnd(buf);
		}

		if (phone_state == PhoneStates::AWAY) {
			// this will only log a warning message if phone is not detected as HERE the next time it is pinged
			door_opened_while_away = true;
		}

		DEBUG_PRINTLN("pd: door opened");
		write_log("pd: door opened");
		//http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20TOGGLE"); //for testing
	}

	pd_trigger = PD_Triggers::NONE;
}
*/
 
void handle_presence_detection() {
	// We check for a phone by pinging its IP address at regular intervals.
	// If the phone is HERE we assume its owner is here.
	// We do not ping for the phone right when the door is opened because pinging can be slow and the phone may not be connected to WiFi yet.
	// Since we cannot rely on pinging to always give an immediate, correct phone state, we have to work with stale data, which makes the code tricky.

	static bool door_opened_while_away = false;
	static bool enable_welcome_light = false;

	static uint32_t interval = 0; // default to zero so check for phone happens on first call
	static uint32_t pm = millis(); // previous millis
	static bool enable_away_power_off = false;
	uint32_t dt = 0;


	dt = millis() - pm; // even when millis() overflows this still gives the correct time elapsed
	if (pd_trigger == PD_Triggers::NONE && dt >= interval) {
		pm = millis();
		// if the DOOR is opened interval was shortened, so need to restore interval in case it was changed
		interval = PD_Phone_Check_Interval;

		// try to ping up to 5 times with a timeout of 1 second.
		// if a response is received any pings requests remaining in the queue will be cancelled
		// trying up to 5 times assures the phone is actually not here instead of a ping being missed
		PingPhone.begin(phone_ip, NUM_PINGS, PING_TIMEOUT);
	}


	if (pd_trigger == PD_Triggers::PHONE) {
		if (phone_state == PhoneStates::AWAY) {
			enable_welcome_light = true;

			if (previous_phone_state == PhoneStates::HERE) {
				// phone was HERE but now AWAY so allow power off and start power off timer
				// checking previous state ensures everything is only turned off once.
				enable_away_power_off = true;
				pm = millis();
				interval = PD_On_Away_Power_Off_Delay;
				DEBUG_PRINTLN("pd: away");
				write_log("pd: away");
			}
			else if (previous_phone_state == PhoneStates::AWAY) {
				// phone was found to be AWAY twice so assume it is not returning soon and power everything off
				if (enable_away_power_off) {
					enable_away_power_off = false;
					char buf[] = "Group0 OFF"; //everything off
					process_cmnd(buf);
					DEBUG_PRINTLN("pd: away power off");
					write_log("pd: away power off");
				}
			}

			if (door_opened_while_away) {
				door_opened_while_away = false; // clear flag, so multiple entries can be logged while away.
				DEBUG_PRINTLN("pd: DOOR OPENED WHILE AWAY");
				write_log("pd: DOOR OPENED WHILE AWAY");
				//smtp_notify(smtp_host, smtp_port, author_email, author_password, recipient_email, "pd: DOOR OPENED WHILE AWAY");
				smtp_notify("pd: DOOR OPENED WHILE AWAY");
			}
		}
		else if (phone_state == PhoneStates::HERE) {
			door_opened_while_away = false;

			if (previous_phone_state == PhoneStates::AWAY) {
				// cannot do much here because we cannot be certain if phone was detected as HERE before entering (triggering DOOR)
				DEBUG_PRINTLN("pd: here");
				write_log("pd: here");
			}
			else if (previous_phone_state == PhoneStates::HERE) {
				enable_welcome_light = false;
				enable_away_power_off = false;
			}
		}
	}
	else if (pd_trigger == PD_Triggers::DOOR) {
		// door was opened so check for phone after PD_WiFi_Connect_Delay.
		// the delay gives time for phone to connect/drop WiFi (arriving/leaving).
		// if the delay is not long enough false DOOR OPENED WHILE AWAY messages will be logged
		pm = millis();
		interval = PD_WiFi_Connect_Delay;

		// cannot use (phone_state == PhoneStates::AWAY) reliably to detect entering because the phone may have been detected before entering
		if (enable_welcome_light) {
			enable_welcome_light = false;
			http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20ON"); //kitchen
			http_cmnd("http://192.168.1.66/cm?cmnd=POWER%20ON"); //lamp
			//http_cmnd("http://192.168.1.64/cm?cmnd=POWER%20ON"); //raspberry pi outlet

			//process_cmnd("POWERCLOCK ON"); //compiles but does not execute. process_cmnd manipulates buf in place so it cannot be used on a constant string
			char buf[] = "POWERCLOCK ON";
			process_cmnd(buf);
		}

		if (phone_state == PhoneStates::AWAY) {
			// this will only log a warning message if phone is not detected as HERE the next time it is pinged
			door_opened_while_away = true;
		}

		DEBUG_PRINTLN("pd: door opened");
		write_log("pd: door opened");
		//http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20TOGGLE"); //for testing
	}

	pd_trigger = PD_Triggers::NONE;
}


void handle_smart_speaker_command() {
	switch (sc) {
	case SC_TV_ON:
		ir_data = "8A"; // discrete TV Power ON
		break;
	case SC_TV_OFF:
		ir_data = "18"; // discrete TV Power OFF
		break;
	case SC_PLAYPAUSE:
		ir_data = "32"; // TV Play/Pause
		break;
	case SC_CLOCK_ON:
		clock_running = true;
		time_update_needed = true;
		break;
	case SC_CLOCK_OFF:
		clock_running = false;
		set_sleep_time();
		break;
	default:
		break;
	}
	sc = SC_DO_NOTHING;
}


void handle_group_power_command() {
	//digitalWrite(LED, digitalRead(LED) == HIGH ? LOW : HIGH);

	vector<string> member_states; //possible power states for a group member
	vector<string> power_cmnds;
	size_t pos = 0;

	if (group_power_flag >= 0) {
		char numstr[3];
		string group_key = string("group") + itoa(group_power_flag, numstr, 10);
		string switch_key;
		string group_state;
		//DEBUG_PRINTLN(key.c_str());

		if (group_power_states[group_power_flag]) {
			switch_key = "switch_on";
			group_state = "on";
		}
		else {
			switch_key = "switch_off";
			group_state = "off";
		}

		while((pos = simple_JSON_parse(smart_devices_JSON_string, group_key, pos, member_states)) != string::npos);
		pos = 0;
		while((pos = simple_JSON_parse(smart_devices_JSON_string, switch_key, pos, power_cmnds)) != string::npos);


		//for debugging
		//member_states.clear();
		//member_states.push_back("off");
		//member_states.push_back("off");
		//power_cmnds.clear();
		//power_cmnds.push_back("http://192.168.1.60/cm?cmnd=PowerClock%20OFF");
		//power_cmnds.push_back("http://192.168.1.43/cm?cmnd=POWER1%20TOGGLE");
		//power_cmnds.push_back("http://192.168.1.31:8089/cm?cmnd=Power%20OFF");


		for (unsigned int i = 0; i < power_cmnds.size(); i++) {
			for (auto& c : member_states[i]) {
				c = tolower(c);
			}
			//DEBUG_PRINTLN(member_states[i].c_str());
			if (member_states[i].find(group_state) != string::npos) {
				// this group member has an allowed state that matches the desired group state
				// e.g. group member allows "off" and the desired group state is "off"
				// if group member state was "onoff" there would also be a match

				if ( power_cmnds[i].find( WiFi.localIP().toString().c_str() ) == string::npos ) {
					// address is not on this device so use http_cmnd
					http_cmnd(power_cmnds[i].c_str());
				}
				else {
					// sending web commands from device to the same device doesn't work, so execute locally
					DEBUG_CONSOLE.println("execute locally");
					string s = power_cmnds[i];
					size_t start = s.find("=");
					if (start != string::npos && start < s.length()-1 ) {
						s = s.substr(start+1);
						start = s.find("%20");
						if (start != string::npos) {
							s.replace(start, 3, " ");
						}
						DEBUG_CONSOLE.println(s.c_str());

						char buf[s.length()+1] = "";
						strcpy(buf, s.c_str());
						process_cmnd(buf);
					}
					else {
						DEBUG_CONSOLE.println("invalid command");
					}
				}
			}
		}
		group_power_flag = -1;
	}
}

// the normal mode of operation is for the client's browser to query all of the devices
// however the TV's web sever does set CORS in the header so the browser blocks the result of the device-info query
// as a workaround we use our SmartHomeControl device to make the query for us, since it does not respect CORS
//void get_tv_state() {
//	if (flag_get_tv_state) {
//		// when TV server is offline GET takes too long to timeout, so use ping to check if the server is online before attempting GET
//		if (Ping.ping(tv_ip, 1)) {
//			ahClient.init("GET", "http://192.168.1.65:8060/query/device-info", &cb_tv_query_data, &cb_tv_query_offline);
//			ahClient.send();
//			// callback will set tv_state
//		}
//		else {
//			tv_state = false;
//		}
//		flag_get_tv_state = false;
//	}
//}


void get_tv_state() {
	const uint32_t interval = 2000; 
	static uint32_t pm = millis(); // previous millis
	uint32_t dt = 0;

	//the client should limit how frequently get_tv_state() is called but let's limit it here too to be safe
	dt = millis() - pm;
	if (flag_get_tv_state && dt >= interval) {
		flag_get_tv_state = false;
		pm = millis();
		// when TV server is offline GET takes too long to timeout, so use ping to check if the server is online before attempting GET
		// if a ping answered then PingTV's callback will do a GET on the TV's query URL to determine the TV's state
		PingTV.begin(tv_ip, NUM_PINGS, PING_TIMEOUT);
	}
}


void write_log(String data) {
	File f = LittleFS.open("/access_logC.txt", "r");
	if (f) {
		size_t fsize = f.size();
		f.close();
		//if fsize is too big the access log will include garbage
		//if (fsize > 2000) { // corruption.
		//if (fsize > 1900) { // seems ok
		// if full, rotate log files
		if (fsize > 1750) { // about 100 lines, no corruption.
			LittleFS.remove("/access_logP.txt");
			LittleFS.rename("/access_logC.txt", "/access_logP.txt");
		}
	}
	

	f = LittleFS.open("/access_logC.txt", "a");
	if (f) {
		//time_t epochTime = timeClient.getEpochTime();
		//struct tm *ptm = gmtime(&epochTime);
		update_time(2);
		char ts[23];
		snprintf(ts, sizeof ts, "%d/%02d/%02d %02d:%02d:%02d - ", local_now->tm_year+1900, local_now->tm_mon+1, local_now->tm_mday, local_now->tm_hour, local_now->tm_min, local_now->tm_sec);
		
		//write_log crashes if it gets interrupted here. not sure why.
		noInterrupts();
		f.print(ts);

		f.print(data);
		f.print("\n");
		delay(1); //works without this. need it?? makes sure access time is changed??
		f.close();
		interrupts();
	}
}


// need to look into certs as related to SMTP
// const char rootCACert[] PROGMEM = "-----BEGIN CERTIFICATE-----\n"
//                                   "-----END CERTIFICATE-----\n";
//void smtp_notify(const char* smtp_host, const int smtp_port, const char* author_email, const char* author_password, const char* recipient_email, const char* body) {
void smtp_notify(const char* body) {
	Session_Config config;
	config.server.host_name = smtp_host;
	config.server.port = smtp_port;
	config.login.email = author_email;
	config.login.password = author_password;
	config.login.user_domain = F("127.0.0.1");

	//If non-secure port is prefered (not allow SSL and TLS connection), use
	//config.secure.mode = esp_mail_secure_mode_nonsecure;

	//If SSL and TLS are always required, use
	//config.secure.mode = esp_mail_secure_mode_ssl_tls;

	//To disable SSL permanently (use less program space), define ESP_MAIL_DISABLE_SSL in ESP_Mail_FS.h
	//or Custom_ESP_Mail_FS.h

	//config.secure.mode = esp_mail_secure_mode_nonsecure;

	SMTP_Message message;
	message.sender.name = F("SmartHome Notifier");
	message.sender.email = author_email;

	message.subject = F("SmartHome Notification");
	message.addRecipient(F("User"), recipient_email);

	message.text.content = body;
	message.text.charSet = F("us-ascii");
	message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
	message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;
	message.addHeader(message_id);

	//smtp.setSystemTime(1693876380, 16);
	//time is set else using the time library's configTime()

	if (!smtp.connect(&config)) {
		MailClient.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
	return;
	}

	if (!smtp.isLoggedIn()) {
		DEBUG_PRINTLN("\nNot yet logged in.");
	}
	else {
		if (smtp.isAuthenticated()) {
			DEBUG_PRINTLN("\nSuccessfully logged in.");
		}
		else {
			DEBUG_PRINTLN("\nConnected with no Auth.");
		}
	}

	MailClient.sendMail(&smtp, &message);
	smtp.sendingResult.clear();
}


static void cb_tv_query_offline(void) {
	tv_state = false;
}


static void cb_tv_query_data(void *arg, AsyncClient *c, void *data, size_t len) {
	const uint8_t target_on_len = 7;
	const uint8_t target_on[target_on_len] = {'P', 'o', 'w', 'e', 'r', 'O', 'n'};
	const uint8_t target_off_len = 10;
	const uint8_t target_off[target_off_len] = {'D', 'i', 's', 'p', 'l', 'a', 'y', 'O', 'f', 'f'};
	static uint8_t match_score_on = 0;
	static uint8_t match_score_off = 0;

	uint8_t *d = (uint8_t *)data;
	for (size_t i = 0; i < len; i++) {
		if (d[i] == target_on[match_score_on]) {
			match_score_on++;
		}
		else {
			match_score_on = 0;
		}

		if (match_score_on == target_on_len) {
			tv_state = true;
			match_score_on = 0;
			match_score_off = 0;
			if (c) {
				c->close(true);
			}
			return;
		}

		if (d[i] == target_off[match_score_off]) {
			match_score_off++;
		}
		else {
			match_score_off = 0;
		}

		if (match_score_off == target_off_len) {
			tv_state = false;
			match_score_on = 0;
			match_score_off = 0;
			if (c) {
				c->close(true);
			}
			return;
		}
	}
}






void transmit_IR_data() {
	if (ir_data != "") {
		uint32_t ulircode = strtoul(ir_data.c_str(), NULL, 16);
		irsend.sendNEC(0x57E30000 + ((uint32_t)ulircode<<8) + ~(0xFFFFFF00 + ulircode));
		ir_data = "";
	}
}


void http_cmnd(String URL) {
	if (WiFi.status() == WL_CONNECTED) {
		ahClient.init("GET", URL);
		ahClient.send();
	}
}


size_t simple_JSON_parse(string s, string key, size_t start_pos, vector<string> &output) {
	size_t end_pos = string::npos;

	size_t nls = s.find("{\"", start_pos);
	size_t nle = s.find("\"}", start_pos)+1;
	if (nls != string::npos && nle != string::npos) {
		string line = s.substr(nls, nle-nls+1);
		//Serial.println(line.c_str());

		size_t match_start = 0;
		size_t match_end = 0;
		string delimiter = "\"" + key + "\":\""; // fragile, config file can't have any space around :

		if ((match_start = line.find(delimiter)) != string::npos) {
			if ((match_end = line.find("\"", match_start+delimiter.length())) != string::npos) {
				output.push_back(line.substr(match_start+delimiter.length(), match_end-(match_start+delimiter.length())));
			}
			else {
				output.push_back("");
			}
		}
		else {
			output.push_back("");
		}
		end_pos = nle;
	}

	return end_pos;
}


bool update_time(int8_t num_attempts) {
	if (num_attempts >= 0) {
		time_t utc_now;
		utc_now = time(nullptr);

		// create some delay before printing
		while (num_attempts > 0 && utc_now < RTC_UTC_TEST) {
			delay(500);
			time(&utc_now);
			num_attempts--;
		}

		if (num_attempts == 0) {
			return false;
		}

		local_now = localtime(&utc_now);

		//DEBUG_PRINT("ut: ");
		//DEBUG_PRINTLN(timeClient.getFormattedTime());
		time_update_needed = false;
		tic = TICS_PER_SEC * local_now->tm_sec;

		if (local_now->tm_hour > 23 || local_now->tm_min > 59 || local_now->tm_sec > 59) {
			delay(2000);
			ESP.restart();
		}
	}

	return true;
}


void set_sleep_time() {
	update_time(2);
	tic = 0;
	uint32_t seconds_since_midnight = 3600*local_now->tm_hour + 60*local_now->tm_min + local_now->tm_sec;
	uint8_t day_of_the_week = local_now->tm_wday;

	if (day_of_the_week == 1 || day_of_the_week == 2 || day_of_the_week == 3 || day_of_the_week == 4) { // Monday-Thursday
		if (seconds_since_midnight > (uint32_t) WEEKDAY_WAKEUP_TIME) {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKDAY_WAKEUP_TIME + 86400 - seconds_since_midnight);
		}
		else {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKDAY_WAKEUP_TIME - seconds_since_midnight);
		}
	}
	else if (day_of_the_week == 5) { // Friday
		if (seconds_since_midnight > (uint32_t) WEEKDAY_WAKEUP_TIME) {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKEND_WAKEUP_TIME + 86400 - seconds_since_midnight);
		}
		else {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKDAY_WAKEUP_TIME - seconds_since_midnight);
		}
	}
	else if (day_of_the_week == 6) { //Saturday
		if (seconds_since_midnight > (uint32_t) WEEKEND_WAKEUP_TIME) {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKEND_WAKEUP_TIME + 86400 - seconds_since_midnight);
		}
		else {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKEND_WAKEUP_TIME - seconds_since_midnight);
		}
	}
	else if (day_of_the_week == 0) { // Sunday
		if (seconds_since_midnight > (uint32_t) WEEKEND_WAKEUP_TIME) {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKDAY_WAKEUP_TIME + 86400 - seconds_since_midnight);
		}
		else {
			num_tics_to_wakeup = TICS_PER_SEC * (WEEKEND_WAKEUP_TIME - seconds_since_midnight);
		}
	}
}


void show_leds() {
		//FastLED.show() produces glitchy results so use NeoPixel Show() instead
		RgbColor pixel;
		for (int i = 0; i < NUM_LEDS; i++) {
			pixel = RgbColor(leds[i].r, leds[i].g, leds[i].b);
			strip.SetPixelColor(i, pixel);
		}
		strip.Show();
}


void handle_flash_bin(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
	//ESP.wdtDisable();

	if (!index) {
		size_t filesize = 0;

		// contentLength() includes the count of bytes in bin file plus the bytes in the header and footer.
		// these extra bytes will cause the writing of a filesystem to fail
		// so use a separate form input on the upload page to send the actual filesize alongside the bin file
		if (request->hasParam("filesize", true)) {
			filesize = request->getParam("filesize", true)->value().toInt();
		}
		else {
			// this doesn't work for spiffs or littlefs 
			filesize = request->contentLength();
		}

		// if filename includes littlefs, update the file system partition
		int cmd = (filename.indexOf("littlefs") > -1) ? U_PART : U_FLASH;
#ifdef ESP8266
		Update.runAsync(true);
		if (!Update.begin(filesize, cmd)) {
#else
		if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
#endif
			Update.printError(Serial);
		}
	}

	if (Update.write(data, len) != len) {
		Update.printError(Serial);
#ifdef ESP8266
	}
	else {
		Serial.printf("Progress: %d%%\n", (Update.progress() * 100) / Update.size());
#endif
	}

	if (final) {
		request->send(LittleFS, "/restart.html");

		if (!Update.end(true)) {
			Update.printError(Serial);
		}
		else {
			Serial.println("Update complete");
			//DEBUG_CONSOLE.println("Update complete");
			Serial.flush();
			restart_needed = true;
		}
	}
}


string process_cmnd(char buf[]) {

	//DEBUG_PRINTLN("process_cmnd");
	char empty[1] = "";
	char *cmnd = NULL;
	char *value = NULL;
	char *up = NULL;
	string message = "{";

	cmnd = strtok(buf, " ");
	value = strtok(NULL, " ");

	if (cmnd == NULL) {
		cmnd = empty;
		value = empty;
	}
	if (value == NULL) {
		value = empty;
	}

	up = cmnd;
	while (*up) {
		*up = toupper(*up);
		up++;
	}

	if (strcmp(cmnd, "CORS") != 0) {
		up = value;
		while (*up) {
			*up = toupper(*up);
			up++;
		}
	}

	if (strcmp(cmnd, "IR") == 0) {
		if (strcmp(value, "") != 0) {
			ir_data = value;
			message += "\"IR\":\"" + ir_data;
			message += "\"}";
		}
		else {
			message += "\"IR\":\"Unknown";
			message += "\"}";
		}
	}
	else if (strcmp(cmnd, "POWERTV") == 0) {
		if (strcmp(value, "ON") == 0) {
			ir_data = "8A";
			message += "\"POWERTV\":\"ON";
			message += "\"}";
		}
		else if (strcmp(value, "OFF") == 0) {
			ir_data = "18";
			message += "\"POWERTV\":\"OFF";
			message += "\"}";
		}
		else {
			flag_get_tv_state = true;
			message += "\"POWERTV\":\"" + string((tv_state) ? "ON" : "OFF");
			message += "\"}";
		}
	}
	else if (strcmp(cmnd, "POWERCLOCK") == 0) {
		if (strcmp(value, "ON") == 0) {
			clock_running = true;
			time_update_needed = true;
		}
		else if (strcmp(value, "OFF") == 0) {
			clock_running = false;
			set_sleep_time_flag = true;
		}
		else if (strcmp(value, "TOGGLE") == 0) {
			clock_running = !clock_running;
			if (clock_running) {
				time_update_needed = true;
			}
			else {
				set_sleep_time_flag = true;
			}
		}
		message += "\"POWERCLOCK\":\"" + string((clock_running) ? "ON" : "OFF");
		message += "\"}";
	}
	else if (strncmp(cmnd, "GROUP", 5) == 0) {
		//group_power_flag = strtol(&cmnd[5], nullptr, 10); // more than ten (0-9) groups
		group_power_flag = cmnd[5] - '0'; // ten (0-9) groups max

		if (strcmp(value, "ON") == 0) {
			group_power_states[group_power_flag] = true;
		}
		else if (strcmp(value, "OFF") == 0) {
			//digitalWrite(LED, digitalRead(LED) == HIGH ? LOW : HIGH);
			group_power_states[group_power_flag] = false;
		}
		else if (strcmp(value, "TOGGLE") == 0) {
			group_power_states[group_power_flag] = !group_power_states[group_power_flag];
		}
		message += "\"GROUP";
		message += cmnd[5];
		message += "\":\"";
		message += string((group_power_states[group_power_flag]) ? "ON" : "OFF");
		message += "\"}";
	}
	else if (strcmp(cmnd, "CORS") == 0) {
		if (strcmp(value, "") != 0) {
			CORS_value = value;
		}
		message += "\"CORS\":\"" + CORS_value;
		message += "\"}";
	}
	else {
		//message += "\"ERROR\":\"" + inputMessage;
		message += "\"Command\":\"Unknown";
		message += "\"}";
	}
	return message;
}


void OTA_server_initiate() {
	ArduinoOTA.onStart([]() {
		String type;
		if (ArduinoOTA.getCommand() == U_FLASH) {
			type = "sketch";
		} else { // U_FS
			type = "filesystem";
		}

		// NOTE: if updating FS this would be the place to unmount FS using FS.end()
		Serial.println("Start updating " + type);
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) {
			Serial.println("Auth Failed");
		} else if (error == OTA_BEGIN_ERROR) {
			Serial.println("Begin Failed");
		} else if (error == OTA_CONNECT_ERROR) {
			Serial.println("Connect Failed");
		} else if (error == OTA_RECEIVE_ERROR) {
			Serial.println("Receive Failed");
		} else if (error == OTA_END_ERROR) {
			Serial.println("End Failed");
		}
	});
	ArduinoOTA.begin();
}


void smart_devices_config_load() {
	File f = LittleFS.open("/smart_devices.json", "r");
	if (f) {
		smart_devices_JSON_string = f.readString().c_str();
		f.close();
	}
	else {
		Serial.println("Failed to open config file");
	}
}


void web_server_initiate() {
	web_server.on("/cm", HTTP_GET, [](AsyncWebServerRequest *request) {
		string message;
		if (request->hasParam(PARAM_INPUT_1)) {
			String inputMessage = request->getParam(PARAM_INPUT_1)->value();
			unsigned int len = inputMessage.length();
			len = (len < MAX_PARAMETER_SIZE) ? len : MAX_PARAMETER_SIZE;
			char buf[len+1];
			inputMessage.toCharArray(buf, len+1); // toCharArray copies inputMessage then adds \0 or the first MAX_PARAMETER_SIZE chars from inputMessage then adds \0
			message = process_cmnd(buf);
		}
		else {
			message = "{}";
		}

		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", message.c_str());
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});


	web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		//request->send_P(200, "text/html", index_html);
		request->send(LittleFS, "/index.html");
	});

	web_server.on("/smart_devices.json", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", smart_devices_JSON_string.c_str());
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});

	web_server.on("/TV", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/TV_IR.html");
	});

	web_server.on("/TV_ECP", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/TV_ECP.html");
	});

	web_server.on("/console", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/console.html");
	});

	web_server.on("/access_log", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/access_log.html");
	});

	web_server.on("/access_logP.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
		//access_logP.txt might not exist yet, so prepare a blank string to return
		String content = "";
		File f = LittleFS.open("/access_logP.txt", "r");
		if (f) {
			content = f.readString().c_str();
			f.close();
		}
		AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", content);
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});

	web_server.on("/access_logC.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
		String content = "";
		File f = LittleFS.open("/access_logC.txt", "r");
		if (f) {
			content = f.readString().c_str();
			f.close();
		}
		AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", content);
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});

	//OTA update through web interface
	web_server.on("/select_bin", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/select_bin.html");
	});

	web_server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/restart.html");
		restart_needed = true;
	});

	//OTA update via web page
	//AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload);
	web_server.on("/flash_bin", HTTP_POST, [](AsyncWebServerRequest *request) {}, handle_flash_bin);

	// Using an older version of fauxmo library because I want smart sockets not smart bulbs.
	// Using fauxmo smart sockets lets me tell Alexa to turn off all the lights without affecting all fauxmo devices.
	// The following are for the newer fauxmo library:
	//// fauxmo: These two callbacks are required for gen1 and gen3 compatibility
	//web_server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
	//	if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), String((char *)data)))
	//		return;
	//	// Handle any other body request here...
	//});
	//web_server.onNotFound([](AsyncWebServerRequest *request) {
	//	String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
	//	if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), body))
	//		return;
	//	request->send_P(404, "text/html", "404");
	//});

	web_server.begin();
}


void webserial_initiate() {
	websocket = new AsyncWebSocket("/consolews");
	websocket->onEvent([&](AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) -> void {
		if (type == WS_EVT_DATA) {
			len = (len < MAX_PARAMETER_SIZE) ? len : MAX_PARAMETER_SIZE;
			char buf[len+1];
			uint8_t i = 0;
			for(i = 0; i < len; i++){
				buf[i] = char(data[i]);
			}
			buf[i] = '\0';

			//write_log(buf);
			//can get sent #c or #c @P (not sure what @P means or why it is sent)
			if (strncmp(buf, "#c", 2) != 0) {
				process_cmnd(buf);
			}
			else {
				//received #c (call) reply with #r (response)
				WebSerial.print("#r");
			}
		}
	});

	web_server.addHandler(websocket);
	WebSerial._ws = websocket;
}


//using an older version of Fauxmo library because I want smart sockets not smart bulbs. Using fauxmo smart sockets lets me tell Alexa to turn off all the lights without affecting fauxmo devices.
void fauxmo_initiate() {
	// You have to call enable(true) once you have a WiFi connection
	// You can enable or disable the library at any moment
	// Disabling it will prevent the devices from being discovered and switched
	fauxmo.enable(true);

	// Add virtual devices
	fauxmo.addDevice("TV");
	//fauxmo.addDevice("Pause"); // not understood well by Alexa
	//fauxmo.addDevice("Show"); // not understood well by Alexa
	//fauxmo.addDevice("Hold"); // not understood well by Alexa
	fauxmo.addDevice("Idle");
	fauxmo.addDevice("Clock");

	fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state) {
		//DEBUG_PRINTLN("Device #%d (%s) state: %s\n", device_id, device_name, state ? "ON" : "OFF");
		//digitalWrite(LED, !state);
		if (0 == device_id) {
			tv_state = state;
			if (state) {
				sc = SC_TV_ON;
			}
			else {
				sc = SC_TV_OFF;
			}
		}
		else if (1 == device_id) {
			pause_state = state;
			sc = SC_PLAYPAUSE;
		}
		else if (2 == device_id) {
			clock_running = state;
			if (state) {
				sc = SC_CLOCK_ON;
			}
			else {
				sc = SC_CLOCK_OFF;
			}
		}
		else {
			sc = SC_DO_NOTHING;
		}
	});

	// Callback to retrieve current state (for GetBinaryState queries)
	fauxmo.onGetState([](unsigned char device_id, const char * device_name) {
		bool state = false;
		if (0 == device_id) {
			state = tv_state;
		}
		else if (1 == device_id) {
			state = pause_state;
		}
		else if (2 == device_id) {
			state = clock_running;
		}
		else {
			sc = SC_DO_NOTHING;
		}
		return state;
	});
}


void clock_initiate() {
	// instead of using FastLED to manage the POWER used we calculate the max brightness one time below
	//FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, LED_STRIP_MILLIAMPS);
	FastLED.setCorrection(TypicalPixelString);
	FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);

	//determine the maximum brightness allowed within POWER limits if 6, 9, 12, and 3 o'clock are a third brightness, and
	//the hour and minute hands are max brightness
	FastLED.clear();
	leds[0] = CHSV(0, 0, 85);
	leds[3] = CHSV(0, 0, 85);
	leds[6] = CHSV(0, 0, 85);
	leds[9] = CHSV(0, 0, 85);
	leds[1] = CHSV(0, 0, 255);
	leds[11] = CHSV(0, 0, 255);
	max_brightness = calculate_max_brightness_for_power_vmA(leds, NUM_LEDS, 255, LED_STRIP_VOLTAGE, LED_STRIP_MILLIAMPS);
	FastLED.clear();

	strip.Begin();
	strip.Show(); // Set to black (^= off) after reset

	configTime(TZ_America_New_York, "pool.ntp.org");

	update_time(2);

}

void ping_initiate() {
	// callback for each answer/timeout of each ping
	PingPhone.on(true, [](const AsyncPingResponse& response) {
		// only need one response to confirm device is present
		// but need multiple timeouts to confirm device is actually away
		if (response.answer) {
			return true; // if any more ping requests are queued, then cancel them
		}
		return false; // if any more ping requests are queued, then continue with them. 
	});

	// callback after all pings complete
	PingPhone.on(false, [](const AsyncPingResponse& response) {
		previous_phone_state = phone_state;
		pd_trigger = PD_Triggers::PHONE;
		if (response.answer) {
			phone_state = PhoneStates::HERE;
		}
		else {
			phone_state = PhoneStates::AWAY;
		}
		return false; // return value does not matter here
	});

	PingPhone.begin(phone_ip, NUM_PINGS, PING_TIMEOUT);
	
	// callback for each answer/timeout of each ping
	PingTV.on(true, [](const AsyncPingResponse& response) {
		// only need one response to confirm device is present
		// but need multiple timeouts to confirm device is actually away
		if (response.answer) {
			return true; // if any more ping requests are queued, then cancel them
		}
		return false; // if any more ping requests are queued, then continue with them. 
	});

	// callback after all pings complete
	PingTV.on(false, [](const AsyncPingResponse& response) {
		if (response.answer) {
			// the normal mode of operation is for the client's browser to query all of the devices
			// however the TV's web sever does set CORS in the header so the browser blocks the result of the device-info query
			// as a workaround we use our SmartHomeControl device to make the query for us, since it does not respect CORS
			// cb_tv callbacks will set tv_state
			ahClient.init("GET", TV_QUERY_URL, &cb_tv_query_data, &cb_tv_query_offline);
			ahClient.send();
		}
		else {
			tv_state = false;
		}
		return false; // return value does not matter here
	});

	PingTV.begin(tv_ip, NUM_PINGS, PING_TIMEOUT);
}

void setup() {
	pinMode(LED, OUTPUT);
	digitalWrite(LED, HIGH); // Our LED has inverse logic (high for OFF, low for ON)

	Serial.begin(115200);

	OTA_server_initiate();

	if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET_MASK, DNS1, DNS2)) {
		Serial.println("WiFi config failed.");
	}

	if (WiFi.SSID() != ssid) {
		WiFi.mode(WIFI_STA);
		WiFi.begin(ssid, password);
		WiFi.persistent(true);
		WiFi.setAutoConnect(true);
		WiFi.setAutoReconnect(true);
	}

	if (WiFi.waitForConnectResult() != WL_CONNECTED) {
		Serial.println(F("Connection Failed!"));

		WiFi.begin(ssid, password);
		Serial.println(F(""));
		Serial.print(F("WiFi connecting"));
		while (WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.print(F("."));
		}
		Serial.println(F(""));
		Serial.print(F("Connected: "));
		Serial.println(WiFi.localIP());
	}

	if (!LittleFS.begin()) {
		Serial.println("Failed to mount file system");
		// something is very broken. don't continue, but still allow OTA updates.
		while(true) {
			ArduinoOTA.handle();
		}
	}

	smart_devices_config_load();
	web_server_initiate();
	webserial_initiate();
	ping_initiate();

	rf_switch.enableReceive(digitalPinToInterrupt(12)); //D6 on HiLetgo ESLittleFS.remove("/access_logP.txt");P8266
	irsend.begin();
	fauxmo_initiate();
	clock_initiate();

	Serial.print("hour: ");
	Serial.println(local_now->tm_hour);
	Serial.print("minute: ");
	Serial.println(local_now->tm_min);


	// a WebSerial message sent here likely won't make it to the console because the client won't have established a connection yet
	DEBUG_CONSOLE.println("SmartHomeControl - Power On");
	write_log("SHC on");

/*
	Dir dir = LittleFS.openDir("/");
	while (dir.next()) {
		DEBUG_CONSOLE.println(dir.fileName());
		write_log(dir.fileName());
		if(dir.fileSize()) {
			File f = dir.openFile("r");
			DEBUG_CONSOLE.println(f.size());
			size_t s = f.size();
			char str[6];
			snprintf(str, sizeof str, "%zu", s);
			write_log(str);
		}
	}
*/

	//ESP.wdtEnable(4000); //[ms], parameter is required but ignored
}


void loop() {
	static uint32_t pm = millis(); // previous millis
	if ((millis() - pm) >= 2000) {
		pm = millis();
		Serial.println(ESP.getFreeHeap(),DEC);
	}

	//ESP.wdtFeed();
	ArduinoOTA.handle();

	if (restart_needed || WiFi.status() != WL_CONNECTED) {
		web_server.removeHandler(websocket); // prevents reconnection before restart?
		websocket->closeAll();
		// need a delay here for OTA update to finish and for websocket to completely close
		delay(2000);
		ESP.restart();
	}

	handle_clock();
	handle_RF_command();
	handle_presence_detection();
	// fauxmoESP uses an async TCP server, but also a sync UDP server; therefore, we have to manually poll for UDP packets.
	fauxmo.handle();
	handle_smart_speaker_command();
	handle_group_power_command();
	get_tv_state();
	transmit_IR_data(); // other commands set ir_data so it's logical to have transmit_IR_data() last

}
