
/*
myWebServerAsync.h for esp8266

Copyright (c) 2016 David Paiva (david@nailbuster.com). All rights reserved.

parts based on various project sample/sources...
FSWebServer - Example WebServer with SPIFFS backend for esp8266 by Hristo Gochkov.
Async Web Server https://github.com/me-no-dev/ESPAsyncWebServer
also some code from project http://www.john-lassen.de/index.php/projects/esp-8266-arduino-ide-webconfig



This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

//required libraries:  
//
//TimeLib for ntp here:  https://github.com/PaulStoffregen/Time
//Arduino Json here:  https://github.com/bblanchon/ArduinoJson
//Async Web Server https://github.com/me-no-dev/ESPAsyncWebServer
//Async TCP Library https://github.com/me-no-dev/ESPAsyncTCP
//latest arduino/esp8266 core:  https://github.com/esp8266/Arduino


#ifndef _MYWEBSERVERASYNC_h
#define _MYWEBSERVERASYNC_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif



#define DEBUG
//#define ALLOW_IDEOTA             //define this to allow IDE OTA,  good while debugging....
#define BOOTSTRAP				   //define will add embedded bootstrap/css/jquery... a hit on prog size (63KB prog size added total)!!  

#ifdef DEBUG
#define DebugPrint(x)     Serial.print (x)
#define DebugPrintDec(x)  Serial.print (x, DEC)
#define DebugPrintln(x)   Serial.println (x)
#else
#define DebugPrint(x)
#define DebugPrintDec(x)
#define DebugPrintln(x) 
#endif

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>


extern AsyncWebServer server;

class MyWebServerClassAsync
{
protected:


public:
	String ssid;
	String password;
	byte  IP[4];
	byte  Netmask[4];
	byte  Gateway[4];
	boolean dhcp;
	boolean MDNSdisable;
	boolean cDNSdisable;
	boolean SoftAPAlways;      //run AP Mode always...normally false to only run when can't connect	
	boolean OTAisflashing;
	boolean isDownloading;    //file transfers get wacky with other services....so other services should check and disable if true....
	boolean AllowPublic = false;  //determine if we allow page views for public pages...or need the admin access pass for all pages.
	String ntpServerName;  //ntp stuffs
	long UpdateNTPEvery;
	unsigned long ResetTimeCheck;
	long timezone;
	boolean daylight;
	boolean useNTP;
	String DeviceName;  //used by mDNS.local
	String CurTimeString();
	String CurDateString();
	String urldecode(String input);
	String urlencode(String str);
	File fsUploadFile;
	String JSONCallBack;  //async json callback in handle;.
	MyWebServerClassAsync();



	void begin();
	void handle();
	void(*jsonSaveHandle)(String fname) = NULL;  //callback function when json form is saved...fname is the filename saved.

	bool WiFiLoadconfig();  //load wificonfig from json file
	void ServerLog(String logmsg);
	bool isAuthorized(AsyncWebServerRequest *request);
	bool clientHasWeb();  //if client is using softAP or ip direct	

};

extern MyWebServerClassAsync WebServer;
extern String WebServerLog;



#endif

