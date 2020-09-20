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

#define WIFI_SSID "Fibre-IoT"
#define WIFI_SECRET "iot@4567"

#define RTS_PIN     14   //D5
#define SDM_RX_PIN  5    //Serial Receive pin D1
#define SDM_TX_PIN  4    //Serial Transmit pin D2

String deviceId = WiFi.macAddress();
String lastError = "";

int wifiFailCount = 0;
int httpFailCount = 0;
int modBusErrorCount = 0;

const int ledPin = D4;

SoftwareSerial swSerSDM;   //Config SoftwareSerial
SDM sdm(swSerSDM, 2400, RTS_PIN, SWSERIAL_8N1, SDM_RX_PIN, SDM_TX_PIN);   //Config SDM

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

    pinMode(ledPin, OUTPUT);
    pinMode(RTS_PIN, OUTPUT);

    digitalWrite(ledPin, HIGH);

    Serial.print("Connecting to ");
    Serial.print(WIFI_SSID);
    Serial.println(" ...");

    digitalWrite(ledPin, LOW);

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
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP());

    handleOTA();

    sdm.begin();  //Initialize SDM communication

    Serial.print("RX, TX: ");
    Serial.print(SDM_RX_PIN);
    Serial.print(",");
    Serial.println(SDM_TX_PIN);

    Serial.print("RE,DE: ");
    Serial.println(RTS_PIN);

    digitalWrite(ledPin, HIGH);
}

int c = 0;

void loop() {
    c++;

    if (c % 10 == 0) {
        int t = 0;

        Serial.println("Looping Start...");

        digitalWrite(ledPin, LOW);
        delay(200);
        digitalWrite(ledPin, HIGH);

        t += readSlave(0x01);

        t += readSlave(0x65); //101 Main Distribution

        t += readSlave(0x66); //102 Kitchen Area

        t += readSlave(0x67); //103 PHEV Charging Dock

        if (t == 0) {
            digitalWrite(ledPin, LOW);
            modBusErrorCount++;
        } else {
            modBusErrorCount = 0;
        }
        digitalWrite(ledPin, HIGH);
        checkError();
    }

    delay(1000); //Wait a while before next loop

    ArduinoOTA.handle();

    if (c >= 99999) {
        c = 0;
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

    Serial.print("Slave :   0x");
    Serial.print(slaveId, HEX);
    Serial.print(",   ");
    Serial.println((int) slaveId);

    voltage = sdm.readVal(SDM230_VOLTAGE, slaveId);

    delay(50);

    current = sdm.readVal(SDM230_CURRENT, slaveId);

    if (isnan(voltage) || isnan(current)) {
        Serial.print("Reading Error :");
        if (isnan(voltage) && isnan(current))
            Serial.println("Voltage and Current is null");
        else if (isnan(voltage))
            Serial.println("Voltage null");
        else
            Serial.println("Current null");
        delay(200);

        return 0;
    } else {
        Serial.print("Voltage:   ");
        Serial.print(voltage, 2); //Display voltage
        Serial.println("V");

        Serial.print("Current:   ");
        Serial.print(current, 2);  //Display current
        Serial.println("A");
        delay(50);

        power = sdm.readVal(SDM230_POWER, slaveId);
        Serial.print("Power:     ");
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
        Serial.println("KWH");
        delay(50);

        importPower = sdm.readVal(SDM230_IMPORT_ACTIVE_ENERGY, slaveId);
        Serial.print("Import: ");
        Serial.print(importPower, 2); //Display Import KWH
        Serial.println("KWH");
        delay(50);

        exportPower = sdm.readVal(SDM230_EXPORT_ACTIVE_ENERGY, slaveId);
        Serial.print("Export: ");
        Serial.print(exportPower, 2); //Display Export KWH
        Serial.println("kWh");
        delay(50);

        powerFactor = sdm.readVal(SDM220T_POWER_FACTOR, slaveId);
        Serial.print("PF : ");
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
    if (httpFailCount >= 6 ||
        wifiFailCount >= 10 ||
        modBusErrorCount > 30) {
        Serial.print("MODBUS: ");
        Serial.print(modBusErrorCount);
        Serial.print(" ,HTTP: ");
        Serial.print(httpFailCount);
        Serial.print(" ,WiFi: ");
        Serial.print(wifiFailCount);
        Serial.println(" > Restarting ");

        sendError("Rebooting...");

        ESP.restart();
    }
}

int sendData(float v,
             float a,
             float w,
             float hz,
             float kwh,
             float im_kwh,
             float ex_kwh,
             float pf,
             byte slaveId) {
    wifiFailCount++;

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        const String dataUploadUrl = "http://localhost:3334/v1/gdb/payload/?deviceId=" + deviceId +
                                     "&slave=" + String((int) slaveId) +
                                     "&v=" + v +
                                     "&a=" + a +
                                     "&w=" + w +
                                     "&hz=" + hz +
                                     "&kwh=" + kwh +
                                     "&im_kwh=" + im_kwh +
                                     "&pf=" + pf +
                                     "&ex_kwh=" + ex_kwh +
                                     "&rssi=" + WiFi.RSSI() +
                                     "&c=" + c;

        Serial.print("[HTTP] begin...\n");

        const bool isPosted = http.begin(client, dataUploadUrl);

        if (isPosted) {
            int httpCode = http.GET();

            Serial.print("[HTTP] GET...\n");

            if (httpCode > 0) {
                Serial.printf("[HTTP] GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpFailCount = 0;
            } else {
                Serial.printf("[HTTP] GET... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            http.end();

            wifiFailCount = 0;
        } else {
            Serial.printf("[HTTP} Unable to connect\n");
        }

        return 1;
    } else {
        Serial.print("Skipping, WiFi not available: ");
        Serial.println(wifiFailCount);

        return 0;
    }
}

void sendError(String error) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        Serial.print("[HTTP] begin...\n");
        httpFailCount++;

        const String errorUploadUrl = "http://localhost:3334/v1/gdb/errors/?deviceId=" + deviceId +
                                      "&error=" + error +
                                      "&rssi=" + WiFi.RSSI() +
                                      "&wifiFailed=" + String(wifiFailCount) +
                                      "&httpFailed=" + String(httpFailCount);

        const bool isPosted = http.begin(client, errorUploadUrl);

        if (isPosted) {
            Serial.print("[HTTP] GET...\n");

            int httpCode = http.GET();

            if (httpCode > 0) {
                Serial.printf("[HTTP] GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpFailCount = 0;
            } else {
                Serial.printf("[HTTP] GET... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            http.end();
        } else {
            Serial.printf("[HTTP} Unable to connect\n");
        }
        wifiFailCount = 0;
    } else {
        wifiFailCount++;
        Serial.println("Skipping, No wifi");
    }
}

void handleOTA() {
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating " + type);
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
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}
