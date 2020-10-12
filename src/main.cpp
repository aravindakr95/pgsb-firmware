/* ----------------------------
 * Power Grid Statistics Broadcaster (PGSB)
 * ----------------------------
 *
 * 2020 Individual Project, National Institute of Business Management
 * @author: Aravinda Rathnayake
 */

#include <Arduino.h>

#include <SDM.h>  //Import SDM library
#include <SoftwareSerial.h> //Import SoftwareSerial library
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define WIFI_SSID "Fibre-IoT" //todo: think way to configure wifi details on demand (wifi manager)
#define WIFI_SECRET "iot@4567"

#define RTS_PIN     14 //D5
#define SDM_RX_PIN  5  //Serial Receive pin D1
#define SDM_TX_PIN  4  //Serial Transmit pin D2

#define LED_LOW LOW
#define LED_HIGH HIGH

const String deviceId = WiFi.macAddress();

const int ledPinFault = D4;
const int ledPinTransmit = D6;

int wifiErrorCount = 0;
int httpErrorCount = 0;
int modBusErrorCount = 0;

SoftwareSerial swSerSDM;   //Config SoftwareSerial
SDM sdm(swSerSDM, 2400, RTS_PIN, SWSERIAL_8N1, SDM_RX_PIN, SDM_TX_PIN); //Config SDM

//function prototypes
int readSlave(byte);

void checkError();

void handleOTA();

int sendData(float,
             float,
             float,
             float,
             float,
             float,
             float,
             float,
             byte);

void sendError(String);

void setup() {
    Serial.begin(115200); //Initialize serial
    delay(3000);

    Serial.print("---- ");

    WiFi.mode(WIFI_STA);

    String HN = deviceId;
    HN.replace(":", "");
    HN = "EM-" + HN.substring(8);

    Serial.println(HN);
    Serial.println(deviceId);

    WiFi.hostname(HN);

    WiFi.begin(WIFI_SSID, WIFI_SECRET);
    Serial.begin(115200);

    Serial.println("");

    pinMode(ledPinFault, OUTPUT);
    pinMode(ledPinTransmit, OUTPUT);
    pinMode(RTS_PIN, OUTPUT);

    digitalWrite(ledPinFault, LED_HIGH);
    digitalWrite(ledPinTransmit, LED_HIGH);

    delay(2000);

    digitalWrite(ledPinFault, LED_LOW);
    digitalWrite(ledPinTransmit, LED_LOW);

    Serial.print("Connecting to ");
    Serial.print(WIFI_SSID);
    Serial.println(" ...");

    digitalWrite(ledPinFault, LED_HIGH);

    int i = 0;

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);

        Serial.print(++i);
        Serial.print(' ');
        if (i >= 100) {
            ESP.restart();
        }
    }

    Serial.println('\n');
    Serial.println("Connection established!");
    Serial.print("IPV4 address:\t");
    Serial.println(WiFi.localIP());

    handleOTA();

    sdm.begin(); //Initialize SDM communication

    Serial.print("RX, TX: ");
    Serial.print(SDM_RX_PIN);
    Serial.print(",");
    Serial.println(SDM_TX_PIN);

    Serial.print("RE,DE: ");
    Serial.println(RTS_PIN);

    digitalWrite(ledPinFault, LED_LOW);
}

int currentRound = 0;

void loop() {
    currentRound++;

    // start looping in every 10 number of current rounds
    if (currentRound % 10 == 0) {
        int successReadsCount = 0;

        Serial.println("Looping Start...");

        if (WiFi.status() == WL_CONNECTED) {
            digitalWrite(ledPinTransmit, LED_HIGH);
            delay(200);
        }

        digitalWrite(ledPinFault, LED_LOW);
        digitalWrite(ledPinTransmit, LED_LOW);

        successReadsCount += readSlave(0x01);

        successReadsCount += readSlave(0xC9); // 1st Floor Distribution (101 in Hex), 2nd Floor Distribution (201 in Hex)

        if (successReadsCount == 0) {
            digitalWrite(ledPinFault, LED_HIGH);
            delay(200);
            digitalWrite(ledPinFault, LED_LOW);
            delay(200);
            digitalWrite(ledPinFault, LED_HIGH);
            delay(200);
            digitalWrite(ledPinFault, LED_LOW);
            delay(200);

            modBusErrorCount++;
        } else {
            modBusErrorCount = 0;
        }
        digitalWrite(ledPinTransmit, LED_LOW);
        checkError();

        Serial.println("Looping End...");
    }

    delay(1000); //Wait a while before next loop

    ArduinoOTA.handle();

    if (currentRound >= 99999) {
        currentRound = 0;
    }
}

int readSlave(byte slaveId) {
    float voltage,
            current,
            power,
            frequency,
            totalPower,
            importPower,
            exportPower,
            powerFactor;

    Serial.print("Slave: 0x");
    Serial.print(slaveId, HEX);
    Serial.print(", ");
    Serial.println((int) slaveId);

    voltage = sdm.readVal(SDM230_VOLTAGE, slaveId);

    delay(50);

    current = sdm.readVal(SDM230_CURRENT, slaveId);

    if (isnan(voltage) || isnan(current)) {
        Serial.print("Reading Error: ");
        if (isnan(voltage) && isnan(current))
            Serial.println("Voltage and Current is null");
        else if (isnan(voltage))
            Serial.println("Voltage null");
        else
            Serial.println("Current null");
        delay(200);

        return 0;
    } else {
        Serial.print("Voltage: ");
        Serial.print(voltage, 2); //Display voltage
        Serial.println("V");

        Serial.print("Current: ");
        Serial.print(current, 2);  //Display current (amperes)
        Serial.println("A");
        delay(50);

        power = sdm.readVal(SDM230_POWER, slaveId);
        Serial.print("Power: ");
        Serial.print(power, 2); //Display power
        Serial.println("W");
        delay(50);

        frequency = sdm.readVal(SDM230_FREQUENCY, slaveId);
        Serial.print("Frequency: ");
        Serial.print(frequency, 2); //Display frequency
        Serial.println("Hz");
        delay(50);

        totalPower = sdm.readVal(SDM230_TOTAL_ACTIVE_ENERGY, slaveId);
        Serial.print("Total Power: ");
        Serial.print(totalPower, 2); //Display Total Power
        Serial.println("kWh");
        delay(50);

        importPower = sdm.readVal(SDM230_IMPORT_ACTIVE_ENERGY, slaveId);
        Serial.print("Import: ");
        Serial.print(importPower, 2); //Display Import KWH
        Serial.println("kWh");
        delay(50);

        exportPower = sdm.readVal(SDM230_EXPORT_ACTIVE_ENERGY, slaveId);
        Serial.print("Export: ");
        Serial.print(exportPower, 2); //Display Export KWH
        Serial.println("kWh");
        delay(50);

        powerFactor = sdm.readVal(SDM220T_POWER_FACTOR, slaveId);
        Serial.print("PF: ");
        Serial.print(powerFactor, 2); //Display Power Factor
        Serial.println("");
        delay(50);

        int requestStatus = sendData(voltage,
                                     current,
                                     power,
                                     frequency,
                                     totalPower,
                                     importPower,
                                     exportPower,
                                     powerFactor,
                                     slaveId);
        delay(200);

        return requestStatus;
    }
}

void checkError() {
    if (httpErrorCount >= 6 ||
        wifiErrorCount >= 10 ||
        modBusErrorCount > 30) {
        Serial.print("MODBUS: ");
        Serial.print(modBusErrorCount);
        Serial.print(" ,HTTP: ");
        Serial.print(httpErrorCount);
        Serial.print(" ,WiFi: ");
        Serial.print(wifiErrorCount);
        Serial.println(" > Restarting ");

        sendError("Reboot");

        ESP.restart();
    }

    if (wifiErrorCount > 1) {
        digitalWrite(ledPinFault, LED_HIGH);
    }
}

int sendData(float voltage,
             float current,
             float power,
             float frequency,
             float totalPower,
             float importPower,
             float exportPower,
             float powerFactor,
             byte slaveId) {
    wifiErrorCount++;

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        const String dataUploadUrl = "http://192.168.1.8:4000/v1/sete/pgsb/payloads";
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwic3VwcGxpZXIiOiJDRUIiLCJhY2NvdW50TnVtYmVyIjo0MzAzMTgwOTMxLCJpYXQiOjE2MDI1MDYzNzN9.u0bcQN2bpPWKBxrBxUrtV4l3vQcBqjfRD8Wi6ObiDow";

        StaticJsonBuffer<600> JSONBuffer;
        JsonObject &JSONEncoder = JSONBuffer.createObject();

        JSONEncoder["deviceId"] = deviceId;
        JSONEncoder["slaveId"] = String((int) slaveId);
        JSONEncoder["currentRound"] = currentRound;
        JSONEncoder["current"] = current;
        JSONEncoder["voltage"] = voltage;
        JSONEncoder["power"] = power;
        JSONEncoder["frequency"] = frequency;
        JSONEncoder["totalPower"] = totalPower;
        JSONEncoder["importPower"] = importPower;
        JSONEncoder["exportPower"] = exportPower;
        JSONEncoder["powerFactor"] = powerFactor;
        JSONEncoder["rssi"] = WiFi.RSSI();

        char JSONMessageBuffer[600];
        JSONEncoder.prettyPrintTo(JSONMessageBuffer, sizeof(JSONMessageBuffer));
        Serial.println(JSONMessageBuffer);

        Serial.print("[HTTP] begin...\n");

        const bool isPosted = http.begin(client, dataUploadUrl);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + authToken);

        if (isPosted) {
            int httpCode = http.POST(JSONMessageBuffer);

            Serial.println(httpCode);

            Serial.print("[HTTP] GET...\n");

            if (httpCode > 0) {
                Serial.printf("[HTTP] GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP] GET... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            Serial.print("[HTTP] end...\n");
            http.end();

            wifiErrorCount = 0;
        } else {
            Serial.printf("[HTTP] Unable to connect\n");
        }

        return 1;
    } else {
        Serial.print("Skipping, WiFi not available: ");
        Serial.println(wifiErrorCount);

        return 0;
    }
}

void sendError(String error) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        const String errorUploadUrl = "http://192.168.1.8:4000/v1/sete/pgsb/errors";
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwic3VwcGxpZXIiOiJDRUIiLCJhY2NvdW50TnVtYmVyIjo0MzAzMTgwOTMxLCJpYXQiOjE2MDI1MDYzNzN9.u0bcQN2bpPWKBxrBxUrtV4l3vQcBqjfRD8Wi6ObiDow";

        StaticJsonBuffer<600> JSONBuffer;
        JsonObject &JSONEncoder = JSONBuffer.createObject();

        JSONEncoder["deviceId"] = deviceId;
        JSONEncoder["error"] = error;
        JSONEncoder["rssi"] = WiFi.RSSI();
        JSONEncoder["wifiFailCount"] = String(wifiErrorCount);
        JSONEncoder["httpFailCount"] = String(httpErrorCount);

        char JSONMessageBuffer[600];
        JSONEncoder.prettyPrintTo(JSONMessageBuffer, sizeof(JSONMessageBuffer));
        Serial.println(JSONMessageBuffer);

        Serial.print("[HTTP] begin...\n");

        httpErrorCount++;

        const bool isPosted = http.begin(client, errorUploadUrl);

        if (isPosted) {
            Serial.print("[HTTP] GET...\n");

            int httpCode = http.POST(JSONMessageBuffer);

            Serial.println(httpCode);

            if (httpCode > 0) {
                Serial.printf("[HTTP] GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP] GET... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            http.end();
            Serial.print("[HTTP] end...\n");
        } else {
            Serial.printf("[HTTP] Unable to connect\n");
        }

        wifiErrorCount = 0;
    } else {
        wifiErrorCount++;
        Serial.println("Skipping, No WiFi network");
    }
}

void handleOTA() {
    ArduinoOTA.setPasswordHash("8048dff8fe79031bfc7a0e84f539620c");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating: " + type);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });

    ArduinoOTA.begin();

    Serial.println("Ready");
    Serial.print("IPV4 address: ");
    Serial.println(WiFi.localIP());
}