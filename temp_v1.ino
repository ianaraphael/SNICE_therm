/*
  temp_v1.ino

  first version of the temperature datalogger code, based on Pat Saylor's piezo
  datalogger code.

  Ian Raphael
  ian.th@dartmouth.edu
  2021.07.12

*/


/************ Libraries ************/

// clock
#include <RTCZero.h>

// memory
#include <SerialFlash.h>
#include <SD.h>

// sensors
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>

// radio
// TODO: LORA or RH_RF95?
#include <LoRa.h>
#include <RH_RF95.h>


// compass/gps
#include <Adafruit_GPS.h>
#include <Adafruit_LSM303_U.h>

// other
#include <SPI.h>
#include <RHReliableDatagram.h>
#include <timestamp.h>
#include <timestamp32bits.h>
#include <string>

/************ macros & globals ************/

/****** user set variables ******/

/*!!!!!!!! impt: define self ID. Must be unique id for each logger !!!!!!!!*/
#define SELF_ADDRESS 28 // address for self (distributed client/loggers)
#define SERVER_ADDRESS 1 // address for receiver (central server/base stn.)
const uint8_t NUM_TEMP_SENSORS = 5; // define the number of temp sensors

int Dspeed = 5000; // Data collection interval (milliseconds)
String tempFilename = "tempData_" + SELF_ADDRESS + ".txt"; // temp data storage file name on SD card
String pingerFilename = "pingerData_" + SELF_ADDRESS + ".txt"; // pinger data storage file name on SD card

int numSensors = 3;  // set number of sensors **COUNT the REFERENCE as a sensor** eg 3 sensors + ref = 4 sensors
int protoID = 21; // SET Unique ID number for each prototype, does not have to be fixed number of characters, but unique required

uint8_t user_hour = 12;
uint8_t user_minute = 0;
uint8_t user_sec = 0;
uint8_t user_day = 0;
uint8_t user_month = 1;
uint8_t user_year = 2021;



#define Serial SerialUSB // usb port
#define pinger Serial // put the snow pinger on Serial port
#define TEMP_BUS 12 // temp probe data line

#define TEMP_POWER 6 // temp probe power
#define PINGER_POWER 7 // snow pinger power

#define RADIO_CS 5 // radio chip select
#define RADIO_INT 2 // radio interrupt pin
#define RADIO_FREQ 915.0 // radio frequency

// define SPI pins:
// const uint8_t SD_PIN = 10; // this pin isn't in use as the Piezo board is built
const uint8_t FLASH_CS 4 // flash chip select

// Sleep cycles
int sleepCycles = 0; // track n of times we've slept. use to determine when to TX to base
const int cycleMatch = 24; // n of sleep cycles (therefore n files gen'd) before we transmit
int lastFile; // track the last file we sent so we know where to start on next round

// data size defs
const uint8_t pingerReadSize = 2;
const uint8_t adcReadSize = 2;
const uint8_t tempDataSize = 2;
const uint8_t tempIDSize = 8;
const uint8_t filenameSize = 12;

// File stuff
char* filenames[1000]; // array of pointers to hold filenames
int writeBufSize = (tempDataSize * NUM_TEMP_SENSORS) + pingerReadSize + adcReadSize; // size of the buffer for writing data to file
int readBufSize = (tempDataSize * NUM_TEMP_SENSORS) + (tempIDSize * NUM_TEMP_SENSORS) + filenameSize + pingerReadSize + adcReadSize + 3; // size of the buffer for reading data off of file
int fileSi
//File Setup
String dataString = "";      // buffer storage variable
File myFile;                 // fileholder name placeholder

RTCZero rtc;  // initialize Real Time Clock Object
int counter = 0; // LORA MESSAGE COUNTER

/************ Instrumentation defs ************/

// Radio
RH_RF95 driver(RADIO_CS, RADIO_INT); // radio driver
RHReliableDatagram manager(driver, SELF_ADDRESS); // delivery/receipt manager
uint8_t messageBuf[RH_RF95_MAX_MESSAGE_LEN]; // buf for TX/RX handshake messages
int shakeLimit = 5; // limit for nTimes to try handshake with server

// Temp sensors
// create oneWire object for temp probe comms
OneWire oneWire(TEMP_BUS);
// and create the Dallas temp object on top that we'll actually be working with
DallasTemperature tempSensors(&oneWire);
uint8_t tempAddrx[NUM_TEMP_SENSORS][8]; // temp probe addresses
// OneWire  ds(7);  // on pin 7 (a 4k7 resistor is necessary) // this is holdover from Pat's code

// compass/accelerometer
Adafruit_LSM303_Mag_Unified compass = Adafruit_LSM303_Mag_Unified(12345); // assign id

// GPS
#define GPSSerial Serial1 // define Serial1 as GPS port
Adafruit_GPS GPS(&GPSSerial); // Mount the GPS on its port
#define GPSECHO false;
uint32_t timer = millis();
uint8_t base_sec = 0;
uint8_t sample_interval = 15;
boolean usingInterrupt = false;

// Other
const int ledPin =  13; // the number of the LED pin -- for diagnostic refs
String loopTemp; // temp looper
String loopTemp_stor; // temp looper

TODO:// deconflict pin 2
int gpsPin = 2; // set gps enable pin. (Radio chip select is also on this pin. Conflict?)
int gpsState = HIGH; // gps_state HIGH is on to start // high = on, low = off
long OnTime = 3600500; //1hr in milliseconds, with an offset to not fall exactly on the minute 500 ms
long OffTime = 82800500; // 23hr in milliseconds, with 500 ms offset
unsigned long previousMillis = 0; // set state timer to 0 for init


/********** SETUP *************   <START> */
void setup() {

  // add in pin func for  STATE MACHINE
  pinMode(gpsPin, OUTPUT);


  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  //while (!Serial) {} // wait for serial port to connect. Needed for native USB port only

  // initialize flash and SD memory
  //init_flash();

  init_SD();


  // GET GPS FIX HERE ***
  init_GPS();


  // Set RTC Time: <eventually from GPS>
  init_RTC();
  alarm_one();


} //SETUP <END>


/* ********* LOOP************* <START> */

void loop() {

  unsigned long currentMillis = millis();

  if((gpsState == HIGH) && (currentMillis - previousMillis >= OnTime)) // pgs turn-off timer
  {
    Serial.println('gps on, turning off');

    gpsState = LOW;  // Turn it off
    previousMillis = currentMillis;  // Remember the time
    digitalWrite(gpsPin, gpsState);  // Update the actual LED
  }
  else if ((gpsState == LOW) && (currentMillis - previousMillis >= OffTime)) // gps turn-on timer
  {
   Serial.println('gps off, turning on');

    gpsState = HIGH;  // turn it on
    previousMillis = currentMillis;   // Remember the time
    digitalWrite(gpsPin, gpsState);   // Update the actual LED
  }


  if (gpsState == HIGH) // update RTC only when gps is on
  {
    gps_to_rtc();
  }

}
// LOOP <END>




/* FUNCTIONS */

void init_flash(){

// Initialize flash and SD
SerialFlash.begin(FLASH_CS);
SerialFlash.wakeup();

 // If connection to flash fails
  if (!SerialFlash.begin(FLASH_CS)) {

    // print error
    Serial.println("Unable to access SPI Flash chip");
    while (1);
  }

  // otherwise print success
  Serial.println("Able to access SPI flash chip");

  // print chip capacity
  unsigned char id[5];
  SerialFlash.readID(id);
  unsigned long size = SerialFlash.capacity(id);
  Serial.print("Flash memory capacity (bytes): ");
  Serial.println(size);

  // erase everything on the chip
  Serial.println("Erasing everything:");
  SerialFlash.eraseAll();
  Serial.println("Done erasing everything");
} // init_flash() <END>

//-----------------------------------------

void init_SD(){

  delay(100);
 // set SS pins high for turning off radio
  pinMode(RADIO_PIN, OUTPUT);
  delay(50);
  digitalWrite(RADIO_PIN, HIGH);
  delay(50);
  pinMode(SD_PIN, OUTPUT);
  delay(50);
  digitalWrite(SD_PIN, LOW);
  delay(100);
  Serial.println("SD init -->");
  SD.begin(SD_PIN);
  delay(200);
if (!SD.begin(SD_PIN)) {
    Serial.println("initialization failed!");
    while (1);
  }
else {
	Serial.println("SD initialization GREEN");
}

} //init_SD() <END>

//----------------------------------------

void init_RADIO(){

    delay(100);
   // set SD pins high for turning off radio
    pinMode(RADIO_PIN, OUTPUT);
    delay(50);
    digitalWrite(RADIO_PIN, LOW);
    delay(50);
    pinMode(SD_PIN, OUTPUT);
    delay(50);
    digitalWrite(SD_PIN, HIGH);
    delay(100);
    Serial.println("init_radio -->");
} // init_RADIO <END>

//----------------------------------------

void init_LORA(){
  LoRa.setPins(5,3,2);
  LoRa.setSPIFrequency(9600);

  //while (!Serial){};

  Serial.println("LoRa Sender");

  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

} // init_LORA <END>

//----------------------------------------

void send_LORA(String _msgData){

//setup packet:
  LoRa.beginPacket();
  LoRa.print(_msgData);
  LoRa.endPacket();

  delay(1000); // allow delay for message send (?)
  //_counter ++
  //return _counter
}

void init_RTC(){
     // init the RTC
    rtc.begin();
    rtc.setTime(user_hour, user_minute, user_sec);
    rtc.setDate(user_day, user_month, user_year);
    //rtc.setTime(15, 0, 0);
    //rtc.setDate(5, 2, 2020);

} // init_RTC() <END>

//----------------------------------------

void alarm_one() {
  rtc.setAlarmSeconds(base_sec);
  rtc.enableAlarm(rtc.MATCH_SS);
  rtc.attachInterrupt(alarm_one_routine);
} // alarm_one <END>

//----------------------------------------

void alarm_one_routine() {
  Serial.println("alarm");

  // put interval events here, most likely, save to SD & radio send
  // this is set in the setup up function, so the main loop is free
  read_p_sensor();

  base_sec += sample_interval;
  if (base_sec >= 60) {
    // If we have incremented the alarm all the way to the next min,
    // reset base sec to 0
    base_sec = 0;
    rtc.setAlarmSeconds(base_sec);
    rtc.enableAlarm(rtc.MATCH_SS);
  }
  else {
    rtc.setAlarmSeconds(base_sec);
  }

} // alarm_one_routine <END>

//----------------------------------------

void read_p_sensor(){

  // define new string and append protoID:
  String dataString = "";

  // insert RTC data timestamp here:
  dataString += String(protoID);
  dataString += ",";


  dataString += String(counter);
  dataString += ",";
  counter++; // increment counter after it has been saved to data string *** confirm that this is populating the global counter too...

  String time_stmp = gen_timestamp();
  dataString += time_stmp;
  dataString += ",";

  // get temp function:
  String tempC = get_temp(); // original on 0304
  //String tempC = loopTemp_stor;  // patch on 0304
  //dataString += tempC;
  dataString += ",";

  // read all voltages and append to the string:
  for (int analogPin = 0; analogPin < numSensors; analogPin++)
  {
    int sensor = analogRead(analogPin);
    dataString += String(sensor);

    if (analogPin < (numSensors - 1))
    {
      dataString += ",";
    }
  }
  //delay(Dspeed); // Dspeed set by user ** superceded by millis() control timer


  Serial.println(dataString); // Print to Serial Terminal

  // print to SD card, make sure SD card is on:
  init_SD();

  File dataFile = SD.open(fname, FILE_WRITE); // set up file for writing
  if (dataFile)
  { // if the file is available, write to it:
    dataFile.println(dataString);
    dataFile.close();
  }

  // shut off SD and init Radio for data send:
  init_RADIO();
  init_LORA();
  send_LORA(dataString);

  // re-initialize SD card after send:
  init_SD();


} // read_p_sensor <END>


String gen_timestamp(){
  String _time = "";
  //RTC month:
  uint8_t month_u = rtc.getMonth();
  _time += String(month_u);
  _time += ",";


  //RTC day:
  uint8_t day_u = rtc.getDay();
  _time += String(day_u);
  _time += ",";

  //RTC hours:
  uint8_t hour_u = rtc.getHours();
  _time += String(hour_u);
  _time += ",";


  //RTC minutes:
  uint8_t minute_u = rtc.getMinutes();
  _time += String(minute_u);
  _time += ",";


  //RTC seconds:
  uint8_t second_u = rtc.getSeconds();
  _time += String(second_u);
  //_time += ",";

  return(_time);

} // gen_timestamp <END>

//---------------------------

String get_temp() {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius;

  if (!ds.search(addr)) {
    ds.reset_search();
    delay(250);
    //return;
  }


  // if (OneWire::crc8(addr, 7) != addr[7]) {
  //     //Serial.println("CRC is not valid!");
  //     //return;
  // }

  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
    //  Serial.println("  Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
    //  Serial.println("  Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
     // Serial.println("  Chip = DS1822");
      type_s = 0;
      break;
    default:
      break;
      // Serial.println("Device is not a DS18x20 family device.");
      //return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end

  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  //Serial.print("  Data = ");
  //Serial.print(present, HEX);
  //Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    //Serial.print(data[i], HEX);
    //Serial.print(" ");
  }
  // Serial.print(" CRC=");
  //Serial.print(OneWire::crc8(data, 8), HEX);
  //Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;

  //char celsius_string[10] = "";
  //dtostr(celsius, 4,6, celsius_string);

  String cString = String(celsius, 2);
  return (cString) ;
} // get_temp() <END>

//--------------------------------

void init_GPS()
  {
  // 9600 baud is the default rate for the Ultimate GPS

  GPSSerial.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY); // set to only location, time, fix responses
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 hz collection interval
 // useInterrupt(true); // setting interrupt for predefined func above (?)

  }// init_GPS() <END>


//----------------------------


void print2digits(int number)
{

  if (number < 10)
  {

    Serial.print("0"); // print a 0 before if the number is < than 10

  }

  Serial.print(number);
} // print2digits() <END>

//---------------------------
void gps_to_rtc()
{


  if (! usingInterrupt)
    {
      // read data from the GPS in the 'main loop'
      char c = GPS.read();
      // if you want to debug, this is a good time to do it!
      //if (GPSECHO)
        //if (c) Serial.print(c);
    }

  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived())
    {

      if (!GPS.parse(GPS.lastNMEA()))
        {   // this also sets the newNMEAreceived() flag to false
        return;  // we can fail to parse a sentence in which case we should just wait for another
        }
      else
      {
        uint8_t gps_yr;
        uint8_t gps_month;
        uint8_t gps_day;
        uint8_t gps_hr;
        uint8_t gps_min;
        uint8_t gps_sec;

        gps_yr    = GPS.year;
        gps_month = GPS.month;
        gps_day   = GPS.day;
        gps_hr    = GPS.hour;
        gps_min   = GPS.minute;
        gps_sec   = GPS.seconds;

        rtc.setTime(gps_hr, gps_min, gps_sec);
        rtc.setDate(gps_day, gps_month, gps_yr);
       }

      // print

      // Print date...

      print2digits(rtc.getDay());

      Serial.print("/");

      print2digits(rtc.getMonth());

      Serial.print("/");

      print2digits(rtc.getYear());

      Serial.print(" ");

      // ...and time

      print2digits(rtc.getHours());

      Serial.print(":");

      print2digits(rtc.getMinutes());

      Serial.print(":");

      print2digits(rtc.getSeconds());

      Serial.println();
    }

  // if millis() or timer wraps around, we'll just reset it
  if (timer > millis())  timer = millis();

static bool second_time_round=false;

} // gps_to_rtc() <END>
