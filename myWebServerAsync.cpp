
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


#include "myWebServerAsync.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> 
#include "htmlEmbedBig.h"
#include <TimeLib.h>

AsyncWebServer server(80);
//AsyncEventSource events("/events");
MyWebServerClassAsync WebServer;
String WebServerLog;

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
bool dsnServerActive = false;
String ConfigUsername = "admin";   //used for webconfig username admin
String ConfigPassword = "";        //used for webconfig username admin
File fsUploadFile;
bool shouldReboot = false;


//ntp stuffs
WiFiUDP UDPNTPClient;
unsigned long lastTimeCheck = 0;  //for timer to fire every sec
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
time_t prevDisplay = 0; // when the digital clock was displayed



// send an NTP request to the time server at the given address
void  sendNTPpacket(IPAddress &address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
							 // 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:                 
	UDPNTPClient.beginPacket(address, 123); //NTP requests are to port 123
	UDPNTPClient.write(packetBuffer, NTP_PACKET_SIZE);
	UDPNTPClient.endPacket();
}

bool IsDst(int hour, int day, int month, int dow)  //north american dst  dow 0=SUN
{
	if (WebServer.daylight == false) return false; //option to disable DST

													 //January, february, and december are out.
	if (month < 3 || month > 11) { return false; }
	//April to October are in
	if (month > 3 && month < 11) { return true; }
	int previousSunday = day - dow;
	//In march, we are DST if our previous sunday was on or after the 8th.
	if (month == 3) { return previousSunday >= 8; }
	//In november we must be before the first sunday to be dst.
	//That means the previous sunday must be before the 1st.
	return previousSunday <= 0;
}


time_t getNtpTime()
{

	while (UDPNTPClient.parsePacket() > 0); // discard any previously received packets
	DebugPrintln("Transmit NTP Request");
	IPAddress timeServerIP;
	WiFi.hostByName(WebServer.ntpServerName.c_str(), timeServerIP);
	sendNTPpacket(timeServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500) {
		int size = UDPNTPClient.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
			DebugPrintln("Receive NTP Response");
			UDPNTPClient.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;
			// convert four bytes starting at location 40 to a long integer
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			secsSince1900 = secsSince1900 - 2208988800UL + ((WebServer.timezone / 10) * SECS_PER_HOUR);
			if (IsDst(hour(secsSince1900), day(secsSince1900), month(secsSince1900), dayOfWeek(secsSince1900) - 1)) secsSince1900 += 3600;  //add hour if DST			
			return secsSince1900;
		}
	}
	DebugPrintln("No NTP Response :-(");
	return 0; // return 0 if unable to get the time
}



// convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)
unsigned char h2int(char c)
{
	if (c >= '0' && c <= '9') {
		return((unsigned char)c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return((unsigned char)c - 'a' + 10);
	}
	if (c >= 'A' && c <= 'F') {
		return((unsigned char)c - 'A' + 10);
	}
	return(0);
}


String MyWebServerClassAsync::CurTimeString()
{
	char tmpStr[20];
	sprintf(tmpStr, "%02d:%02d:%02d %s", hourFormat12(), minute(), second(), (isAM() ? "AM" : "PM"));
	return String(tmpStr);
}

String MyWebServerClassAsync::CurDateString()
{
	return String(year()) + "-" + String(month()) + "-" + String(day());
}

String MyWebServerClassAsync::urldecode(String input)
{
	char c;
	String ret = "";

	for (byte t = 0; t<input.length(); t++)
	{
		c = input[t];
		if (c == '+') c = ' ';
		if (c == '%') {


			t++;
			c = input[t];
			t++;
			c = (h2int(c) << 4) | h2int(input[t]);
		}

		ret.concat(c);
	}
	return ret;

}

String MyWebServerClassAsync::urlencode(String str)
{
	String encodedString = "";
	char c;
	char code0;
	char code1;
	//char code2;
	for (unsigned int i = 0; i < str.length(); i++) {
		c = str.charAt(i);
		if (c == ' ') {
			encodedString += '+';
		}
		else if (isalnum(c)) {
			encodedString += c;
		}
		else {
			code1 = (c & 0xf) + '0';
			if ((c & 0xf) >9) {
				code1 = (c & 0xf) - 10 + 'A';
			}
			c = (c >> 4) & 0xf;
			code0 = c + '0';
			if (c > 9) {
				code0 = c - 10 + 'A';
			}
			//code2 = '\0';
			encodedString += '%';
			encodedString += code0;
			encodedString += code1;
			//encodedString+=code2;
		}
		yield();
	}
	return encodedString;
}





bool isAdmin(AsyncWebServerRequest *request)
{
	if (ConfigPassword == "") return true; //not using web password (default);
	DebugPrintln("U:" + ConfigUsername + " P:" + ConfigPassword);
	bool isAuth = false;
	//String realm = "*";
	if (!request->authenticate(ConfigUsername.c_str(), ConfigPassword.c_str())) {
		request->requestAuthentication(NULL, false);	
		return false;
		}
	else isAuth = true;

	return isAuth;
}

bool isPublicFile(String filename)  //in Public mode,  display any file that doesn't have a $$$, or they will need admin access....
{
	bool isPub = false;

	if (WebServer.AllowPublic)
	{
		if (filename.indexOf("$$$") < 0) isPub = true;   //if no $ in filename then allow file to be used/viewed.
		if (filename.indexOf("wifiset") >= 0) isPub = false;   //hardcode wifiset cannot be public.....
	}

	return isPub;
}


void handleFileList(AsyncWebServerRequest *request)
{
	if (isAdmin(request) == false) return;
	Dir dir = SPIFFS.openDir("/");
	String output = "{\"success\":true, \"is_writable\" : true, \"results\" :[";
	bool firstrec = true;
	while (dir.next()) {
		if (!firstrec) { output += ','; }  //add comma between recs (not first rec)
		else {
			firstrec = false;
		}
		String fn = dir.fileName();
		fn.remove(0, 1); //remove slash
		output += "{\"is_dir\":false";
		output += ",\"name\":\"" + fn;
		output += "\",\"size\":" + String(dir.fileSize());
		output += ",\"path\":\"";
		output += "\",\"is_deleteable\":true";
		output += ",\"is_readable\":true";
		output += ",\"is_writable\":true";
		output += ",\"is_executable\":true";
		output += ",\"mtime\":1452813740";   //dummy time
		output += "}";
	}
	output += "]}";
	//DebugPrintln("got list >"+output);
	request->send(200, "text/json", output);
}


bool handleFileRead(AsyncWebServerRequest *request, String path)
{
	DebugPrintln("handleFileRead: " + path);
	if (path.endsWith("/")) path += "index.html";
	String pathWithGz = path + ".gz";
	path = WebServer.urldecode(path);
	bool download = (request->arg("download") == "true");
	if (isPublicFile(path) == false) {
		if (isAdmin(request) == false) return false;  //check if a public file.
	}

	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {  //if file exists

		if (!download)
		{
			AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path);
			if (SPIFFS.exists(pathWithGz))  //set cache
			{				
				response->addHeader("Cache-Control", "max-age=86400");//set cache header here....
			}

			response->addHeader("Access-Control-Allow-Origin", "*");//always add '*'?
			request->send(response);
		}
		else {    //download
			AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path, String(), true); 
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
		}


		return true;

	}
	else return false;
}


void handleFileDelete(AsyncWebServerRequest *request,String fname)
	{
		if (isAdmin(request) == false) return;
		DebugPrintln("handleFileDelete: " + fname);
		fname = '/' + fname;
		fname = WebServer.urldecode(fname);
		if (!SPIFFS.exists(fname))
			return request->send(404, "text/plain", "FileNotFound");
		if (SPIFFS.exists(fname))
		{
			SPIFFS.remove(fname);
			request->send(200, "text/plain", "");
		}
	}


void HandleFileBrowser(AsyncWebServerRequest *request)
{   
	if (isAdmin(request) == false) return;	

	if (request->arg("do") == "list") {
		handleFileList(request);
	}
	else
		if (request->arg("do") == "delete") {
			handleFileDelete(request,request->arg("file"));
		}
		else
			/*if (request->arg("download") == "true") {
				handleFileRead(request, request->arg("file"));
			//	request->send(SPIFFS, "/filebrowse.html");
			}*/			
			{
				//if (!handleFileRead("/filebrowse.html")) { //send GZ version of embedded browser
				//	server.sendHeader("Content-Encoding", "gzip");
				//	server.send_P(200, "text/html", PAGE_FSBROWSE, sizeof(PAGE_FSBROWSE));
				handleFileRead(request,"/filebrowse.html");
				WebServer.isDownloading = true; //need to stop all cloud services from doing anything!  crashes on upload with mqtt...
			}
}




void handleJsonSave(AsyncWebServerRequest *request)
{
	//new must have JS in it!
	if (!request->hasArg("js") || !request->hasArg("f"))
		return request->send(500, "text/plain", "BAD JsonSave ARGS");  //must have f and js

	String fname = "/" + request->arg("f");
	fname = WebServer.urldecode(fname);


	

	if (isPublicFile(fname) == false)
	{
		if (isAdmin(request) == false) return;  //check if a public file.
	}

	File file = SPIFFS.open(fname, "w");
	if (file) {
		file.println(request->arg("js"));  //save json data	
		file.close();
	}
	else  //cant create file
		return request->send(500, "text/plain", "JSONSave FAILED");
	request->send(200, "text/plain", "");
	DebugPrintln("handleJsonSave: " + fname);
	//if (WebServer.jsonSaveHandle != NULL)	WebServer.jsonSaveHandle(fname);
	if (WebServer.jsonSaveHandle != NULL) WebServer.JSONCallBack=(fname);
	else WebServer.JSONCallBack = "";
}

void handleJsonLoad(AsyncWebServerRequest *request)
{

	if (request->arg(0) == 0)
		return request->send(500, "text/plain", "BAD JsonLoad ARGS");
	String fname = "/" + request->arg(0);

	fname = WebServer.urldecode(fname);

	if (isPublicFile(fname) == false)
	{
		if (isAdmin(request) == false) return;  //check if a public file.
	}
	DebugPrintln("handleJsonRead: " + fname);
	handleFileRead(request,fname);  
}

MyWebServerClassAsync::MyWebServerClassAsync() {

}


void formatspiffs(AsyncWebServerRequest *request)
{
	if (isAdmin(request) == false) return;
	DebugPrintln("formatting spiff...");
	if (!SPIFFS.format()) {
		DebugPrintln("Format failed");
	}
	else { DebugPrintln("format done...."); }
	request->send(200, "text/html", "Format Finished....rebooting");
}


void restartESP(AsyncWebServerRequest *request) {
	if (isAdmin(request) == false) return;
	request->send(200, "text/plain", "Restarting ESP...");
	shouldReboot = true;
}

void sendNetworkStatus(AsyncWebServerRequest *request)
{
	if (isAdmin(request) == false) return;
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	String state = "N/A";
	String Networks = "";
	if (WiFi.status() == 0) state = "Idle";
	else if (WiFi.status() == 1) state = "NO SSID AVAILBLE";
	else if (WiFi.status() == 2) state = "SCAN COMPLETED";
	else if (WiFi.status() == 3) state = "CONNECTED";
	else if (WiFi.status() == 4) state = "CONNECT FAILED";
	else if (WiFi.status() == 5) state = "CONNECTION LOST";
	else if (WiFi.status() == 6) state = "DISCONNECTED";

	Networks = "";  //future to scan and show networks async

	String wifilist = "";
	wifilist += "WiFi State: " + state + "<br>";
	wifilist += "Scanned Networks <br>" + Networks + "<br>";

	String values = "";
	values += "<body> SSID          :" + (String)WiFi.SSID() + "<br>";
	values += "IP Address     :   " + (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + "<br>";
	values += "Wifi Gateway   :   " + (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + "<br>";
	values += "NetMask        :   " + (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + "<br>";
	values += "Mac Address    >   " + String(macStr) + "<br>";
	values += "NTP Time       :   " + String(hour()) + ":" + String(minute()) + ":" + String(second()) + " " + String(year()) + "-" + String(month()) + "-" + String(day()) + "<br>";
	values += "Server Uptime  :   " + String(millis() / 60000) + " minutes" + "<br>";
	values += "Server Heap  :   " + String(ESP.getFreeHeap()) + "<br>";
	values += wifilist;
	values += " <input action=\"action\" type=\"button\" value=\"Back\" onclick=\"history.go(-1);\" style=\"width: 100px; height: 50px;\" /> </body> ";
	request->send(200, "text/html", values);
}


void SendAvailNetworks(AsyncWebServerRequest *request) {	
	int n = WiFi.scanComplete();
	if (n == -2) {
		WiFi.scanNetworks(true);
	}
	else if (n) {
		String postStr = "{ \"Networks\":[";    //build json string of networks available
		if (n == 0)
		{
			postStr += "{\"ssidname\":\"No networks found!\",\"qual\":\"0\",\"sec\":\" \" }";
		}
		else
		for (int i = 0; i < n; ++i) {
			int quality = 0;
			if (WiFi.RSSI(i) <= -100)
			{
				quality = 0;
			}
			else if (WiFi.RSSI(i) >= -50)
			{
				quality = 100;
			}
			else
			{
				quality = 2 * (WiFi.RSSI(i) + 100);
			}
			postStr += "{\"ssidname\":\"" + String(WiFi.SSID(i)) + "\",\"qual\":\"" + String(quality) + "\",\"sec\":\"" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*") + "\"},";
		}
		if (postStr.charAt(postStr.length() - 1) == ',') postStr.remove(postStr.length() - 1, 1);
		postStr += "] }";  //finish json array
		request->send(200, "application/json", postStr);
		WiFi.scanDelete();
		if (WiFi.scanComplete() == -2) {
			WiFi.scanNetworks(true);
		}
	}			
}



void SendServerLog(AsyncWebServerRequest *request) {
	if (isAdmin(request) == false) return;
	String rhtml = "";
	rhtml += "<body> <h1> Server Log </h1><br>";
	rhtml += "Server Uptime  :   " + String(millis() / 60000) + " minutes" + "<br>";
	rhtml += "Server Heap  :   " + String(ESP.getFreeHeap()) + "<br>";
	rhtml += "Last Log: <br>";
	rhtml += WebServerLog;
	rhtml += " <input action=\"action\" type=\"button\" value=\"Back\" onclick=\"history.go(-1);\" style=\"width: 100px; height: 50px;\" /> </body> ";
	request->send(200, "text/html", rhtml);
}












void onRequest(AsyncWebServerRequest *request) {
	//Handle Unknown Request
	if (!handleFileRead(request, request->url()))
		request->send(404, "text/plain", " FileNotFound " + request->url());
}

void onBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
	//Handle body
	if (!index) {
		Serial.printf("BodyStart: %u B\n", total);
	}
	for (size_t i = 0; i<len; i++) {
		Serial.write(data[i]);
	}
	if (index + len == total) {
		Serial.printf("BodyEnd: %u B\n", total);
	}
}

void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	//Handle upload
	if (!index) {		
		if (!filename.startsWith("/")) filename = "/" + filename;
		DebugPrintln("handleFileUpload Name: "); DebugPrintln(filename);
		fsUploadFile = SPIFFS.open(filename, "w");		
	}	
	fsUploadFile.write(data, len);	
	if (final) {
		fsUploadFile.close();
		Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index + len);
	}

}

void handleESPUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	if (isAdmin(request) == false) return;
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	
	if (!index) {
		WiFiUDP::stopAll();
		WebServer.OTAisflashing = true;
		DebugPrintln("Update: " + filename);
		//TODO check filesize from request?
		uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
		if (!Update.begin(maxSketchSpace)) {//start with max available size
			Update.printError(Serial);
		} 
	}	
	DebugPrint(".");
	Update.write(data,len);
	if (final) {
		if (Update.end(true)) { //true to set the size to the current progress
			DebugPrintln("Update Success: \nRebooting...\n");
			shouldReboot = true;
		}	
	}		
};


void OTAStart() {
	DebugPrintln("OTA Started"); WebServer.OTAisflashing = true;
}

void FileSaveContent_P(String fname, PGM_P content, u_long numbytes, bool overWrite = false) {   //save PROGMEM array to spiffs file....//f must be already open for write!

	if (SPIFFS.exists(fname) && overWrite == false) return;


	const int writepagesize = 1024;
	char contentUnit[writepagesize + 1];
	contentUnit[writepagesize] = '\0';
	u_long remaining_size = numbytes;


	File f = SPIFFS.open(fname, "w");



	if (f) { // we could open the file 

		while (content != NULL && remaining_size > 0) {
			size_t contentUnitLen = writepagesize;

			if (remaining_size < writepagesize) contentUnitLen = remaining_size;
			// due to the memcpy signature, lots of casts are needed
			memcpy_P((void*)contentUnit, (PGM_VOID_P)content, contentUnitLen);

			content += contentUnitLen;
			remaining_size -= contentUnitLen;

			// write is so overloaded, had to use the cast to get it pick the right one
			f.write((uint8_t *)contentUnit, contentUnitLen);
		}
		f.close();
		DebugPrintln("created:" + fname);
	}
}



void CheckNewSystem() {   //if new system we save the embedded htmls into the root of Spiffs as .gz!

	FileSaveContent_P("/wifisetup.html.gz", PAGE_WIFISETUP, sizeof(PAGE_WIFISETUP), false);
	FileSaveContent_P("/filebrowse.html.gz", PAGE_FSBROWSE, sizeof(PAGE_FSBROWSE), false);
	//jquery/bootstrap embed
#ifdef BOOTSTRAP
	FileSaveContent_P("/jquery.min.js.gz", PAGE_JQUERY, sizeof(PAGE_JQUERY), false);
	FileSaveContent_P("/bootstrap.min.css.gz", PAGE_BOOTCSS, sizeof(PAGE_BOOTCSS), false);
	FileSaveContent_P("/bootstrap.min.js.gz", PAGE_BOOTSTRAP, sizeof(PAGE_BOOTSTRAP), false);
	FileSaveContent_P("/font-awesome.min.css.gz", PAGE_FONTAWESOME, sizeof(PAGE_FONTAWESOME), false);
#endif // BOOTSTRAP

}

void handleRoot(AsyncWebServerRequest *request) {  //handles root of website (used in case of virgin systems.)

	if (!handleFileRead(request,"/")) {   //if new system without index we either show wifisetup or if already setup/connected we show filebrowser for config.
		if (isAdmin(request)) {
			if (WiFi.status() != WL_CONNECTED) {
				request->send(SPIFFS, "/wifisetup.html");
			}
			else { HandleFileBrowser(request); }
		}
	}
	//use indexhtml or use embedded wifi setup...	


}

void MyWebServerClassAsync::begin()
{


	DebugPrintln("Starting ES8266 ASYNC");

	bool result = SPIFFS.begin();
	DebugPrintln("SPIFFS opened: " + result);

	if (!WiFiLoadconfig())   //read network ..
	{
		// DEFAULT CONFIG
		ssid = "MYSSID";
		password = "MYPASSWORD";
		dhcp = true;
		IP[0] = 192; IP[1] = 168; IP[2] = 1; IP[3] = 100;
		Netmask[0] = 255; Netmask[1] = 255; Netmask[2] = 255; Netmask[3] = 0;
		Gateway[0] = 192; Gateway[1] = 168; Gateway[2] = 1; Gateway[3] = 1;
		ntpServerName = "0.de.pool.ntp.org";
		UpdateNTPEvery = 0;
		timezone = -10;
		daylight = true;
		DeviceName = "myESP";
		useNTP = false;
		ConfigPassword = "";
		DebugPrintln("General config applied");
	}

	String chipid = String(ESP.getChipId(), HEX);
	String hostname = "myESP" + chipid;


	WiFi.hostname(DeviceName);

	WiFi.mode(WIFI_STA);
	delay(10);

	//try to connect

	WiFi.begin(ssid.c_str(), password.c_str());


	DebugPrintln("Wait for WiFi connection.");

	// ... Give ESP 20 seconds to connect to station.
	unsigned long startTime = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000 && !(ssid == "MYSSID"))
	{
		DebugPrint('.');
		// SerialLog.print(WiFi.status());
		delay(500);
	}


	if (WiFi.waitForConnectResult() != WL_CONNECTED)
	{
		DebugPrintln("cannot connect to: " + ssid);
	}
	else if (!dhcp) {
		WiFi.config(IPAddress(IP[0], IP[1], IP[2], IP[3]), IPAddress(Gateway[0], Gateway[1], Gateway[2], Gateway[3]), IPAddress(Netmask[0], Netmask[1], Netmask[2], Netmask[3]));
	}




	bool StartAP = false;
	// Check connection
	if (WiFi.status() == WL_CONNECTED)
	{
		// ... print IP Address
		DebugPrint("IP address: ");
		DebugPrintln(WiFi.localIP());
		if (SoftAPAlways) {
			StartAP = true; //adminenabled then always start AP
			WiFi.mode(WIFI_AP_STA);
		}
	}
	else
	{
		StartAP = true;  //start AP if cannot connect
		WiFi.mode(WIFI_AP);  //access point only....if no client connect
		DebugPrintln("Can not connect to WiFi station. Go into AP mode.");
	}

	if (StartAP)  //have option to start with AP on always?
	{

		//WiFi.mode(WIFI_AP);   

		WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
		WiFi.softAP(hostname.c_str());

		if (cDNSdisable == false) {   //if captive dns allowed
			dnsServer.setTTL(300);
			dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
			dnsServer.start(53, "*", apIP);   //start dns catchall on port 53
			dsnServerActive = true;
		}

		DebugPrint("Soft AP started IP address: ");
		DebugPrintln(WiFi.softAPIP());
		// start DNS server for a specific domain name
		//dnsServer.start(DNS_PORT, "www.setup.com", apIP);
		//dnsServer.start(DNS_PORT, "*", apIP);
		//DebugPrintln("start AP");
	}
	else
		if (MDNSdisable == false) {
			MDNS.begin(DeviceName.c_str());  //multicast webname when not in SoftAP mode
			DebugPrintln("Starting mDSN " + DeviceName + ".local");
			MDNS.addService("http", "tcp", 80);
		}

//server stuffs:
// attach AsyncEventSource
//	server.addHandler(&events);

	server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", String(ESP.getFreeHeap()));
	});

	// upload a file to /upload
	server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
		request->send(200);
	}, onUpload);


	
	//server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

	server.on("/flashupdate", HTTP_POST, [](AsyncWebServerRequest *request) {
		shouldReboot = !Update.hasError();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
		response->addHeader("Connection", "close");
		request->send(response);
	}, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		if (!index) {
			Serial.printf("Update Start: %s\n", filename.c_str());
			Update.runAsync(true);
			if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
				Update.printError(Serial);
			}
		}
		if (!Update.hasError()) {
			if (Update.write(data, len) != len) {
				Update.printError(Serial);
			}
		}
		if (final) {
			if (Update.end(true)) {
				Serial.printf("Update Success: %uB\n", index + len);
			}
			else {
				Update.printError(Serial);
			}
		}
	});

	server.on("/jsonsave", handleJsonSave);
	server.on("/jsonload", handleJsonLoad);
	server.on("/serverlog", SendServerLog);
	server.on("/generate_204", handleRoot);  //use indexhtml or use embedded wifi setup...);
	server.on("/restartesp", restartESP);
	server.on("/availnets", SendAvailNetworks);
	server.on("/browse", HTTP_ANY, HandleFileBrowser);
	server.on("/formatspiff", formatspiffs);
	server.on("/", handleRoot);
	server.on("/info.html", sendNetworkStatus);
	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {	
		request->send(200, "text/html", "");
	
	});
	server.on("/scanwifi", HTTP_GET, [](AsyncWebServerRequest *request) {   //async scan we need to start scan before getting availnetworks after a delay....
		WiFi.scanDelete();
		WiFi.scanNetworks(true);
		request->send(200, "text/html", "");

	});


	// Catch-All Handlers
	// Any request that can not find a Handler that canHandle it
	// ends in the callbacks below.
	server.onNotFound(onRequest);
	server.onFileUpload(onUpload);
	server.onRequestBody(onBody);

#ifdef ALLOW_IDEOTA	     //if defined in h we will start/allow OTA via IDE.  should be disabled on your released code.

	//Send OTA events to the browser
	//ArduinoOTA.onStart([]() { SPIFFS.end(); OTAisflashing = true; DebugPrintln("OTA Started"); });
	ArduinoOTA.onStart(OTAStart);
	ArduinoOTA.onEnd([]() { shouldReboot = true; });
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DebugPrint(".");		
	});
	ArduinoOTA.onError([](ota_error_t error) {
		if (error == OTA_AUTH_ERROR) DebugPrintln("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) DebugPrintln("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) DebugPrintln("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) DebugPrintln("Recieve Failed");
		else if (error == OTA_END_ERROR) DebugPrintln("End Failed");
	});
	ArduinoOTA.setHostname(DeviceName.c_str());
	ArduinoOTA.begin();

#endif // Allow_IDEOTA


	CheckNewSystem();  //see if init files exist....


	server.begin();	

	OTAisflashing = false;
	isDownloading = false;
	WiFi.setAutoReconnect(true);
	DebugPrintln("HTTP server started");
	


	if (useNTP) {
		UDPNTPClient.begin(2390);  // Port for NTP receive
		setSyncProvider(getNtpTime);
		setSyncInterval(UpdateNTPEvery * 60);
		ResetTimeCheck = millis()-50000;  //check in 10 seconds if got ntptime (check every 60 seconds)
	}

	
	WiFi.scanNetworks(true);  //async needs to be called once on startup...
	DebugPrintln("wifi scan...");


	ServerLog("SERVER STARTED");
	DebugPrintln(CurTimeString());
#ifdef DEBUG
	Serial.setDebugOutput(true);
#endif
	
	



}

void MyWebServerClassAsync::handle()
{
	if (shouldReboot) {
		Serial.println("Rebooting...");
		delay(100);
		ESP.restart();
	}

if (dsnServerActive)  dnsServer.processNextRequest();  //captive dns	

if (JSONCallBack != "") {
	jsonSaveHandle(JSONCallBack);  //async last json file save;
	JSONCallBack = "";
}


if (useNTP) {
	if (millis() - ResetTimeCheck > 60000) {   //check every 60 seconds if time is good
		if (timeStatus() == timeNotSet) {
			setSyncProvider(getNtpTime);
			ResetTimeCheck = millis();
			DebugPrintln("re-check ntp");
		}
	}
}




#ifdef ALLOW_IDEOTA	     //if defined in h we will start/allow OTA via IDE.  should be disabled on your released code.
	ArduinoOTA.handle();
#endif // Allow_IDEOTA
}

bool MyWebServerClassAsync::WiFiLoadconfig()
{



	String values = "";
	dhcp = true;  //defaults;
	ssid = "empty";
	useNTP = false;


	File f = SPIFFS.open("/wifiset.json", "r");
	if (!f) {
		DebugPrintln("wifi config not set/found");
		return false;
	}
	else {  //file exists;
		values = f.readStringUntil('\n');  //read json         
		f.close();

		DynamicJsonBuffer jsonBuffer;


		JsonObject& root = jsonBuffer.parseObject(values);  //parse weburl
		if (!root.success())
		{
			DebugPrintln("parseObject() loadwifi failed");
			return false;
		}
		if (String(root["ssid"].asString()) != "") { //verify good json info                                                
			ssid = root["ssid"].asString();
			password = root["password"].asString();
			if (String(root["dhcp"].asString()).toInt() == 1) dhcp = true; else dhcp = false;
			IP[0] = String(root["ip_0"].asString()).toInt(); IP[1] = String(root["ip_1"].asString()).toInt(); IP[2] = String(root["ip_2"].asString()).toInt(); IP[3] = String(root["ip_3"].asString()).toInt();
			Netmask[0] = String(root["nm_0"].asString()).toInt(); Netmask[1] = String(root["nm_1"].asString()).toInt(); Netmask[2] = String(root["nm_2"].asString()).toInt(); Netmask[3] = String(root["nm_3"].asString()).toInt();
			Gateway[0] = String(root["gw_0"].asString()).toInt(); Gateway[1] = String(root["gw_1"].asString()).toInt(); Gateway[2] = String(root["gw_2"].asString()).toInt(); Gateway[3] = String(root["gw_3"].asString()).toInt();
			if (String(root["grabntp"].asString()).toInt() == 1) useNTP = true; else useNTP = false;

			ntpServerName = root["ntpserver"].asString();

			UpdateNTPEvery = String(root["update"].asString()).toInt();
			timezone = String(root["tz"].asString()).toInt();
			DeviceName = root["devicename"].asString();
			ConfigPassword = root["AccessPass"].asString();
			if (String(root["mDNSoff"].asString()) == "true") MDNSdisable = true; else  MDNSdisable = false;

			if (String(root["CDNSoff"].asString()) == "true") cDNSdisable = true; else  cDNSdisable = false;
			if (String(root["SoftAP"].asString()) == "true") SoftAPAlways = true; else  SoftAPAlways = false;


			if (String(root["grabntp"].asString()).toInt() == 1) useNTP = true; else useNTP = false;
			if (String(root["dst"].asString()).toInt() == 1) daylight = true; else daylight = false;

			if (String(root["Public"].asString()) == "true") AllowPublic = true; else  AllowPublic = false;

			DebugPrintln("all good");
			return true;
		}
	} //file exists;      
	return false; //error if here
}

void MyWebServerClassAsync::ServerLog(String logmsg)
{
	WebServerLog += "*" + String(month()) + String(day()) + CurTimeString() + "*" + logmsg + "<br>";
	if (WebServerLog.length() > 1024) { WebServerLog.remove(0, 256); }
}

bool MyWebServerClassAsync::isAuthorized(AsyncWebServerRequest *request)
{
	return isAdmin(request);
}

bool MyWebServerClassAsync::clientHasWeb()
{
	return true;
}


