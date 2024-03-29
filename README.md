# SmartHomeControl

This is a Swiss army knife of a ESP8266 project. It is born from the desire to play with the control of smart home devices without having to run a Home Assistant server. It does the following:
1) Creates a web interface for turning on/off several devices in one panel.
2) Creates a web interface for controlling a TV with an IR blaster.
3) Simulates Wemo devices with Fauxmo to allow voice control using an Echo device.
4) Controls devices based on 433 MHz RF signals received.
5) Runs an LED ring clock that is updated via NTP and adjusts for Daylight Saving Time.
6) Does presence detection by pinging my smart phone.
7) Sends notifications via SMTP to my phone's SMS email address. No ports to open, servers to run, apps to install, or notifications to enable.

Your ESP8266 can be programmed over the air in a couple of ways:

1) Use the Select Bin File option in the web interface
The bin files can be found in .pio/build/nodemcuv2.
To create firmware.bin simply build the project.
To create littlefs.bin click the ant logo, and click Build Filesystem Image.

2) Use the espota upload protocol in platformio. This is already setup in the included platformio.ini.
To flash the firmware click upload.
To flash littlefs.bin click the ant logo, and click Upload Filesystem Image.


**A few accomplishments I'm particularly proud of**

To correctly write a littefs.bin that is uploaded via the webpage's OTA method requires having the exact size of the bin file. However, when uploading a file the header's Content-Length is the size of the bin file plus the number of bytes in the header and footer. The header and footer size varies between browsers so it's impossible to get the actual size of the bin file using request->contentLength(). To work around this I used a hidden filesize parameter in the select_bin html page which is set and sent alongside the bin file when the upload is started:
	document.querySelector('form').addEventListener('submit', (e) => {
		document.getElementById('filesize').value = document.getElementById('file').files[0].size;
		...
	});
Then the firmware's flash handler can get the actual size of the bin file with:
	filesize = request->getParam("filesize", true)->value().toInt();
An alternate solution is to use javascript to upload the bin file and set X-File-Size in the header, but I found that approach to be much less reliable than using the browser's native upload capabilities.
	
I updated the AsyncHttpClient library to allow multiple simultaneous async requests.

I updated the NTPClient library to use the Timezone library so manual adjustments aren't necessary when daylight saving time starts/ends.

data/smart_devices.json is used to generate the smart device controls that appear on the web page. To add or remove devices you simply edit the json file instead of editing the web page.

**CORS Note**
The CORS command has been disabled by default in TAMOTA v10.0.0 and v10.0.1 until they come up with a safer implementation.
See https://github.com/arendst/Tasmota/pull/12827
This project requires the CORS command to be working for the interface to display the state of a device. As a workaround you can use an older version of TASMOTA or compile a newer version with #define USE_CORS uncommented in tasmota/my_user_config.h
See https://tasmota.github.io/docs/Gitpod/ for info on how to compile TAMOTA online.


