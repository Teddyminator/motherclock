
 //https://youtu.be/vTNIgLoh7OU
/************************( Importieren der genutzten Bibliotheken )************************/

#include <ESP8266WiFi.h>                    //https://github.com/esp8266/Arduino
#include "WiFiManager.h"                    //https://github.com/tzapu/WiFiManager
#include <Timezone.h>                       //https://github.com/JChristensen/Timezone
#include <TimeLib.h>                        //https://github.com/PaulStoffregen/Time
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#define OLED_RESET   10
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
Adafruit_SSD1306 display(OLED_RESET);

int h, m, s, w, mo, ye, d;                  // variablen für die lokale Zeit
int brightnessPin = A0;
int brightness = 0;

// NTP Serverpool für die Deutschland:
static const char ntpServerName[] = "de.pool.ntp.org";    //Finde lokale Server unter http://www.pool.ntp.org/zone/de
const int timeZone = 0;                     // 0 wenn mit <Timezone.h> gearbeitet wird sonst stimmt es nachher nicht
WiFiUDP Udp;
unsigned int localPort = 8888;              // lokaler Port zum Abhören von UDP-Paketen

time_t getNtpTime();
void serialprintclock();
void printtodisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

// - Timezone. - //
// Bearbeiten Sie diese Zeilen entsprechend Ihrer Zeitzone und Sommerzeit.
// TimeZone Einstellungen Details https://github.com/JChristensen/Timezone
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Time (Frankfurt, Paris)
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Time (Frankfurt, Paris)
Timezone CE(CEST, CET);


void setup() {
  Serial.begin(115200);
  
  Wire.begin(D2,D1);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  WiFiManager wifiManager;  
   //wifiManager.resetSettings();          //mit diesem befehl kannst die gespeicherten werte löschen              
  wifiManager.autoConnect("Motherclock");
  Serial.println("verbunden!");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Verbunden!");
  display.display();
  Udp.begin(localPort);
  Serial.print("lokaler Port: ");
  Serial.println(Udp.localPort());
  Serial.println("Warten auf die Synchronisation");
  setSyncProvider(getNtpTime);
  setSyncInterval(3600);  // Anzahl der Sekunden zwischen dem erneuten Synchronisieren ein. 86400 = 1 Tag
  
  
}

time_t prevDisplay = 0; // wenn die Digitaluhr angezeigt wurde

void loop()
{
  if (timeStatus() != timeNotSet) {             // wenn die Zeit über NTP gesetzt wurde
  lokaleZeit();
  }
  serialprintclock();
  brightness = analogRead(brightnessPin);
  Serial.print("Helligkeit");
  Serial.println(brightness);
  
  if (brightness > 900) {
    printtodisplay();
  } 
  else {
  display.clearDisplay();
  display.display();
  }  
  
}


void lokaleZeit()
{
  time_t tT = now();
  time_t tTlocal = CE.toLocal(tT);
  w = weekday(tTlocal);
  d = day(tTlocal);
  mo = month(tTlocal);
  ye = year(tTlocal);
  h = hour(tTlocal);
  m = minute(tTlocal);
  s = second(tTlocal);
  
  
}

void printtodisplay()
{
  display.clearDisplay();         //Anzeige auf dem OLED
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(15,18);
  printDigit(h);
  display.print(":");
  printDigit(m);
  display.print(":");                 
  printDigit(s);
  display.setTextSize(2);
  display.setCursor(5,0);  
  printDigit(d);
  display.print(".");
  printDigit(mo);
  display.print(".");
  display.print(ye);
  display.display();
}


void serialprintclock()
{
  // digitale Uhrzeitanzeige
  Serial.print(h);
  printDigits(m);
  printDigits(s);
  Serial.print(" ");
  Serial.print(d);
  Serial.print(".");
  Serial.print(mo);
  Serial.print(".");
  Serial.println(ye);
}

void printDigits(int digits)
{
  // Utility für digitale Uhrenanzeige: druckt vor Doppelpunkt und führende 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void printDigit(int digits)
{
  // Utility für digitale Uhrenanzeige: druckt vor Doppelpunkt und führende 0
  
  if (digits < 10)
    display.print('0');
  display.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP-Zeit in den ersten 48 Bytes der Nachricht
byte packetBuffer[NTP_PACKET_SIZE]; //Puffer für eingehende und ausgehende Pakete

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip Adresse

  while (Udp.parsePacket() > 0) ; // alle zuvor empfangenen Pakete verwerfen
  Serial.println("Transmit NTP Request");
  // einen zufälligen Server aus dem Pool holen
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // Paket in den Puffer einlesen
      unsigned long secsSince1900;
      // vier Bytes ab Position 40 in eine lange Ganzzahl umwandeln
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("keine NTP Antwort");
  return 0; // gibt 0 zurück, wenn die Zeit nicht ermittelt werden kann.
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // alle Bytes im Puffer auf 0 setzen
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialisieren von Werten, die für die Bildung von NTP-Requests benötigt werden.
  // (siehe URL oben für Details zu den Paketen)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // alle NTP-Felder wurden jetzt mit Werten versehen
  // Sie können ein Paket senden, das einen Zeitstempel anfordert.:
  Udp.beginPacket(address, 123); //NTP-Requests sollen auf Port 123 erfolgen
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
