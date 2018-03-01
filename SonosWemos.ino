/************************************************************************/
/* Sonos UPnP, an UPnP based read/write remote control library, v1.1.   */
/*                                                                      */
/* This library is free software: you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This library is distributed in the hope that it will be useful, but  */
/* WITHOUT ANY WARRANTY; without even the implied warranty of           */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU     */
/* General Public License for more details.                             */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */
/* along with this library. If not, see <http://www.gnu.org/licenses/>. */
/*                                                                      */
/* Written by Thomas Mittet (code@lookout.no) January 2015.             */
/************************************************************************/
/*

  Before you can control you Sonos speakers you need to enter their IP
  addresses and serial numbers below. You can find this information in the
  Sonos Controller application on your PC, Mac or phone by opening the
  "About My Sonos System" menu. For each device you will find information
  similar to this:

  PLAY:3: Bedroom
  Serial Number: 00-0A-74-7F-33-A7:8
  Version: 5.2 (build 28183040)
  Hardware Version: 1.8.1.2-2
  IP Address: 192.168.0.203

  Given the above, you configure the IP address and serial number (ID):
  IPAddress g_sonosBedroomIP(192, 168, 0, 203);
  const char g_sonosBedroomID[] = "000A747F33A7";

  When you are done with the configuration you can send commands to the
  speakers by entering commands in the Arduino Serial Monitor. The test
  sketch will mainly control the speaker named Living Room. The other
  speakers are only there to be able to test the group speakers function.

  Here's a list of the commands that are implemented in the test sketch:

  pl = Play
  pa = Pause
  st = Stop
  pr = Previous track
  nx = Next track

  fi = Play test file (file path must be changed for this to work)
  ht = Play http stream (you need access to the music service WIMP)
  ra = Play radio (works if your speakers are connected to the internet)
  li = Play line in (only works if the device has an AUX input)

  gr = Group speakers (Living Room, Bathroom and Bedroom)
  ug = Ungroups speakers

  re = Toggle repeat
  sh = Toggle shuffle
  lo = Toggle loudness
  mu = Toggle mute

  52 = Set volume level 52 (range is 00 - 99)
  b5 = Set bass level -5 (range is 0 to -9)
  B5 = Set bass level +5 (range is 0 to +9)
  t3 = Set treble level -3
  T0 = Set treble level normal

*/

#include <SonosUPnP.h>
#include <MicroXPath_P.h>
//#include <MicroXPath.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
MDNSResponder mdns;
#include <ArduinoOTA.h>

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>

PN532_I2C pn532_i2c(Wire);
NfcAdapter nfc = NfcAdapter(pn532_i2c);


extern "C" {
#include "user_interface.h"
}

#define SERIAL_DATA_THRESHOLD_MS 500
#define SERIAL_ERROR_TIMEOUT "E: Serial"
#define ETHERNET_ERROR_DHCP "E: DHCP"
#define ETHERNET_ERROR_CONNECT "E: Connect"

void handleSerialRead();
void ethConnectError();
//EthernetClient g_ethClient;
WiFiClient client;
SonosUPnP g_sonos = SonosUPnP(client, ethConnectError);

// Bathroom
IPAddress g_sonosBasementIP(192, 168, 1, 79);
const char g_sonosBasementID[] = "000E58207E00";

char uri[100] = "";
String lastCmd;

#include "FS.h"

File f;

#define HTTPPORT 88

ESP8266WebServer server(HTTPPORT);

void handleRoot();
void handleCmd();
void handleNotFound();
void handleResponse();
void handleGet();
void handleGt();

void setup()
{
  Serial.begin(115200);

  Serial.println("NDEF Reader");
  nfc.begin();

  WiFiManager wifiManager;

  wifiManager.autoConnect("sonos-config");

  Serial.print("Free memory:");Serial.println(system_get_free_heap_size());

  Serial.println("connected to WiFi");
  if (mdns.begin("sonos", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  ArduinoOTA.setHostname("sonos");
  ArduinoOTA.setPort(8268);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  /* File System stuff */

  SPIFFS.begin();

  Dir dir = SPIFFS.openDir("/");

  while (dir.next()) {
    Serial.println(dir.fileName());
    f = dir.openFile("r");
    Serial.println(f.size());
    //f.close();
  }

  /* WebServer stuff */

  server.on("/gong.mp3", []() {
    //File f2 = SPIFFS.open("/f.txt", "w");

    f.seek(0, SeekSet);
    Serial.println(f.position());
    size_t sent = server.streamFile(f, "audio/mpeg");
    Serial.print("sent gong ");Serial.println(sent);

  });

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/get", handleGt);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.print("HTTP server started on ");
  Serial.println(HTTPPORT);
}

void ethConnectError()
{
  Serial.println(ETHERNET_ERROR_CONNECT);
  Serial.println("Wifi died.");
}

void loop()
{
  ArduinoOTA.handle();
  server.handleClient();
  checkForTag();

}

void checkForTag()
{
  static unsigned long timeNow = 0;
  if (millis() > timeNow + 5000)
  {
    timeNow = millis();

    Serial.println("\nScan a NFC tag\n");
    if (nfc.tagPresent())
    {
      NfcTag tag = nfc.read();

      // tags contain a single message
      if (tag.hasNdefMessage())
      {
        // but messages have multiple records
        // records have types, but I'm assuming they're all strings
        NdefMessage msg = tag.getNdefMessage();
        unsigned int recordCount = msg.getRecordCount();
        if (recordCount == 0)
        {
          Serial.println("Couldn't find any records");
          return;
        }

        Serial.print("found records: ");Serial.println(recordCount);

        // the album id is in the first record
        NdefRecord record = msg[0];
        // skip the first 3 characters because it's the language
        String album = record.getPayload().substring(3);
        Serial.print("playing");Serial.println(album);
        g_sonos.playAlbum(g_sonosBasementIP, album);
      }
    }
  }
}

bool isCommand(const char *command, byte b1, byte b2)
{
  return *command == b1 && *++command == b2;
}

String handleGet(String cmd)
{
    Serial.println("Handling command ->" + cmd);
    if (cmd == "gv")
    {
      int volume = g_sonos.getVolume(g_sonosBasementIP);
      return String(volume);
    }
    else
    {
      Serial.println("*****************Don't know Command:::");Serial.println();
      return "-1";
    } 
}

void handleInput(String cmd, byte b1, byte b2)
{
  // Read 2 bytes from serial buffer
    // Play
    Serial.println("Handling command -->" + cmd);
    if (cmd == "pl")
    {
      g_sonos.play(g_sonosBasementIP);
    }
    // Pause
    else if (isCommand("pa", b1, b2))
    {
      g_sonos.pause(g_sonosBasementIP);
    }
    // Stop
    else if (isCommand("st", b1, b2))
    {
      g_sonos.stop(g_sonosBasementIP);
    }
    // Previous
    else if (isCommand("pr", b1, b2))
    {
      g_sonos.skip(g_sonosBasementIP, SONOS_DIRECTION_BACKWARD);
    }
    // Next
    else if (isCommand("nx", b1, b2))
    {
      g_sonos.skip(g_sonosBasementIP, SONOS_DIRECTION_FORWARD);
    }
    // Play File
    else if (isCommand("fi", b1, b2))
    {
      g_sonos.playFile(g_sonosBasementIP, "192.168.188.22/Music/ringtone/ring1.mp3");

    }
    // Play HTTP
    else if (isCommand("ht", b1, b2))
    {
      // Playing file from music service WIMP (SID = 20)
      //g_sonos.playHttp(g_sonosBasementIP, "trackid_37554547.mp4?sid=20&amp;flags=32");

      //g_sonos.playHttp(g_sonosBasementIP, "http://192.168.188.1:49200/AUDIO/DLNA-1-0/MXT-USB-StorageDevice-01/take_me_to_church.mp3");
      g_sonos.playHttp(g_sonosBasementIP, "http://192.168.188.28:88/gong.mp3");
    }
    // Play Radio
    else if (isCommand("ra", b1, b2))
    {
      g_sonos.playRadio(g_sonosBasementIP, "//lyd.nrk.no/nrk_radio_p3_mp3_h.m3u", "NRK P3");

    }
    // Play Line In
    else if (isCommand("li", b1, b2))
    {
      g_sonos.playLineIn(g_sonosBasementIP, g_sonosBasementID);
    }
    else if (isCommand("gs", b1, b2))
    {
      Serial.println("*****************Get Source:::");Serial.println();
      String response = "";
      char uri[25] = "";
      TrackInfo track = g_sonos.getTrackInfo(g_sonosBasementIP, uri, sizeof(uri));
      Serial.print("track -> ");Serial.println(track.uri);
      byte source = g_sonos.getSourceFromURI(track.uri);
      switch (source)
      {
        case SONOS_SOURCE_FILE:
          response += "File: ";
          break;
        case SONOS_SOURCE_HTTP:
          response += "HTTP: ";
          break;
        case SONOS_SOURCE_RADIO:
          response += "Radio: ";
          break;
        case SONOS_SOURCE_LINEIN:
          response += "Line-In: ";
          break;
        case SONOS_SOURCE_MASTER:
          response += "Other Speaker: ";
          break;
        default:
          response += "Unknown";
          break;
      }
      if (source == SONOS_SOURCE_FILE || source == SONOS_SOURCE_HTTP)
      {
        response += ", track = ";
        response += track.number;
        response += ", pos = ";
        response += track.position;
        response += " of ";
        response += track.duration;
      }
    }
    // Group
    else if (isCommand("al", b1, b2))
    {
      g_sonos.playAlbum(g_sonosBasementIP, "");
    }
    else if (isCommand("gr", b1, b2))
    {
//      g_sonos.playConnectToMaster(g_sonosBedroomIP, g_sonosBasementID);
//      g_sonos.playConnectToMaster(g_sonosBathroomIP, g_sonosBasementID);
    }
    // UnGroup
    else if (isCommand("ug", b1, b2))
    {
//      g_sonos.disconnectFromMaster(g_sonosBedroomIP);
//      g_sonos.disconnectFromMaster(g_sonosBathroomIP);
    }
    // Repeat On
    else if (isCommand("re", b1, b2))
    {
      g_sonos.setPlayMode(g_sonosBasementIP, SONOS_PLAY_MODE_REPEAT);
    }
    // Shuffle On
    else if (isCommand("sh", b1, b2))
    {
      g_sonos.setPlayMode(g_sonosBasementIP, SONOS_PLAY_MODE_SHUFFLE);
    }
    // Repeat and Shuffle On
    else if (isCommand("rs", b1, b2))
    {
      g_sonos.setPlayMode(g_sonosBasementIP, SONOS_PLAY_MODE_SHUFFLE_REPEAT);
    }
    // Repeat and Shuffle Off
    else if (isCommand("no", b1, b2))
    {
      g_sonos.setPlayMode(g_sonosBasementIP, SONOS_PLAY_MODE_NORMAL);
    }
    // Loudness On
    else if (isCommand("lo", b1, b2))
    {
      g_sonos.setLoudness(g_sonosBasementIP, true);
    }
    // Loudness Off
    else if (isCommand("l_", b1, b2))
    {
      g_sonos.setLoudness(g_sonosBasementIP, false);
    }
    // Mute On
    else if (isCommand("mu", b1, b2))
    {
      g_sonos.setMute(g_sonosBasementIP, true);
    }
    // Mute Off
    else if (isCommand("m_", b1, b2))
    {
      g_sonos.setMute(g_sonosBasementIP, false);
    }
    // Volume/Bass/Treble
    else if (b2 >= '0' && b2 <= '9')
    {
      // Volume 0 to 99
      if (b1 >= '0' && b1 <= '9')
      {
        g_sonos.setVolume(g_sonosBasementIP, ((b1 - '0') * 10) + (b2 - '0'));
      }
      // Bass 0 to -9
      else if (b1 == 'b')
      {
        g_sonos.setBass(g_sonosBasementIP, (b2 - '0') * -1);
      }
      // Bass 0 to 9
      else if (b1 == 'B')
      {
        g_sonos.setBass(g_sonosBasementIP, b2 - '0');
      }
      // Treble 0 to -9
      else if (b1 == 't')
      {
        g_sonos.setTreble(g_sonosBasementIP, (b2 - '0') * -1);
      }
      // Treble 0 to 9
      else if (b1 == 'T')
      {
        g_sonos.setTreble(g_sonosBasementIP, b2 - '0');
      }
    }

    else if (isCommand("ti", b1, b2))
    {
      Serial.println("we want the track uri");
      TrackInfo track = g_sonos.getTrackInfo(g_sonosBasementIP, uri, sizeof(uri));
      Serial.println(track.uri);
      Serial.println(uri);
    }

}

/* WebServer Stuff */

void handleRoot() {
  int vol = g_sonos.getVolume(g_sonosBasementIP);
  String msg = "<html>\n";
  msg += "<head>\n";
  msg += "<title>ESP8266 Sonos Controller</title>\n";
  msg += "<link rel=\"stylesheet\" type=\"text/css\" href=\"http://joeybabcock.me/iot/hosted/hosted-sonos.css\">";
  msg += "<script src=\"https://code.jquery.com/jquery-3.1.1.min.js\"></script>\n";
  msg += "<script src=\"http://joeybabcock.me/iot/hosted/hosted-sonos.js\"></script>\n";
  msg += "</head>\n";
  msg += "<body>\n";
  msg += "<div id=\"container\">\n";
  msg += "<h1>Sonos - Esp8266 Web Controller!</h1>\n";
  msg += "<p id=\"linkholder\"><a href=\"#\" onclick=\"sendCmd('pr');\"><img src=\"http://joeybabcock.me/iot/hosted/rw.png\"/></a> \n";
  msg += "<a href=\"#\" onclick=\"sendCmd('pl');\"><img src=\"http://joeybabcock.me/iot/hosted/play.png\"/></a> \n";
  msg += "<a href=\"#\" onclick=\"sendCmd('pa');\"><img src=\"http://joeybabcock.me/iot/hosted/pause.png\"/></a> \n";
  msg += "<a href=\"#\" onclick=\"sendCmd('nx');\"><img src=\"http://joeybabcock.me/iot/hosted/ff.png\"/></a></p>\n";
  msg += "<h3>Volume: <span id=\"vol\">"+String(vol)+"</span><input type=\"hidden\" id='volume' value='"+String(vol)+"' onchange=\"setVolume(this.value)\"/></h3><br/>\n";
  msg += "<input type=\"range\" class=\"slider\"  min=\"0\" max=\"99\" value=\""+String(vol)+"\" name=\"volume-slider\" id=\"volume-slider\" onchange=\"setVolume(this.value)\" />\n";
  msg += "<p>Server Response:<div id=\"response\" class=\"response\"></div></p>\n";
  msg += "<p><form action=\"/\" method=\"get\" id=\"console\"><input placeholder=\"Enter a command...\" type=\"text\" id='console_text'/></form></p>\n";
  msg += "<script>var intervalID = window.setInterval(getVolume, 5000);\n$('#console').submit(function(){parseCmd($(\"#console_text\").val());\nreturn false;\n});\n</script>\n";
  msg += "</div>\n";
  msg += "<div id=\"tips\"></div>\n";
  msg == "</body>\n";
  msg += "</html>\n";
  server.send(200, "text/html", msg);
}

void handleCmd(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i) == "cmd") 
    {
      lastCmd = server.arg(i);
      byte b1 =  server.arg(i)[0];
      byte b2 = server.arg(i)[1];
      handleInput(lastCmd,b1,b2);
    }
  }
  handleResponse();
}

void handleGt(){
  String resp;
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i) == "cmd") 
    {
      lastCmd = server.arg(i);
      //byte b1 =  server.arg(i)[0];
      //byte b2 = server.arg(i)[1];
      resp = handleGet(lastCmd);
    }
  }
  handleGetResponse(resp);
}

void handleNotFound() {

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);

}

void handleResponse() {
      server.send(200, "text/html", "Worked("+lastCmd+")");
      Serial.println("Got client.");
}

void handleGetResponse(String response) {
      server.send(200, "text/html", response);
      Serial.print("Got client: "); Serial.println(response);
}
