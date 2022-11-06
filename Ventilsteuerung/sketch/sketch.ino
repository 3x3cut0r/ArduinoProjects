/**
  Ventilsteuerung für Heizung
  mit 4 Zeilen Display
  und Soll-Temperatur-Bedienung

  Copyright (C) 2022, 3x3cut0r

  Released under the MIT License.
*/

/** 
  Includes
*/
#include <OneWire.h> // Temperatur Library
#include <DallasTemperature.h> // Temperatur Library für DS18B20
#include <LiquidCrystal_I2C.h> // LCD Library für 20x4 I2C Display
#include <EEPROM.h> // EEPROM um die Solltemperaturen dauerhaft zu speichern

/** 
  ==================================================
  Änderbare Variablen
  ==================================================
*/

/** 
 * 1. Startverzögerung (in Sekunden)
 * die gewartet wird bevor die erste Temperaturanpassung stattfindet
 * Default = 660
 * 
 * Zulässige Werte = 0-65535
 * Maximaler Wert entspricht 18h 12m 15s
 * (wegen Arduino Uno Speicherbegrenzung von 32 bit (unsigned int))
 */
const unsigned int DELAY_BEFORE_START_1 = 660;

/** 
 * 2. Startverzögerung (in Sekunden)
 * die gewartet wird bevor die erste Temperaturanpassung stattfindet
 * Default = 600
 * 
 * Zulässige Werte = 0-65535
 * Maximaler Wert entspricht 18h 12m 15s
 * (wegen Arduino Uno Speicherbegrenzung von 32 bit (unsigned int))
 */
const unsigned int DELAY_BEFORE_START_2 = 600;

/** 
 * Zeit (in Millisekunden)
 * wie lang das Relais beim Einschalten einmalig schaltet
 * Default = 3000
 * 
 * Zulässige Werte = 0-65535
 * Maximaler Wert entspricht 65s 535ms
 * (wegen Arduino Uno Speicherbegrenzung von 32 bit (unsigned int))
 */
const unsigned int INIT_RELAIS_TIME = 3000;

/** 
 * Zeit (in Sekunden)
 * bis zur nächsten Temperaturmessung
 * Default = 180
 * 
 * Zulässige Werte = 0-65535
 * Maximaler Wert entspricht 18h 12m 15s
 * (wegen Arduino Uno Speicherbegrenzung von 32 bit (unsigned int))
 */
const unsigned int UPDATE_TIME = 180; // Sekunden

/** 
 * Zeit (in Millisekunden)
 * wie lang das Relais schaltet
 * Default = 2000
 * 
 * Zulässige Werte = 0-65535
 * Maximaler Wert entspricht 65s 535ms
 * (wegen Arduino Uno Speicherbegrenzung von 32 bit (unsigned int))
 */
const unsigned int RELAIS_TIME = 2000;

/** 
 * Minimale Solltemperatur (in Grad Celsius)
 * Default = 300
 * 
 * Zulässige Werte = 0.0 - 120.0
 * Bedinung: NOMINAL_MIN_TEMP >= NOMINAL_MAX_TEMP
 */
const float NOMINAL_MIN_TEMP = 45.0;

/** 
 * Maximale Solltemperatur (in Grad Celsius)
 * Default = 300
 * 
 * Zulässige Werte = 0.0 - 120.0
 * Bedinung: NOMINAL_MIN_TEMP >= NOMINAL_MAX_TEMP
 */
const float NOMINAL_MAX_TEMP = 60.0; 

/** 
 * Hintergrundbeleuchtung des LCD I2C Displays
 * Default = 1
 * 
 * Zulässige Werte = 0 oder 1
 * 0 = Hintergrundbeleuchtung aus
 * 1 = Hintergrundbeleuchtung an
 */
const int LCD_I2C_BACKLIGHT = 1;

/** 
 * Zeitkorrektur (in Millisekunden)
 * Default = 165
 * 
 * Ein Zyklus besteht aus einer Sekunde.
 * In dieser Sekunde finden Rechenaufgaben (x) statt (Messungen, Schaltungen, etc ...).
 * Die Zeit der Rechenaufgaben lässt sich nicht ermitteln.
 * Nach den Rechenaufgaben wird mit einem delay eine kurze Zeit gewartet bis der nächste Zyklus beginnt.
 * 
 * Mit SECOND_REDUCER gibt man die geschätze Zeit für x an, welche vom delay abgezogen wird.
 * x + delay sollte also genau 1 Sekunde betragen (1000ms), damit am Ende die Zeitangaben stimmen.
 * 
 * Zulässige Werte = 0 bis 1000 (in Millisekunden)
 * 0 = es wird nichts abgezogen
 * 1000 = es wird 1 Sekunde abgezogen
 */
const int SECOND_REDUCER = 165;



/** 
  ==================================================
  Feste Variablen (bitte nicht ändern!)
  ==================================================
*/

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C_ADDR, LCD_COLUMNS, LCD_LINES

// Temperatursensor DS18B20
#define ONE_WIRE_BUS 8
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Relais
const int RELAY_OPEN_PIN = 4; // Relais, welches öffnet, wenn es wärmer werden soll
const int RELAY_CLOSE_PIN = 5; // Relais, welches öffnet, wenn es kälter werden soll

// Temps
float currentTemp = 0; // aktuelle Temperatur (in Grad Celsius)
unsigned int updateTime = UPDATE_TIME; // Zeit (in Sekunden), bis zur nächsten Angleichung
float nominalMinTemp = NOMINAL_MIN_TEMP; // eingestellte Minimale Solltemperatur (in Grad Celsius)
float nominalMaxTemp = NOMINAL_MAX_TEMP; // eingestellte Maximale Solltemperatur (in Grad Celsius)
const int nominalMinTempAddress = 0; // Speicheradresse (int), im EEPROM der Minimalen Solltemperatur
const int nominalMaxTempAddress = 4; // Speicheradresse (int), im EEPROM der Maximalen Solltemperatur

// Buttons
const int BUTTON_TEMP_UP_PIN = 2; // PIN des Buttons Solltemperatur senken
const int BUTTON_TEMP_DOWN_PIN = 3; // PIN des Buttons Solltemperatur erhöhen



/** 
 * ==================================================
 * Funktionen
 * ==================================================
 */

/** 
 * gleicht die Zeit (time) eines delays mit einer Sekunde
 * mittels SECOND_REDUCER an
 */
void delayTime(int time) {
  int reducer = SECOND_REDUCER;
  if(SECOND_REDUCER > 1000 || SECOND_REDUCER < 0){
    reducer = 165;
  }
  time = time - reducer;
  delay(time);
}

/** 
 * speichert einen Wert (value) in der übergebenen Adresse (address)
 */
void toEEPROM(int address, float value) {
  EEPROM.put(address, value);
}

/** 
 * liest einen Wert (value) aus der übergebenen Adresse (address)
 */
float getEEPROM(int address) {
  float temp;
  EEPROM.get(address, temp);
  if (temp >= 0 && temp <= 300) {
    return temp;
  } else {
    return 0;
  }
}

/** 
 * liest die minimale Solltemperatur aus dem EEPROM und gibt sie zurück
 */
void readNominalMinTemp() {
  float temp = getEEPROM(nominalMinTempAddress);
  if (temp == 0) {
    temp = NOMINAL_MIN_TEMP;
  }
  nominalMinTemp = temp;
}

/** 
 * liest die maximale Solltemperatur aus dem EEPROM und gibt sie zurück
 */
void readNominalMaxTemp() {
  float temp = getEEPROM(nominalMaxTempAddress);
  if (temp == 0) {
    temp = NOMINAL_MAX_TEMP;
  }
  nominalMaxTemp = temp;
}

/** 
 * aktualisiert die minimale Solltemperatur im EEPROM (falls geändert)
 */
void updateNominalMinTemp() {
  toEEPROM(nominalMinTempAddress, nominalMinTemp);
}

/** 
 * aktualisiert die maximale Solltemperatur im EEPROM (falls geändert)
 */
void updateNominalMaxTemp() {
  toEEPROM(nominalMaxTempAddress, nominalMaxTemp);
}

/** 
 * druckt (print) den übergebenen text die Zeile (line)
 * an der übergebenen Position (cursor)
 * auf das 4 Zeilen, 20 Zeichen Display
 */
void printLCD(int line, int cursor, String text) {
  lcd.setCursor(cursor, line);
  lcd.print(text);
}

/** 
 * liest die Temperatur vom Thermostat und gibt sie als float zurück
 */
float getTemp() {
  sensors.requestTemperatures(); // Send the command for all devices on the bus to perform a temperature conversion:
  float tempC = sensors.getTempCByIndex(0); // the index 0 refers to the first device
  return tempC;
}

/** 
 * aktualisiert die aktuelle Temperatur auf dem Display
 */
void updateTemp() {
  currentTemp = getTemp();
  String currentTempString = String(currentTemp, 1) + " \337C";
  int tempPos = 20 - currentTempString.length();
  printLCD(0, 0, "Aktuell:            ");
  printLCD(0, tempPos, currentTempString);
}

/** 
 * aktualisiert die Solltemperatur auf dem Display
 */
void printNominalTemp() {
  printLCD(1, 0, "Soll:               ");
  if (nominalMinTemp < 0) { nominalMinTemp = 0.0; }
  if (nominalMinTemp > 120.0) { nominalMinTemp = 120.0; }
  if (nominalMaxTemp < 0) { nominalMaxTemp = 0.0; }
  if (nominalMaxTemp > 120.0) { nominalMaxTemp = 120.0; }
  if (nominalMaxTemp < nominalMinTemp) { nominalMaxTemp = nominalMinTemp; }
  String nominalTemp = String(nominalMinTemp, 1) + " - " + String(nominalMaxTemp, 1) + " \337C";
  int tempPos = 20 - nominalTemp.length();
  printLCD(1, tempPos, nominalTemp);
}

/** 
 * schaltet das Relais (relayPin)
 * über die übergebene Zeit (relayTime)
 */
void setRelais(int relayPin, int relayTime) {
  if (relayPin == RELAY_OPEN_PIN) {
    printLCD(3, 0, "\357ffne Ventil     >>>");
  } else if (relayPin == RELAY_CLOSE_PIN) {
    printLCD(3, 0, "schlie\342e Ventil: <<<");
  }
  digitalWrite(relayPin, HIGH);
  delay(relayTime);
  digitalWrite(relayPin, LOW);
}

/** 
 * schaltet das jeweilige Relais abhängig von der Solltemperatur 
 */
void openRelais(int relayTime) {
  if (currentTemp < NOMINAL_MIN_TEMP) {
      // erhöhe Temperatur
      setRelais(RELAY_OPEN_PIN, relayTime);
    } else if (currentTemp > NOMINAL_MAX_TEMP) {
      // verringere Temperatur
      setRelais(RELAY_CLOSE_PIN, relayTime);
    } else {
      printLCD(3, 0, "Soll Temp erreicht !");
    }
}

/** 
 * erhöht oder verringert die Solltemperatur
 */
void updateNominalTemp(int buttonPin) {
  int buttonLong = 0;
  float rate = 0.1;
  while (digitalRead(buttonPin) == LOW) {
      if (buttonPin == BUTTON_TEMP_UP_PIN) {
        if (rate >= 3) {
          printLCD(2, 0, "TempUp Pressed   +++");
        } else if (rate >= 0.5) {
          printLCD(2, 0, "TempUp Pressed    ++");
        } else {
          printLCD(2, 0, "TempUp Pressed     +");
        }
        nominalMinTemp = nominalMinTemp + rate;
        nominalMaxTemp = nominalMaxTemp + rate;
      } else if (buttonPin == BUTTON_TEMP_DOWN_PIN) {
        if (rate >= 3) {
          printLCD(2, 0, "TempDown Pressed ---");
        } else if (rate >= 0.5) {
          printLCD(2, 0, "TempDown Pressed  --");
        } else {
          printLCD(2, 0, "TempDown Pressed   -");
        }
        nominalMinTemp = nominalMinTemp - rate;
        nominalMaxTemp = nominalMaxTemp - rate;
      }
      printNominalTemp();
      delay(500);
      buttonLong = buttonLong + 1;
      if (buttonLong == 5) {
        rate = 1;
      } else if (buttonLong == 10) {
        rate = 5;
      }
  }
  printLCD(2, 0, "                    ");
}

/** 
 * prüft ob ein TempButton gedrückt ist und
 * ändert entsprechend die Solltemperatur
 */
void checkButtons() {
  updateNominalTemp(BUTTON_TEMP_UP_PIN);
  updateNominalTemp(BUTTON_TEMP_DOWN_PIN);
}

/** 
 * warte die übergebene Zeit (seconds) vor dem Start
 */
void waitStart(unsigned int secs) {
  int counter = secs;
  while (counter > 0) {

    // prüfe ob Button gedrückt
    checkButtons();

    // aktualisiere aktuelle Temperatur
    updateTemp();

    secs = counter;
    String statusWait = "STARTE IN:          ";
    String time = "";

    // int years = (int) (secs / (60*60*24*365));
    // secs     -= years       * (60*60*24*365);
    // int days  = (int) (secs / (60*60*24));
    // secs     -= days 	      * (60*60*24);
    int hours = (int) (secs / (60*60));
    secs     -= hours       * (60*60);
    int mins  = (int) (secs / (60));
    secs     -= mins        * (60);

    if (hours > 0) { time = time + hours + "h "; }
    time = time + mins  + "m " + secs  + "s"; 

    int empty = 20 - time.length();
    printLCD(3, 0, statusWait);
    printLCD(3, empty, time);

    counter = counter - 1;

    // warte 1 Sekunde
    delayTime(1000);
  }
}

/** 
 * aktualisiert die Uhrzeit des Timers auf dem Display
 */
void updateTimer(unsigned int secs) {
  String statusWait = "WARTE:              ";
  String time = "";

  // int years = (int) (secs / (60*60*24*365));
  // secs     -= years       * (60*60*24*365);
  // int days  = (int) (secs / (60*60*24));
  // secs     -= days 	      * (60*60*24);
  int hours = (int) (secs / (60*60));
  secs     -= hours       * (60*60);
  int mins  = (int) (secs / (60));
  secs     -= mins        * (60);

  if (hours > 0) { time = time + hours + "h "; }
  time = time + mins  + "m " + secs  + "s"; 

  int empty = 20 - time.length();
  printLCD(3, 0, statusWait);
  printLCD(3, empty, time);
}



/** 
 * ==================================================
 * initialisiert alle Bauteile
 * ==================================================
 */
void setup() {

  // Init LCD
  lcd.init();
  if(LCD_I2C_BACKLIGHT == 1) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }

  // Init Temperatursensor
  sensors.begin();

  // setze Relais pinMode
  pinMode(RELAY_OPEN_PIN, OUTPUT);
  pinMode(RELAY_CLOSE_PIN, OUTPUT);

  // setze Button pinMode
  pinMode(BUTTON_TEMP_UP_PIN, INPUT_PULLUP); 
  pinMode(BUTTON_TEMP_DOWN_PIN, INPUT_PULLUP); 

  // aktualisiere aktuelle Temperatur
  updateTemp();

  // setze Solltemperatur
  readNominalMinTemp();
  readNominalMaxTemp();
  printNominalTemp();

  // Startverzögerung
  waitStart(DELAY_BEFORE_START_1); // warte auf Start 1
  setRelais(RELAY_CLOSE_PIN, INIT_RELAIS_TIME); // öffne Ventil initial
  waitStart(DELAY_BEFORE_START_2); // warte auf Start 2

  // öffne Ventil
  openRelais(RELAIS_TIME);
}



/** 
 * ==================================================
 * Schleife des Hauptprogramms
 * ==================================================
 */
void loop() {

  // warte 1 Sekunde
  delayTime(1000);

  // prüfe ob Button gedrückt
  checkButtons();

  // aktualisiere Temperatur
  updateTemp();

  // öffne Ventil
  if (updateTime == 0) {
    openRelais(RELAIS_TIME);
  }

  // reset Timer
  if (updateTime == 0) {
    updateTime = UPDATE_TIME;
    updateNominalMinTemp();
    updateNominalMaxTemp();
  }

  // aktualisiere Timer
  if (updateTime > 0) {
    updateTimer(updateTime);
    updateTime--;
  }
}
