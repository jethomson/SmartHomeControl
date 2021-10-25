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

#include "wifi_credentials.h" // set const char *ssid and const char *password in lib/wifi_credentials.h
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include <AsyncHttpClient.h>

#include <WebSerial.h>

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

#include <NTPClient.h>
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


#define LED 2 // onboard LED

#define DATA_PIN 2	// this project doesn't connect LEDs to this pin but setting DATA_PIN is still necessary for FastLED to run correctly
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


WiFiUDP ntpUDP;
// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);
NTPClient timeClient(ntpUDP, "pool.ntp.org", usET);

uint32_t tic = 0;
uint8_t h = 0;
uint8_t m = 0;
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

fauxmoESP fauxmo;
enum SmartCommand {DO_NOTHING, TV_ON, TV_OFF, PLAYPAUSE, CLOCK_ON, CLOCK_OFF};
SmartCommand sc = DO_NOTHING;
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
void handle_smart_speaker_command(void);
void handle_group_power_command(void);
void transmit_IR_data();

void transmit_IR_data(void);
void http_cmnd(String);
size_t simple_JSON_parse(string, string, size_t, vector<string> &);
void update_time(void);
void set_sleep_time(void);
void show_leds(void);
void handle_flash_bin(AsyncWebServerRequest *, const String &, size_t, uint8_t *, size_t, bool);
string process_cmnd(char *);

void smart_devices_config_load(void);
void fauxmo_initiate(void);
void web_server_initiate(void);
void OTA_server_initiate(void);
void clock_initiate(void);


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
		update_time();
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
				update_time();

				// turn off clock display at midnight
				if (h == 0 && m == 0) {
					clock_running = false;
					set_sleep_time();
				}
			}

			mi = ((m / 5) + OFFSET) % NUM_LEDS;
			hi = (h + OFFSET) % NUM_LEDS;

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
	static uint32_t pm = millis(); // previous millis
	uint32_t dt = 0;

	if (rf_switch.available()) {
		int value = rf_switch.getReceivedValue();

		dt = millis() - pm;
		if (dt >= 1500) { // use a delay to prevent repeating commands
			pm = millis();

			switch (value) {
			case 6902449: //left button
				http_cmnd("http://192.168.1.50/cm?cmnd=POWER%20TOGGLE"); //kitchen
				break;
			case 6902456: //middle button
				http_cmnd("http://192.168.1.51/cm?cmnd=POWER%20TOGGLE"); //hallway
				//http_cmnd("http://192.168.1.62/cm?cmnd=POWER%20TOGGLE"); //dining
				//http_cmnd("http://192.168.1.61/cm?cmnd=POWER%20TOGGLE"); //den
				break;
			case 6902450: //right button
				group_power_flag = 1;
				group_power_states[group_power_flag] = !group_power_states[group_power_flag];
				break;
			case 0:
				break;
			default:
				break;
			}
		}
		rf_switch.resetAvailable();
	}
}

void handle_smart_speaker_command() {
	switch (sc) {
	case TV_ON:
		ir_data = "8A"; // discrete TV Power ON
		break;
	case TV_OFF:
		ir_data = "18"; // discrete TV Power OFF
		break;
	case PLAYPAUSE:
		ir_data = "32"; // TV Play/Pause
		break;
	case CLOCK_ON:
		clock_running = true;
		time_update_needed = true;
		break;
	case CLOCK_OFF:
		clock_running = false;
		set_sleep_time();
		break;
	default:
		break;
	}
	sc = DO_NOTHING;
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
					WebSerial.println("execute locally");
					string s = power_cmnds[i];
					size_t start = s.find("=");
					if (start != string::npos && start < s.length()-1 ) {
						s = s.substr(start+1);
						start = s.find("%20");
						if (start != string::npos) {
							s.replace(start, 3, " ");
						}
						WebSerial.println(s.c_str());
						process_cmnd(&s[0]);
					}
					else {
						WebSerial.println("invalid command");
					}
				}
			}
		}
		group_power_flag = -1;
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

void update_time() {
	uint8_t s = 0;

	if (timeClient.update()) {
		DEBUG_PRINTLN(timeClient.getFormattedTime());
		time_update_needed = false;
		s = timeClient.getSeconds();
		h = timeClient.getHours();
		m = timeClient.getMinutes();
		tic = TICS_PER_SEC * s;
	}

	if (h > 23 || m > 59 || s > 59) {
		delay(2000);
		ESP.restart();
	}
}

void set_sleep_time() {
	update_time();
	tic = 0;
	uint32_t seconds_since_midnight = 3600*timeClient.getHours() + 60*timeClient.getMinutes() + timeClient.getSeconds();
	uint8_t day_of_the_week = timeClient.getDay();

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

	if (!index)	{
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
			//WebSerial.println("Update complete");
			Serial.flush();
			restart_needed = true;
		}
	}
}

string process_cmnd(char *buf) {
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
			message += "\"POWERTV\":\"Unknown";
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

void web_server_initiate() {
	web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		//request->send_P(200, "text/html", index_html);
		request->send(LittleFS, "/index.html");
	});

	web_server.on("/TV", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/TV.html");
	});

	web_server.on("/cm", HTTP_GET, [](AsyncWebServerRequest *request) {
		char buf[301]; // max buf size on TASMOTA
		string message;
		if (request->hasParam(PARAM_INPUT_1)) {
			String inputMessage = request->getParam(PARAM_INPUT_1)->value();
			inputMessage.toCharArray(buf, 301);
			message = process_cmnd(buf);
		}
		else {
			message = "{}";
		}

		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", message.c_str());
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});

	web_server.on("/smart_devices.json", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", smart_devices_JSON_string.c_str());
		response->addHeader("Access-Control-Allow-Origin", CORS_value.c_str());
		request->send(response);
	});

	web_server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/restart.html");
		restart_needed = true;
	});

	web_server.on("/console", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/console.html");
	});

	//OTA update through web interface
	web_server.on("/select_bin", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(LittleFS, "/select_bin.html");
	});

	//OTA update via web page
	//AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload);
	web_server.on("/flash_bin", HTTP_POST,	[](AsyncWebServerRequest *request) {}, handle_flash_bin);

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

void smart_devices_config_load() {
	if (LittleFS.begin()) {
		File f = LittleFS.open("/smart_devices.json", "r");
		if (f) {
			smart_devices_JSON_string = f.readString().c_str();
			f.close();
		}
		else {
			Serial.println("Failed to open config file");
		}
	}
	else {
		Serial.println("Failed to mount file system");
		return;
	}
}

//using an older version of Fauxmo library because I want smart sockets not smart bulbs. Using fauxmo smart sockets lets me tell Alexa to turn off all the lights without affecting fauxmo devices.
void fauxmo_initiate() {
	// You have to call enable(true) once you have a WiFi connection
	// You can enable or disable the library at any moment
	// Disabling it will prevent the devices from being discovered and switched
	fauxmo.enable(true);

	// Add virtual devices
	fauxmo.addDevice("TV");
	//fauxmo.addDevice("Pause"); // not understood well
	//fauxmo.addDevice("Show"); // not understood well
	//fauxmo.addDevice("Hold"); // not understood well
	fauxmo.addDevice("Idle");
	fauxmo.addDevice("Clock");

	fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state) {
		//DEBUG_PRINTLN("Device #%d (%s) state: %s\n", device_id, device_name, state ? "ON" : "OFF");
		//digitalWrite(LED, !state);
		if (0 == device_id) {
			tv_state = state;
			if (state) {
				sc = TV_ON;
			}
			else {
				sc = TV_OFF;
			}
		}
		else if (1 == device_id) {
			pause_state = state;
			sc = PLAYPAUSE;
		}
		else if (2 == device_id) {
			clock_running = state;
			if (state) {
				sc = CLOCK_ON;
			}
			else {
				sc = CLOCK_OFF;
			}
		}
		else {
			sc = DO_NOTHING;
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
			sc = DO_NOTHING;
		}
		return state;
	});
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

	timeClient.begin();
	update_time();
	//Serial.println(timeClient.getFormattedTime());
}

void setup() {
	pinMode(LED, OUTPUT);
	digitalWrite(LED, HIGH); // Our LED has inverse logic (high for OFF, low for ON)

	Serial.begin(57600);

	smart_devices_config_load();

	rf_switch.enableReceive(digitalPinToInterrupt(12)); //D6 on HiLetgo ESP8266
	irsend.begin();

	websocket = new AsyncWebSocket("/consolews");
    websocket->onEvent([&](AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) -> void {
		if(type == WS_EVT_DATA){
			char buf[len];
			for(uint8_t i=0; i < len; i++){
				buf[i] = char(data[i]);
			}
			if (strcmp(buf, "#c") != 0) {
				process_cmnd(buf);
			}
			else {
				WebSerial.print("#r");
			}
        }
	});

	web_server.addHandler(websocket);
	WebSerial._ws = websocket;

	web_server_initiate();
	OTA_server_initiate();

	WiFi.begin(ssid, password);
	Serial.println("");
	Serial.print("WiFi connecting");
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.print("Connected: ");
	Serial.println(WiFi.localIP());

	fauxmo_initiate();

	clock_initiate();

	//ESP.wdtEnable(4000); //[ms], parameter is required but ignored
}

void loop() {
	//ESP.wdtFeed();
	ArduinoOTA.handle();

	if (restart_needed || WiFi.status() != WL_CONNECTED) {
		restart_needed = false;
		delay(2000);
		ESP.restart();
	}

	handle_clock();
	handle_RF_command();
	// fauxmoESP uses an async TCP server, but also a sync UDP server; therefore, we have to manually poll for UDP packets.
	fauxmo.handle();
	handle_smart_speaker_command();
	handle_group_power_command();
	transmit_IR_data(); // other commands set ir_data so it's best to have transmit_IR_data() last
}
