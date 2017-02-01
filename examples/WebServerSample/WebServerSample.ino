
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <FS.h>
#include <ArduinoJson.h> 
#include <myWebServerAsync.h>
#include <TimeLib.h>






void myTestFunc(AsyncWebServerRequest *request){
	//  http://192.168.1.xx/testurl
	//  execute this code:  DO NOT USE DELAY or YEILD function EVER in any handle functions!!!!  Async rules....

	request->send(200, "text/html", "I will now do something interesting... Thanks for the request!");
}




void setup() {
  // put your setup code here, to run once:
	Serial.begin(115200);

	WebServer.begin();	

	server.on("/testurl", myTestFunc);
	server.on("/testquick", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", "You wanted something quick...here it is :)");
	});
}

void loop() {
  // put your main code here, to run repeatedly:
	WebServer.handle();

}
