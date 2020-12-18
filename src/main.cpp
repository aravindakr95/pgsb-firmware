/* --------------------------------------------------
 * Power Grid Statistics Broadcaster Firmware (PGSB)
 * --------------------------------------------------
 *
 * 2020© NIB303COM Individual Project, National Institute of Business Management (affiliated with Coventry University, England)
 * @author: Aravinda Rathnayake
 */

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include <SDM.h>
#include <ArduinoJson.h>

#define WIFI_SSID "Fibre-IoT" //todo: think way to configure wifi details on demand (wifi manager)
#define WIFI_SECRET "iot@4567"

#define RTS_PIN     14 //D5
#define SDM_RX_PIN  5  //Serial Receive pin D1
#define SDM_TX_PIN  4  //Serial Transmit pin D2

#define LED_LOW LOW
#define LED_HIGH HIGH

const String deviceId = WiFi.macAddress();

const int maxErrorCount = 5;
const int requestInterval = 360; //approx. 6 (240 requests per day)

const int ledPinFault = D4;
const int ledPinTransmit = D6;

int wifiErrorCount = 0;
int httpErrorCount = 0;
int modBusErrorCount = 0;

SoftwareSerial swSerSDM;   //Config SoftwareSerial
SDM sdm(swSerSDM, 2400, RTS_PIN, SWSERIAL_8N1, SDM_RX_PIN, SDM_TX_PIN); //Config SDM

//function prototypes
int readSlave(byte);

int sendCustomPayload(float,
                      float,
                      float,
                      float,
                      float,
                      float,
                      float,
                      float,
                      byte);

void checkError();

void sendError(String);

void handleOTA();

void setup() {
    Serial.begin(115200); //Initialize serial
    delay(3000);

    WiFi.mode(WIFI_STA);

    String HN = deviceId;
    HN.replace(":", "");
    HN = "EM-" + HN.substring(8);

    Serial.print("WiFi Hostname: ");
    Serial.println(HN);

    Serial.print("Device ID: ");
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

    Serial.print(F("Connecting WiFi SSID: "));
    Serial.println(WIFI_SSID);

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
    Serial.println("WiFi connection established!");
    Serial.print(F("IPV4 Address: "));
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
    if (currentRound % requestInterval == 0) {
        int successReadsCount = 0;

        Serial.println("Looping Start...");

        if (WiFi.status() == WL_CONNECTED) {
            digitalWrite(ledPinTransmit, LED_HIGH);
            delay(200);
        }

        digitalWrite(ledPinFault, LED_LOW);
        digitalWrite(ledPinTransmit, LED_LOW);

        // Values are in Hexadecimal
        successReadsCount += readSlave(0x65); // Ground Floor Main

        successReadsCount += readSlave(0x66); // Ground Floor Kitchen

        successReadsCount += readSlave(0x67); // Ground Floor PHEV

        successReadsCount += readSlave(0xC9); // 1st Floor Main
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

    delay(1000); //Wait second before next loop

    ArduinoOTA.handle();

    if (currentRound >= 99999) {
        currentRound = 0;
    }

    currentRound++;
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

        int requestStatus = sendCustomPayload(voltage,
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
    if (httpErrorCount >= maxErrorCount ||
        wifiErrorCount >= maxErrorCount ||
        modBusErrorCount > maxErrorCount) {
        Serial.print("Modbus Errors: ");
        Serial.print(modBusErrorCount);
        Serial.print(" ,HTTP Errors: ");
        Serial.print(httpErrorCount);
        Serial.print(" ,WiFi Errors: ");
        Serial.print(wifiErrorCount);
        Serial.println("Restarting system...");

        sendError("MAX_ERROR_COUNT_REACHED");

        ESP.restart();
    }

    if (wifiErrorCount > 1) {
        digitalWrite(ledPinFault, LED_HIGH);
    }
}

int sendCustomPayload(float voltage,
                      float current,
                      float power,
                      float frequency,
                      float totalPower,
                      float importPower,
                      float exportPower,
                      float powerFactor,
                      byte slaveId) {
    int state = 0;

    wifiErrorCount++;

    if (WiFi.status() == WL_CONNECTED) {
        String jsonPayload;

        WiFiClient client;
        HTTPClient http;

        const String dataUploadUrl = "http://sete-home.brilliant-apps.club/v1/sete/pgsb/payloads?deviceId=" + deviceId + "&slaveId=" + String((int) slaveId);
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hbGluZGE1NTVAZ21haWwuY29tIiwiYWNjb3VudE51bWJlciI6NDMwMzE4MDkwNCwiaWF0IjoxNjA4MjczMTI1fQ.aURQ8LkGyWV-CBiQ9YQIk1pgNXA43hs0XyP9Vx20kNI";

        StaticJsonDocument<500> PayloadDoc;

        PayloadDoc["currentRound"] = currentRound;
        PayloadDoc["current"] = current;
        PayloadDoc["voltage"] = voltage;
        PayloadDoc["power"] = power;
        PayloadDoc["frequency"] = frequency;
        PayloadDoc["totalPower"] = totalPower;
        PayloadDoc["importPower"] = importPower;
        PayloadDoc["exportPower"] = exportPower;
        PayloadDoc["powerFactor"] = powerFactor;
        PayloadDoc["rssi"] = WiFi.RSSI();

        serializeJson(PayloadDoc, jsonPayload);

        Serial.print("[HTTP](1) begin...\n");

        const bool isPosted = http.begin(client, dataUploadUrl);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + authToken);

        if (isPosted) {
            int httpCode = http.POST(jsonPayload);

            if (httpCode > 0) {
                Serial.printf("[HTTP](1) POST... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);

                    state = 1;
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](1) POST... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            Serial.print("[HTTP](1) end...\n");
            http.end();

            wifiErrorCount = 0;
        } else {
            Serial.printf("[HTTP](1) Unable to connect\n");
        }
    } else {
        Serial.print("Skipping, WiFi not available: ");
        Serial.println(wifiErrorCount);
    }

    return state;
}

void sendError(String error) {
    if (WiFi.status() == WL_CONNECTED) {
        String jsonError;

        WiFiClient client;
        HTTPClient http;

        const String errorUploadUrl = "http://sete-home.brilliant-apps.club/v1/sete/pgsb/errors?deviceId=" + deviceId;
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hbGluZGE1NTVAZ21haWwuY29tIiwiYWNjb3VudE51bWJlciI6NDMwMzE4MDkwNCwiaWF0IjoxNjA4MjczMTI1fQ.aURQ8LkGyWV-CBiQ9YQIk1pgNXA43hs0XyP9Vx20kNI";

        StaticJsonDocument<500> ErrorDoc;

        ErrorDoc["error"] = error;
        ErrorDoc["rssi"] = WiFi.RSSI();
        ErrorDoc["wifiFailCount"] = String(wifiErrorCount);
        ErrorDoc["httpFailCount"] = String(httpErrorCount);

        serializeJson(ErrorDoc, jsonError);

        Serial.print("[HTTP](2) begin...\n");

        httpErrorCount++;

        const bool isPosted = http.begin(client, errorUploadUrl);

        if (isPosted) {
            int httpCode = http.POST(jsonError);

            if (httpCode > 0) {
                Serial.printf("[HTTP](2) POST... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](2) POST... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            http.end();
            Serial.print("[HTTP](2) end...\n");
        } else {
            Serial.printf("[HTTP](2) Unable to connect\n");
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

    Serial.println("OTA Ready");
    Serial.print(F("IPV4 Address: "));
    Serial.println(WiFi.localIP());
}