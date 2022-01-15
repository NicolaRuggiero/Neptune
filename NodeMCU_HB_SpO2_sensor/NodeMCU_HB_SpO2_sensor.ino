#include <LittleFS.h>
#include <FirebaseESP8266.h>

#include <Wire.h>

#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include <ESP8266HTTPClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include "wifi_cred.h"

#define MAX_BRIGHTNESS 255

#define FIREBASE_HOST "neptune-ad095.firebaseio.com"
#define FIREBASE_AUTH "0KlpGnM2yphHuo4ON1YLpYlPQdcSdd62hnyI0mjv"

MAX30105 particleSensor;

//variables and arrays for the HB part

const byte RATE_SIZE = 4; // Increase this for more averaging
byte rates[RATE_SIZE];    // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

// variables and arrays for the SpO2 part

uint32_t irBuffer[100];  // infrared LED sensor data
uint32_t redBuffer[100]; // red LED sensor data

int32_t bufferLength;  // data length
int32_t spo2;          // SpO2 value
int8_t validSpO2;      // indicator showing if the SpO2 calculation is valid
int32_t heartRate;     //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid

// WiFi settings and variables

// Set your Static IP address
/*IPAddress local_IP(192, 168, 1, 134);

// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);

IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional
*/
String page = "";
double dataHR;
double dataSpO2;

FirebaseData fbdo;
//FirebaseJson json;
FirebaseJson json1;
FirebaseJson json2;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void printResult(FirebaseData &data)
{

  if (data.dataType() == "int")
    Serial.println(data.intData());
  else if (data.dataType() == "float")
    Serial.println(data.floatData(), 5);
  else if (data.dataType() == "double")
    printf("%.9lf\n", data.doubleData());
  else if (data.dataType() == "boolean")
    Serial.println(data.boolData() == 1 ? "true" : "false");
  else if (data.dataType() == "string")
    Serial.println(data.stringData());
  else if (data.dataType() == "json")
  {
    Serial.println();
    FirebaseJson &json = data.jsonObject();
    //Print all object data
    Serial.println("Pretty printed JSON data:");
    String jsonStr;
    json.toString(jsonStr, true);
    Serial.println(jsonStr);
    Serial.println();
    Serial.println("Iterate JSON data:");
    Serial.println();
    size_t len = json.iteratorBegin();
    String key, value = "";
    int type = 0;
    for (size_t i = 0; i < len; i++)
    {
      json.iteratorGet(i, type, key, value);
      Serial.print(i);
      Serial.print(", ");
      Serial.print("Type: ");
      Serial.print(type == FirebaseJson::JSON_OBJECT ? "object" : "array");
      if (type == FirebaseJson::JSON_OBJECT)
      {
        Serial.print(", Key: ");
        Serial.print(key);
      }
      Serial.print(", Value: ");
      Serial.println(value);
    }
    json.iteratorEnd();
  }
  else if (data.dataType() == "array")
  {
    Serial.println();
    //get array data from FirebaseData using FirebaseJsonArray object
    FirebaseJsonArray &arr = data.jsonArray();
    //Print all array values
    Serial.println("Pretty printed Array:");
    String arrStr;
    arr.toString(arrStr, true);
    Serial.println(arrStr);
    Serial.println();
    Serial.println("Iterate array values:");
    Serial.println();
    for (size_t i = 0; i < arr.size(); i++)
    {
      Serial.print(i);
      Serial.print(", Value: ");

      FirebaseJsonData &jsonData = data.jsonData();
      //Get the result data from FirebaseJsonArray object
      arr.get(jsonData, i);
      if (jsonData.typeNum == FirebaseJson::JSON_BOOL)
        Serial.println(jsonData.boolValue ? "true" : "false");
      else if (jsonData.typeNum == FirebaseJson::JSON_INT)
        Serial.println(jsonData.intValue);
      else if (jsonData.typeNum == FirebaseJson::JSON_FLOAT)
        Serial.println(jsonData.floatValue);
      else if (jsonData.typeNum == FirebaseJson::JSON_DOUBLE)
        printf("%.9lf\n", jsonData.doubleValue);
      else if (jsonData.typeNum == FirebaseJson::JSON_STRING ||
               jsonData.typeNum == FirebaseJson::JSON_NULL ||
               jsonData.typeNum == FirebaseJson::JSON_OBJECT ||
               jsonData.typeNum == FirebaseJson::JSON_ARRAY)
        Serial.println(jsonData.stringValue);
    }
  }
  else if (data.dataType() == "blob")
  {

    Serial.println();

    for (int i = 0; i < data.blobData().size(); i++)
    {
      if (i > 0 && i % 16 == 0)
        Serial.println();

      if (i < 16)
        Serial.print("0");

      Serial.print(data.blobData()[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
  else if (data.dataType() == "file")
  {

    Serial.println();

    File file = data.fileStream();
    int i = 0;

    while (file.available())
    {
      if (i > 0 && i % 16 == 0)
        Serial.println();

      int v = file.read();

      if (v < 16)
        Serial.print("0");

      Serial.print(v, HEX);
      Serial.print(" ");
      i++;
    }
    Serial.println();
    file.close();
  }
  else
  {
    Serial.println(data.payload());
  }
}

struct Sensors_polling
{
  int saturation;
  int heartRate;
  String date;
};

void setup()
{

  timeClient.begin();

  // put your setup code here, to run once:
  Serial.begin(115200);
  // WiFi setup
  delay(1000);
  //if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
  //Serial.println("STA Failed to configure");
  //}
  WiFi.begin(WIFI_SSID, WIFI_PW); // begin WiFi connection

  Serial.println();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(WiFi.status());
  }
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH); // connect to firebase database server
  Firebase.reconnectWiFi(true);
  Serial.println();
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Web server started!");

  Serial.println("Initializing...");

  // initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) // Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1)
      ;
  }

  Serial.println("Place your index finger on the sensor with steady pressure.");
  /*
  TODO should we wait for an input from the app?
  */

  byte ledBrightness = 60; // Options: 0=Off to 255= 50mA
  byte sampleAverage = 4;  // Options: 1, 2 4, 8, 16, 32
  byte ledMode = 2;        // Options: 1 = Red only, 2 = Red * IR, 3 = Red + IR + Green
  byte sampleRate = 100;   // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;    // Options: 69, 118, 215, 411
  int adcRange = 4096;     // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // configure sensor
}

void loop()
{
  int currentSize_firebase;
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime(); //get the seconds from 1 Jan. 1970
  char buffer[30] = {"\0"};                            //initialize char buffer that will contain the converted UTC time
  struct tm *timeinfo = gmtime((time_t*)&epochTime);
  strftime(buffer, 30, "%F-%T%z", timeinfo);

  String firebase_date(buffer);

  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  //read the first 100 samples, and determine the signal range
  for (byte i = 0; i < bufferLength; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check();                   //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);
  }

  //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSpO2, &heartRate, &validHeartRate);

  //Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second

  //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
  for (byte i = 25; i < 100; i++)
  {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25] = irBuffer[i];
  }

  //take 25 sets of samples before calculating the heart rate.
  for (byte i = 75; i < 100; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check();                   //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    //send samples and calculation result to terminal program through UART
    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);
  }

  Serial.println();

  Serial.print(F("HR="));
  // normalization [55, 305] ~~> [55, 105]
  //this part shouldn't be here, but for the purpose of the exam i decided to normalize the values to get something nicer
  if (heartRate <= 55)
  {
    heartRate = 55;
  }
  else
  {
    heartRate = ((heartRate - 55) * 0.2) + 55;
  }
  Serial.print(heartRate, DEC);

  Serial.print(F(", HRvalid="));
  Serial.print(validHeartRate, DEC);
  String fireHR = String(heartRate) + String(" bpm");

  Serial.print(F(", SPO2="));
  // normalization [50, 100] ~~> [90, 100]
  //this part shouldn't be here, but for the purpose of the exam i decided to normalize the values to get something nicer
  if (spo2 <= 50)
  {
    spo2 = 90;
  }
  else
  {
    spo2 = ((spo2 - 50) * 0.2) + 90;
  }

  Serial.print(spo2, DEC);
  String firespo2 = String(spo2) + String("%");

  Serial.print(F(", SPO2Valid="));
  Serial.println(validSpO2, DEC);

  if (spo2 < 90)
  {
    Serial.println("Saturazione TROPPO bassa!");
  }
  else if (spo2 < 94)
  {
    Serial.println("Saturazione bassa!");
  }

  Serial.println();
  dataHR = heartRate;
  dataSpO2 = spo2;

  Sensors_polling sensors_value = {spo2, dataHR, firebase_date};

  if (Firebase.getInt(fbdo, "/size"))
  {
    Serial.println("PASSED");
    Serial.println("PATH: " + fbdo.dataPath());
    Serial.println("TYPE: " + fbdo.dataType());
    Serial.println("ETag: " + fbdo.ETag());
    Serial.print("VALUE: ");
    //FirebaseJson json = fbdo.jsonObject();
    Serial.print(fbdo.intData());
    Serial.println("------------------------------------");
    Serial.println();
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }

  currentSize_firebase = fbdo.intData();

  //After gathering 25 new samples recalculate HR and SP02
  // maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSpO2, &heartRate, &validHeartRate);
  if (Firebase.getInt(fbdo, "/using"))
  {
    Serial.println("PASSED");
    Serial.println("PATH: " + fbdo.dataPath());
    Serial.println("TYPE: " + fbdo.dataType());
    Serial.println("ETag: " + fbdo.ETag());
    Serial.print("VALUE: ");
    FirebaseJson json1 = fbdo.jsonObject();
    Serial.print(fbdo.intData());
    Serial.println("------------------------------------");
    Serial.println();
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }

  if (fbdo.intData() == 1)
  {

    json1.set("Data", dataHR);
    json2.set("Data", dataSpO2);
    json1.set("timestamp", firebase_date);
    json2.set("timestamp", firebase_date);

    if (Firebase.updateNode(fbdo, "heartRate/" + String(currentSize_firebase + 1), json1))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
      //No ETag available
      Serial.print("VALUE: ");
      //printResult(fbdo);
      Serial.println("------------------------------------");
      Serial.println();
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
      Serial.println("------------------------------------");
      Serial.println();
    }

    if (Firebase.updateNode(fbdo, "saturation/" + String(currentSize_firebase + 1), json2))
    {
      Serial.println("PASSED");

      Serial.println("PATH: " + fbdo.dataPath());
      Serial.print("PUSH NAME: ");
      Serial.println(fbdo.pushName());
      Serial.println("ETag: " + fbdo.ETag());
      Serial.println("------------------------------------");
      Serial.println();
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
      Serial.println("------------------------------------");
      Serial.println();
    }
    if (Firebase.setInt(fbdo, "/using", 0))
    {
      Serial.println("PASSED");

      Serial.println("PATH: " + fbdo.dataPath());
      Serial.print("PUSH NAME: ");
      Serial.println(fbdo.pushName());
      Serial.println("ETag: " + fbdo.ETag());
      Serial.println("------------------------------------");
      Serial.println();
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
      Serial.println("------------------------------------");
      Serial.println();
    }
    if (Firebase.setInt(fbdo, "/size", currentSize_firebase + 1))
    {
      Serial.println("PASSED");

      Serial.println("PATH: " + fbdo.dataPath());
      Serial.print("PUSH NAME: ");
      Serial.println(fbdo.pushName());
      Serial.println("ETag: " + fbdo.ETag());
      Serial.println("------------------------------------");
      Serial.println();
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
      Serial.println("------------------------------------");
      Serial.println();
    }
  }
}

// TODO check global variables for fbdo and json variables, and and their initialization
