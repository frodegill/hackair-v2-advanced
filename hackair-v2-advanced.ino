/**
 * @file hackair_wemos_2
 * This example reads data from a sensor and sends it to the hackAIR platform
 * using the Wemos integrated WiFi . This code
 * assumes a DHT11 humidity sensor connected to pin D4.
 *
 * @author LoRAthens Air Quality team
 * @author Thanasis Georgiou (Cleanup)
 * @author Michiel van der Kraats ( added custom parameter to WiFi manager, mDNS support, Adafruit.IO and cleanup )
 *
 * This example is part of the hackAIR Arduino Library and is available
 * in the Public Domain.
 */

#include <Arduino.h>
#include <DHT.h>                  // Adafruit's DHT sensor library https://github.com/adafruit/DHT-sensor-library
#include <DHT_U.h>
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <ESP8266mDNS.h>          // ESP8266 MDNS for .local name registration
#include <FS.h>                   // Arduino filesystem layer
#include <WiFiClientSecure.h>     // Variant of WiFiClient with TLS support (from ESP82266 core wifi)
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <hackair.h>              // https://github.com/hackair-project/hackAir-Arduino
#include "Adafruit_MQTT.h"        // Adafruit.io MQTT library
#include "Adafruit_MQTT_Client.h" // Adafruit.io MQTT library
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson

// Configuration

#define HOSTNAME "hackair" // hostname to use for MDNS under the .local extension ( hackair.local )
#define AUTHORIZATION "CHANGEME" // hackAIR authorisation token
#define DEBUG "1"               // set this to 1 to stop sending data to the hackAIR platform
#define ADAFRUIT_IO_ENABLE "1"  // set this to 1 to enable Adafruit.io sending

// Adafruit MQTT

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  8883
#define AIO_USERNAME    "AIO_USERNAME"
#define AIO_KEY         "AIO_KEY"
#define AIO_PM25        "PM25FEED"
#define AIO_PM10        "PM10FEED"

// No more configuration below this line

char hackair_api_token[40]; // hackAIR API token to be collected via WiFiManager on first start

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// initialise the PM10/PM25 sensor
hackAIR sensor(SENSOR_SDS011);

// Setup the temperature and humidity sensor (pin D4)
DHT dht(D4, DHT11);

// Measurement interval
const unsigned long minutes_time_interval = 5;

// Setup ADC to measure Vcc (battery voltage)
ADC_MODE(ADC_VCC);

// Create a secure client for sending data using HTTPs
WiFiClientSecure client;

// create the objects for Adafruit IO
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Publish pm25_feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME AIO_PM25);
Adafruit_MQTT_Publish pm10_feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME AIO_PM10);

// Struct for storing sensor data
struct hackAirData data;
unsigned long previous_millis = 0;

void setup() {
  // Open serial communications
  
  Serial.begin(9600);
  Serial.println("\nHackAIR v2 sensor");
    
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(BUILTIN_LED, OUTPUT);

  // read config from filesystem

    if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(hackair_api_token, json["hackair_api_token"]);
          
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  

  if ( AUTHORIZATION == "AUTHORIZATION TOKEN" ) {
    Serial.println("ERROR: no authorization token specified");
  }
  
  // Initialize the PM sensor
  sensor.begin();
  sensor.enablePowerControl();
  sensor.turnOn();

  sensor.clearData(data);

  // Initialize temperature and humidity sensor
  dht.begin();

  // Initialize the WiFi connection
  WiFiManager wifiManager;

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_hackair_api_token("hackair_api_token", "hackAIR API token", hackair_api_token, 40);

   wifiManager.addParameter(&custom_hackair_api_token);

  // start the sensor once with the following line uncommented to reset previous WiFi settings
  // wifiManager.resetSettings();
  
  if (!wifiManager.autoConnect("ESP-wemos")) {
    Serial.println("failed to connect, please push reset button and try again");
    delay(3000);
    ESP.reset();
    delay(10000);
  }

  //read updated parameters
  strcpy(hackair_api_token, custom_hackair_api_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hackair_api_token"] = hackair_api_token;
 
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  
  // check if we have connected to the WiFi
  Serial.println("Network connected");
  Serial.println("Local IP address: ");
  Serial.print(WiFi.localIP());
  Serial.println("Default Gateway: ");
  Serial.print(WiFi.gatewayIP());
  MDNS.begin(HOSTNAME);
  
}

void loop() {
  
  float vdd = ESP.getVcc() / 500.0;
  
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.hostname("hackair");
    delay(500);
    Serial.print(".");
  }

  // Measure data
  sensor.clearData(data);
  sensor.refresh(data);

  // Average readings (60 measurments)
  double pm25 = data.pm25;
  double pm10 = data.pm10;
  int error = 0;
  Serial.println("Measuring...");
  for (int i = 0; i < 59; i++) {

    digitalWrite(BUILTIN_LED, LOW); // turn on the LED while measuring
    
    // Read from the sensor
    Serial.print(".");
    sensor.refresh(data);

    // If error is not zero something went wrong with this measurment
    // and we should not send it.
    if (data.error == 0) {
      pm25 = (pm25 + data.pm25) / 2;
      pm10 = (pm10 + data.pm10) / 2;
    } else {
      error++;
    }
    delay(1000);  // Wait one second

    digitalWrite(BUILTIN_LED, HIGH); // turn off the LED
    
  }
  data.pm25 = pm25;
  data.pm10 = pm10;
  data.error = error;


  // Measure humidity and temperature
    float humidity = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float temperature = dht.readTemperature();

  // construct the JSON to send to the hackAIR platform

  String dataJson = "{\"reading\":{\"PM2.5_AirPollutantValue\":\"";
    dataJson += pm25;
    dataJson += "\",\"PM10_AirPollutantValue\":\"";
    dataJson += pm10;
    dataJson += "\"},\"battery\":\"";
    dataJson += vdd;
    dataJson += "\",\"tamper\":\"";
    dataJson += "0";
    dataJson += "\",\"error\":\"";
    dataJson += "0";
    dataJson += "\"}";

  MQTT_connect();

  if ( DEBUG ==! "1" ) {

    // send data to network

    if ( ADAFRUIT_IO_ENABLE == "1" ) {

        
        pm25_feed.publish(pm25);
        pm10_feed.publish(pm10);
        
    }
    
    // Send the data to the hackAIR server
       
    if (client.connect("api.hackair.eu", 443)) {
      Serial.println("Connected to api.hackair.eu");
      client.print("POST /sensors/arduino/measurements HTTP/1.1\r\n");
      client.print("Host: api.hackair.eu\r\n");
      client.print("Connection: close\r\n");
      client.print("Authorization: ");
      client.println(hackair_api_token);
      client.print("Accept: application/vnd.hackair.v1+json\r\n");
      client.print("Cache-Control: no-cache\r\n");
      client.print("Content-Type: application/json\r\n");
      client.print("Content-Length: ");
      client.println(dataJson.length() + 2);
      client.println("");
      client.println(dataJson);
      Serial.println(dataJson);
      delay(500);
      
      while (client.available()) {
        
        char c = client.read();
      
      Serial.print(c);
    }
    client.stop();
  }
  delay(1000);
   
  } else {

    // DEBUG is on, output values to serial but don't send to network

    Serial.print("hackair API token: ");
    Serial.println(hackair_api_token); // write API token
    Serial.println(dataJson); // write sensor values to serial for debug
      
    }
    
  // Turn off sensor and go to sleep
  sensor.turnOff();
  unsigned long current_millis = millis();
  while (current_millis <
         (previous_millis + (minutes_time_interval * 60 * 1000))) {
    delay(10000);
    current_millis = millis();
    
    Serial.println(temperature);
    Serial.println(humidity);
    
  }
  previous_millis = current_millis;
  sensor.turnOn();
}

// define functions

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println(ret);
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }

  Serial.println("MQTT Connected!");
}