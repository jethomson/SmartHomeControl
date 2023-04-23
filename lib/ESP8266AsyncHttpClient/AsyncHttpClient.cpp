/*
  AsyncHttpClient.cpp - Library for sending simple data via HTTP GET/POST.
  Created by Jonathan Strauss, July 27, 2020.
  Modified by Jonathan Thomson, October 18, 2021 to allow multiple simultaneous async requests.
*/

//https://github.com/me-no-dev/ESPAsyncTCP/issues/18

#include "Arduino.h"
#include "ESPAsyncTCP.h"
#include "AsyncHttpClient.h"


//### String type = POST or GET (most used for POST)
//### String fullUrl = full URL e.g. http://server.com:8080/push.php or http://server.com or http://server.com:8081
//### String dataMode = only for POST - Content Type --> e.g. application/json , application/x-www-form-urlencoded
//### String data = data to submit --> depends on Type (GET/POST) e.g. (application/x-www-form-urlencoded) api-key=1asnd12i3kas&firstname=Jonathan&lastname=Strauss or (application/json) {"api-key": "1asnd12i3kas", "firstname": "Jonathan", "lastname": "Strauss"}

void AsyncHttpClient::init(String type, String fullUrl, String dataMode, String data, AcDataHandler handle_data, void (*handle_offline)(void)) {
	connect_args = new ConnectArgs;
	connect_args->_type = type;
	connect_args->_fullUrl = fullUrl;
	connect_args->_dataMode = dataMode;
	connect_args->_data = data;
	connect_args->_handle_data = handle_data;
	connect_args->_handle_offline = handle_offline;
	getHostname(fullUrl);
}

void AsyncHttpClient::init(String type, String fullUrl, AcDataHandler handle_data, void (*handle_offline)(void)) {
	connect_args = new ConnectArgs;
	connect_args->_type = type;
	connect_args->_fullUrl = fullUrl;
	connect_args->_handle_data = handle_data;
	connect_args->_handle_offline = handle_offline;
	getHostname(fullUrl);
}

//### String type = POST or GET (most used for GET)
//### String fullUrl = full URL e.g. http://server.com:8080/push.php?api-key=1asnd12i3kas&firstname=Jonathan&lastname=Strauss  or http://server.com or http://server.com:8081
void AsyncHttpClient::init(String type, String fullUrl) {
	connect_args = new ConnectArgs;
	connect_args->_type = type;
	connect_args->_fullUrl = fullUrl;
	// I seem to recall setting the callback to default_handle_data did not work as expected. Need to investigate.
	connect_args->_handle_data = default_handle_data;
	//connect_args->_handle_data = nullptr;
	connect_args->_handle_offline = nullptr;
	getHostname(fullUrl);
}

void AsyncHttpClient::setType(String type) {
	connect_args->_type = type;
}
void AsyncHttpClient::setFullURL(String fullUrl) {
	connect_args->_fullUrl = fullUrl;
	getHostname(fullUrl);
}
void AsyncHttpClient::setDataMode(String dataMode) {
	connect_args->_dataMode = dataMode;
}
void AsyncHttpClient::setData(String data) {
	connect_args->_data = data;
}


//### SUBTRACT Hostname and Port from URL
void AsyncHttpClient::getHostname(String url) {
	int index = url.indexOf(':');
	if (index < 0) {
		connect_args->_hostname = "";
		return;
	}
	String protocol = url.substring(0, index);
	connect_args->_port = 80;
	if (index == 5) {
		connect_args->_port = 443;
	}
	
	url.remove(0, (index + 3));
	index = url.indexOf('/');
	String hostPart;
	if (index > 0) {
		hostPart = url.substring(0, index);
		connect_args->_absolutePath = url.substring(index);
	}
	else {
		hostPart = url;
		connect_args->_absolutePath = '/';
	}

	url.remove(0, index);

	index = hostPart.indexOf(':');
	String host;
	if (index >= 0) {
		connect_args->_hostname = hostPart.substring(0, index);
		host = connect_args->_hostname;
		hostPart.remove(0, (index + 1)); // remove hostname + :
		connect_args->_port = hostPart.toInt();        // get port
	}
	else {
		connect_args->_hostname = hostPart;
	}
}

void AsyncHttpClient::default_handle_data(void *arg, AsyncClient *c, void *data, size_t len) {
	Serial.print("\r\nResponse: ");
	Serial.println(len);
	uint8_t *d = (uint8_t *)data;
	for (size_t i = 0; i < len; i++)
		Serial.write(d[i]);
	c->close(true);
}


void AsyncHttpClient::send() {

	Serial.println("Type: " + connect_args->_type + " URL: " + connect_args->_fullUrl + " _absolutePath: " + connect_args->_absolutePath +" DataMode: " + connect_args->_dataMode + " Data: " + connect_args->_data);

	if (connect_args->_hostname.length() <= 0) {
		Serial.println("Hostname is not defined");
		return;
	}

	if (connect_args->_port <= 0) {
		Serial.println("Port is not defined");
		return;
	}

	if (connect_args->_type.length() <= 0) {
		Serial.println("Type is not defined (Only GET or POST)");
		return;
	}

	if (connect_args->_fullUrl.length() <= 0) {
		Serial.println("URL is not defined");
		return;
	}

	if (connect_args->_type == "POST" && connect_args->_dataMode.length() <= 0) {
		Serial.println("DataMode is not defined");
		return;
	}

	if (connect_args->_type == "POST" && connect_args->_data.length() <= 0) {
		Serial.println("Data is not defined");
		return;
	}

	AsyncClient *aClient = NULL;
	aClient = new AsyncClient();
	if (!aClient) {
		//could not allocate client
		return;
	}

	// setConnectTimeout is only available for ESP32
	//aClient->setConnectTimeout(3);
	// these are only effective after connection established
	//aClient->setRxTimeout(2);
	//aClient->setAckTimeout(1000);

	aClient->onError([&](void *arg, AsyncClient *client, err_t error) {
		ConnectArgs* ca = reinterpret_cast<ConnectArgs*>(arg);
		Serial.println("Connect Error");
		Serial.println(error);
		Serial.println(client->errorToString(error));
		//aClient = NULL;
		delete client;
		delete ca;
	}, connect_args);

	aClient->onConnect([&](void *arg, AsyncClient *client) {
		ConnectArgs* ca = reinterpret_cast<ConnectArgs*>(arg);

		Serial.println("Connected");
		client->onError(NULL, NULL);

		client->onDisconnect([&](void *arg, AsyncClient *c) {
			Serial.println("Disconnected");
			//aClient = NULL;
			delete c;
		}, NULL);

		client->onData(ca->_handle_data, NULL);

		//send the request
		String PostHeader;
		if (ca->_type == "POST") {
			PostHeader = "POST " + ca->_absolutePath + " HTTP/1.1\r\nHost: " + ca->_hostname + "\r\nUser-Agent: Arduino/1.0\r\nConnection: close\r\nContent-Type: " + ca->_dataMode + ";\r\nContent-Length: " + String(ca->_data.length()) + "\r\n\r\n" + ca->_data + "\r\n";
		}
		else if (ca->_type == "GET") {
			PostHeader = "GET " + ca->_absolutePath + " HTTP/1.1\r\nHost: " + ca->_hostname + "\r\n\r\n";
		}
		client->write(PostHeader.c_str());
		delete ca;
	}, connect_args);


	if (!aClient->connect(connect_args->_hostname.c_str(), connect_args->_port)) {
		Serial.println("Connect Fail");
		connect_args->_handle_offline();
		AsyncClient *client = aClient;
		aClient = NULL;
		delete client;
		delete connect_args;
	}
}
