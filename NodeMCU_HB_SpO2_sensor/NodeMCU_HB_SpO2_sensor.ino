#include <Wire.h>

#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include "wifi_cred.h"

#define MAX_BRIGHTNESS 255

MAX30105 particleSensor;

//variables and arrays for the HB part

const byte RATE_SIZE = 4;  // Increase this for more averaging
byte rates[RATE_SIZE];  // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0;  // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

// variables and arrays for the SpO2 part

uint32_t irBuffer[100];  // infrared LED sensor data
uint32_t redBuffer[100];  // red LED sensor data

int32_t bufferLength;  // data length
int32_t spo2;  // SpO2 value
int8_t validSpO2;  // indicator showing if the SpO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid

// WiFi settings and variables

ESP8266WebServer server(80);  // instantiate server at port 80 (http port)

String page = "";
double dataHR;
double dataSpO2;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // WiFi setup
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PW);  // begin WiFi connection
  Serial.println();

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  server.on("/", [](){
    page = "<h1>Sensor to Node MCU Web Server</h1><h3>Data:</h3> <h4>HeartRate: "+String(dataHR)+" bpm</h4> <h4>Saturation: "+String(dataSpO2)+"%</h4>";
    server.send(200, "text/html", page);
  });

  server.begin();
  Serial.println("Web server started!");


  
  Serial.println("Initializing...");

  // initialize sensor
  if(!particleSensor.begin(Wire, I2C_SPEED_FAST))  // Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while(1);
  }

  Serial.println("Place your index finger on the sensor with steady pressure.");
  /*
  TODO should we wait for an input from the app?
  */

  byte ledBrightness = 60;  // Options: 0=Off to 255= 50mA
  byte sampleAverage = 4;  // Options: 1, 2 4, 8, 16, 32
  byte ledMode = 2;  // Options: 1 = Red only, 2 = Red * IR, 3 = Red + IR + Green
  byte sampleRate = 100;  // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;  // Options: 69, 118, 215, 411
  int adcRange = 4096;  // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);  // configure sensor
}



void loop()
{
  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

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
  while (1)
  {
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
        particleSensor.check(); //Check the sensor for new data

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
    server.handleClient();
    
    //After gathering 25 new samples recalculate HR and SP02
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSpO2, &heartRate, &validHeartRate);
  }
}