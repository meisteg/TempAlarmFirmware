/*
 * Copyright (C) 2015-2019 Gregory S. Meiste  <http://gregmeiste.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Adafruit_DHT.h>
#include <Adafruit_IO_Client.h>

#define SERIAL             Serial   // USB port
//#define SERIAL           Serial1  // TX/RX pins
#define SERIAL_BAUD        115200

#define DHT_PIN            D2
#define DHT_TYPE           DHT22

#define LED_PIN            D7

#define SENSOR_CHECK_MS    2000

#define SETTINGS_ADDR      0x0000
#define SETTINGS_MAGIC_NUM 0xdeb8ab1e
#define DEFAULT_REPORT_MS  120000

#define MAX_READING_DELTA  5.0

// Adafruit IO API Key length
#define AIO_KEY_LEN        32

struct SensorSettings
{
    uint32_t magic;
    unsigned int reportMillis;
    char aioKey[AIO_KEY_LEN + 1];
};

SensorSettings sensorSettings;
double currentTempF = 0.0;
double currentHumid = 0.0;
bool haveValidReading = false;

DHT dht(DHT_PIN, DHT_TYPE);

TCPClient tcpClient;
Adafruit_IO_Client* AIOClient;

static int setReportRate(String rate)
{
    unsigned int reportMillis = atoi(rate.c_str());

    SERIAL.printlnf("New reportMillis: %u", reportMillis);

    sensorSettings.reportMillis = reportMillis;
    EEPROM.put(SETTINGS_ADDR, sensorSettings);

    return 0;
}

static int setAIOKey(String aioKey)
{
    if (aioKey.length() == AIO_KEY_LEN)
    {
        aioKey.getBytes((unsigned char *)sensorSettings.aioKey, sizeof(sensorSettings.aioKey));
        EEPROM.put(SETTINGS_ADDR, sensorSettings);

        SERIAL.printlnf("New AIO Key: %s", sensorSettings.aioKey);

        return 0;
    }

    return -1;
}

static void doMonitorIfTime(void)
{
    static unsigned long lastReadingMillis = 0;
    unsigned long now = millis();

    if ((now - lastReadingMillis) >= SENSOR_CHECK_MS)
    {
        float h = dht.getHumidity();
        float f = dht.getTempFarenheit();

        float delta_temp = f - currentTempF;

        // Check if reading failed
        if (isnan(h) || isnan(f))
        {
            SERIAL.println("Failed to read from DHT sensor!");
            digitalWrite(LED_PIN, HIGH);
        }
        // Sanity check temperature read from sensor. If delta from previous
        // reading is greater than MAX_READING_DELTA degrees, it probably is bogus.
        else if (haveValidReading && (fabsf(delta_temp) > MAX_READING_DELTA))
        {
            SERIAL.printlnf("Bad reading (%.2f) from DHT sensor!", f);
            digitalWrite(LED_PIN, HIGH);
        }
        // Reading is good, keep it
        else
        {
            currentHumid = h;
            currentTempF = f;

            SERIAL.printf("Humid: %.2f%% - Temp: %.2f*F ", currentHumid, currentTempF);
            SERIAL.println(Time.timeStr());

            haveValidReading = true;
            digitalWrite(LED_PIN, LOW);
        }

        lastReadingMillis = now;
    }
}

static void doReportIfTime(void)
{
    static unsigned long lastReportMillis = 0;
    unsigned long now = millis();
    char publishString[64];

    // Skip report if there hasn't been a valid reading
    if (!haveValidReading) return;

    if ((lastReportMillis == 0) || ((now - lastReportMillis) >= sensorSettings.reportMillis))
    {
        SERIAL.println("Reporting sensor data to server");

        snprintf(publishString, sizeof(publishString), "{\"tempF\": %.1f, \"humid\": %.1f}", currentTempF, currentHumid);
        Particle.publish("sensorData", publishString, PRIVATE);

        // Send to Adafruit IO
        Adafruit_IO_Feed tempFeed = AIOClient->getFeed("temp-alarm.temperature");
        snprintf(publishString, sizeof(publishString), "%.1f", currentTempF);
        if (!tempFeed.send(publishString))
        {
            SERIAL.println("Failed to publish temperature to Adafruit IO!");
        }

        lastReportMillis = now;
    }
}

void setup(void)
{
    SERIAL.begin(SERIAL_BAUD);

    SERIAL.println("Initializing DHT22 sensor");
    dht.begin();
    delay(2000);

    EEPROM.get(SETTINGS_ADDR, sensorSettings);
    if (SETTINGS_MAGIC_NUM == sensorSettings.magic)
    {
        SERIAL.printlnf("reportMillis = %u", sensorSettings.reportMillis);
        SERIAL.printlnf("AIO Key: %s", sensorSettings.aioKey);
    }
    else
    {
        SERIAL.println("EEPROM not programmed! Setting default values.");
        sensorSettings.magic = SETTINGS_MAGIC_NUM;
        sensorSettings.reportMillis = DEFAULT_REPORT_MS;
        EEPROM.put(SETTINGS_ADDR, sensorSettings);
    }

    Particle.variable("currentTempF", &currentTempF, DOUBLE);
    Particle.variable("currentHumid", &currentHumid, DOUBLE);

    Particle.function("reportRate", setReportRate);
    Particle.function("aioKey", setAIOKey);

    AIOClient = new Adafruit_IO_Client(tcpClient, sensorSettings.aioKey);
    AIOClient->begin();

    pinMode(LED_PIN, OUTPUT);
}

void loop(void)
{
    doMonitorIfTime();
    doReportIfTime();
}
