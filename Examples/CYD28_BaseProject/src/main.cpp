/**
 * @file main.cpp
 * @author Piotr Zapart (www.hexefx.com)
 * @brief 	demo project for the CYD 2.8" board (ESP32 + ILI9341 display)
 * 
 * 	Libraries:
 * 		CYD28_LDR - onboard LDR for light intensite measurement
 * 		CYD28_RGBled - onboard RGB led, or just R if PSRAM mod is perfromed
 * 		CYD28_SD - sd card library and filesystem
 * 		CYD28_display - dual buffered display driver configured for LVGL v8
 * 		CYD28_Touchscreen - bit banged SPI driver for the onboard touch screen controller
 * 		CYD_Audio - audio library optimized for internal 8bit DAC, based on ESP32-audioI2S by Wolle (schreibfaul1)
 * 		lvgl - v8.3.9 
 * 		SimpleCLI - console command via UART
 * 		TFT_eSPI - used for low level dislpay access.
 * 		WifiManager - added as lib dependency in platformio.ini
 * 
 * 		--- Audio ---
 * 		Audio is runing as a separate task on core 0. Use queues to communicate with it. 
 * 		See CYD28_audio.h/cpp and gui.h/cpp files  
 * 
 * @version 1.
 * @date 2023-08-25
 */
#include <Arduino.h>
#include "SPI.h"
#include "SD.h"
#include <WiFi.h>
#include "CYD28_LDR.h"
#include "CYD28_RGBled.h"
#include "CYD28_SD.h"
#include "CYD28_Display.h"
#include "gui.h"
#include "CYD28_audio.h"
#include "console.h"

#include <Update.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>


LDR ldr;
RGBLED led;
WiFiManager wifiManager;

uint32_t tNow, tLast;

// Try each "ssid password" line from /wifi.txt, top to bottom, until one
// connects. Lines starting with '#' and blanks are skipped. Returns true
// once associated; false if no SD card, no file, or all entries fail.
static bool try_wifi_from_sd()
{
	Serial.println("[wifi] entered try_wifi_from_sd");
	Serial.flush();
	if (!SD.exists("/wifi.txt")) {
		Serial.println("[wifi] SD.exists(/wifi.txt) = false");
		Serial.flush();
		return false;
	}
	File f = SD.open("/wifi.txt", "r");
	if (!f) {
		Serial.println("[wifi] SD.open(/wifi.txt) returned invalid file");
		Serial.flush();
		return false;
	}
	Serial.printf("[wifi] /wifi.txt opened, size=%u\n", (unsigned)f.size());
	WiFi.mode(WIFI_STA);
	char line[160];
	while (f.available()) {
		int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
		if (len <= 0) continue;
		line[len] = 0;
		// trim leading whitespace
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		// trim trailing \r / whitespace
		char *e = s + strlen(s);
		while (e > s && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
		if (*s == 0 || *s == '#') continue;
		// split on first whitespace
		char *sep = s;
		while (*sep && *sep != ' ' && *sep != '\t') sep++;
		if (!*sep) continue;
		*sep++ = 0;
		while (*sep == ' ' || *sep == '\t') sep++;
		const char *ssid = s, *pass = sep;
		Serial.printf("[wifi] try ssid=\"%s\"\n", ssid);
		WiFi.begin(ssid, pass);
		unsigned long t0 = millis();
		while (millis() - t0 < 12000) {
			if (WiFi.status() == WL_CONNECTED) {
				Serial.printf("[wifi] connected: %s -> %s\n", ssid,
				              WiFi.localIP().toString().c_str());
				f.close();
				return true;
			}
			delay(200);
		}
		Serial.printf("[wifi] %s timed out\n", ssid);
		WiFi.disconnect(true);
		delay(200);
	}
	f.close();
	return false;
}

void setup()
{
	Serial.begin(115200);
#ifndef DUSE_BACKLIGHT_MOD
	pinMode(21, OUTPUT);		// turn on the display backlight
	digitalWrite(21, HIGH);
#endif

	console_init();
	delay(1500);

	led.begin();
	analogReadResolution(10);
	ldr.begin();
	ldr.thresSet(300);
	ldr.hystSet(100);
	audioInit();
	display.begin(CYD28_DISPLAY_ROT_LANDSC1);

	gui_init();

	// SD before WiFi so we can read /wifi.txt.
	Serial.println("[boot] mounting SD...");
	Serial.flush();
	sdcard.begin();
	Serial.printf("[boot] SD cardType=%d\n", (int)SD.cardType());
	Serial.flush();
	if (!try_wifi_from_sd()) {
		Serial.println("[boot] falling back to WiFiManager");
		Serial.flush();
		// Fallback: WiFiManager captive portal (kept for first-boot use).
		wifiManager.autoConnect("CYD28", "passwordcyd28");
	}
}

void loop()
{
	display.loop();
	console_process();

	// tNow = millis();
	// if (tNow - tLast > 1000)
	// {
	// 	tLast = tNow;

	// }

}
