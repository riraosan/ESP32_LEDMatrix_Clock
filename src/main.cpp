/*
The MIT License (MIT)

Copyright (c) 2020-2021 riraosan.github.io

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#define TS_ENABLE_SSL  // Don't forget it for ThingSpeak.h!!
#include <Arduino.h>
#include <esp32-hal-log.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_I2CDevice.h>
#include <HD_0158_RG0019A.h>
#include <AutoConnect.h>
#include <ESPUI.h>
#include <ThingSpeak.h>
#include <Ticker.h>
#include <secrets.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <timezone.h>
#include <secrets.h>

#define HOSTNAME      "esp32_clock"
#define HTTP_PORT     80
#define MSG_CONNECTED "WiFi Started."

#define CLOCK_EN_S    6   //Start AM 6:00
#define CLOCK_EN_E    23  //End   PM11:00

Ticker clocker;
Ticker connectBlinker;
Ticker clockChecker;
Ticker sensorChecker;

StaticJsonDocument<384> doc;
// HD_0158_RG0019A library doesn't use manual RAM control.
// Set SE and ABB low.
#define PANEL_PIN_A3  23
#define PANEL_PIN_A2  21
#define PANEL_PIN_A1  25
#define PANEL_PIN_A0  26
#define PANEL_PIN_DG  19
#define PANEL_PIN_CLK 18
#define PANEL_PIN_WE  17
#define PANEL_PIN_DR  16
#define PANEL_PIN_ALE 22
#define PORT_SE_IN    13
#define PORT_AB_IN    27
#define PORT_LAMP     5

HD_0158_RG0019A matrix(
    2,
    PANEL_PIN_A3, PANEL_PIN_A2, PANEL_PIN_A1, PANEL_PIN_A0,
    PANEL_PIN_DG, PANEL_PIN_CLK, PANEL_PIN_WE, PANEL_PIN_DR, PANEL_PIN_ALE);

uint16_t timeLabelId;
uint16_t temperatureLabelId;
uint16_t humidityLabelId;
uint16_t pressurLabelId;

WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;  // Enable autoReconnect supported on v0.9.4
AutoConnectAux Timezone;

unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey     = SECRET_WRITE_APIKEY;
const char *certificate       = SECRET_TS_ROOT_CA;

//message ID
enum class MESSAGE {
    MSG_COMMAND_NOTHING,
    MSG_COMMAND_GET_SENSOR_DATA,
    MSG_COMMAND_PRINT_TEMPERATURE_VALUE,
    MSG_COMMAND_PRINT_PRESSURE_VALUE,
    MSG_COMMAND_PRINT_HUMIDITY_VALUE,
    MSG_COMMAND_START_CLOCK,
    MSG_COMMAND_STOP_CLOCK,
    MSG_COMMAND_BLE_INIT,
    MSG_COMMAND_BLE_DO_CONNECT,
    MSG_COMMAND_BLE_CONNECTED,
    MSG_COMMAND_BLE_DISCONNECTED,
    MSG_COMMAND_BLE_NOT_FOUND,
};

MESSAGE message = MESSAGE::MSG_COMMAND_NOTHING;

void setAllPortOutput() {
    pinMode(PORT_SE_IN, OUTPUT);
    pinMode(PORT_AB_IN, OUTPUT);
    pinMode(PORT_LAMP, OUTPUT);
}

void setAllPortLow() {
    digitalWrite(PORT_SE_IN, LOW);
    digitalWrite(PORT_AB_IN, LOW);
    digitalWrite(PORT_LAMP, LOW);
}

void rootPage(void) {
    String content =
        "<html>"
        "<head>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<script type=\"text/javascript\">"
        "setTimeout(\"location.reload()\", 1000);"
        "</script>"
        "</head>"
        "<body>"
        "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Hello, world</h2>"
        "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
        "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
        "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
                                                                                           "</body>"
                                                                                           "</html>";
    static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};
    struct tm *tm;
    time_t t;
    char dateTime[26];

    t  = time(NULL);
    tm = localtime(&t);
    sprintf(dateTime, "%04d/%02d/%02d(%s) %02d:%02d:%02d.",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            wd[tm->tm_wday],
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    content.replace("{{DateTime}}", String(dateTime));
    Server.send(200, "text/html", content);
}

void startPage(void) {
    // Retrieve the value of AutoConnectElement with arg function of WebServer class.
    // Values are accessible with the element name.
    String tz = Server.arg("timezone");

    for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
        String tzName = String(TZ[n].zone);
        if (tz.equalsIgnoreCase(tzName)) {
            configTime(TZ[n].tzoff * 3600, 0, TZ[n].ntpServer);
            log_d("Time zone: %s", tz.c_str());
            log_d("ntp server: %s", String(TZ[n].ntpServer).c_str());
            break;
        }
    }

    // The /start page just constitutes timezone,
    // it redirects to the root page without the content response.
    Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
    Server.send(302, "text/plain", "");
    Server.client().flush();
    Server.client().stop();
}

void otaPage(void) {
    String content = R"(
        <!DOCTYPE html>
        <html>
        <head>
        <meta charset="UTF-8" name="viewport" content="width=device-width, initial-scale=1">
        </head>
        <body>
        Place the root page with the sketch application.&ensp;
        __AC_LINK__
        </body>
        </html>
    )";

    content.replace("__AC_LINK__", String(AUTOCONNECT_LINK(COG_16)));
    Server.send(200, "text/html", content);
}

String makeCreateTime() {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    char buffer[128] = {0};
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d+0900",
            tm->tm_year + 1900,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec);

    log_i("[time] %s", String(buffer).c_str());

    return String(buffer);
}

void printTimeLEDMatrix(void) {
    static int flag = 0;
    flag            = ~flag;

    char tmp_str[10] = {0};
    time_t t         = time(NULL);
    struct tm *tm    = localtime(&t);

    if (flag == 0) {
        sprintf(tmp_str, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        sprintf(tmp_str, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    }

    matrix.startWrite();
    matrix.fillScreen(DOT_BLACK);
    matrix.setCursor(0, 0);
    matrix.setTextColor(DOT_GREEN);
    matrix.println(tmp_str);
    matrix.endWrite();
}

void connecting() {
    static int num = 0;

    num = ~num;

    matrix.startWrite();
    matrix.fillScreen(DOT_BLACK);
    matrix.setCursor(0, 0);
    matrix.setTextColor(DOT_GREEN);

    if (num) {
        matrix.print("init");
        matrix.setTextColor(DOT_ORANGE);
        matrix.println(".");
    } else {
        matrix.println("init");
    }
    matrix.endWrite();
}

void initMatrix(void) {
    setAllPortOutput();
    setAllPortLow();

    matrix.begin();
#ifdef DEBUG
    delay(1000);
    matrix.fillScreen(DOT_GREEN);
    delay(1000);
    matrix.fillScreen(DOT_RED);
    delay(1000);
#endif
    matrix.fillScreen(DOT_BLACK);

    matrix.setTextWrap(false);
    matrix.setTextSize(1);
    matrix.setFont(NULL);
}

bool check_clock_enable(uint8_t start_hour, uint8_t end_hour) {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    log_i("HH:MM:SS = %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);

    if (start_hour <= tm->tm_hour && tm->tm_hour < end_hour) {
        return true;
    } else {
        return false;
    }
}

void checkSensor() {
    message = MESSAGE::MSG_COMMAND_GET_SENSOR_DATA;
}

void stopClock() {
    clocker.detach();
}

void startClock() {
    clocker.attach_ms(250, printTimeLEDMatrix);
}

void check_clock() {
    if (check_clock_enable(CLOCK_EN_S, CLOCK_EN_E)) {
        message = MESSAGE::MSG_COMMAND_START_CLOCK;
    } else {
        stopClock();
        matrix.fillScreen(DOT_BLACK);
        digitalWrite(PORT_LAMP, LOW);
    }
}

bool WaitSeconds(int second) {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    if (tm->tm_sec == second) {
        return false;
    }

    return true;
}

void initClock() {
    //Get NTP Time
    configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(2000);

    while (WaitSeconds(0)) {
        delay(100);
        yield();
    }

    check_clock();
    clockChecker.attach(60, check_clock);

    while (WaitSeconds(30)) {
        delay(100);
        yield();
    }

    //sensorChecker.attach(60, checkSensor);
}

void selectHour(Control *sender, int value) {
    log_d("Select: ID: %d, Value: %d", sender->id, sender->value);
}

void selectMinuit(Control *sender, int value) {
    log_d("Select: ID: %d, Value: %d", sender->id, sender->value);
}

void initESPUI() {
    ESPUI.setVerbosity(Verbosity::Quiet);

    uint16_t hour = ESPUI.addControl(ControlType::Select, "Hour", "", ControlColor::Alizarin, Control::noParent, &selectHour);
    ESPUI.addControl(ControlType::Option, "6 am", "6", ControlColor::Alizarin, hour);
    ESPUI.addControl(ControlType::Option, "7 am", "7", ControlColor::Alizarin, hour);
    ESPUI.addControl(ControlType::Option, "8 am", "8", ControlColor::Alizarin, hour);

    uint16_t minuit = ESPUI.addControl(ControlType::Select, "Minuit", "", ControlColor::Alizarin, Control::noParent, &selectMinuit);
    ESPUI.addControl(ControlType::Option, "0", "0", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "10", "10", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "20", "20", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "30", "30", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "40", "40", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "50", "50", ControlColor::Alizarin, minuit);

    ESPUI.begin("LAMP Alarm Clock Setting");
}

void initAutoConnect(void) {
    Serial.begin(115200);
    // Enable saved past credential by autoReconnect option,
    // even once it is disconnected.
    Config.autoReconnect = true;
    Config.ota           = AC_OTA_BUILTIN;
    Portal.config(Config);

    // Load aux. page
    Timezone.load(AUX_TIMEZONE);
    // Retrieve the select element that holds the time zone code and
    // register the zone mnemonic in advance.
    AutoConnectSelect &tz = Timezone["timezone"].as<AutoConnectSelect>();
    for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
        tz.add(String(TZ[n].zone));
    }

    Portal.join({Timezone});  // Register aux. page

    // Behavior a root path of ESP8266WebServer.
    Server.on("/", rootPage);
    Server.on("/start", startPage);  // Set NTP server trigger handler
    Server.on("/ota", otaPage);

    // Establish a connection with an autoReconnect option.
    if (Portal.begin()) {
        log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
        if (MDNS.begin(HOSTNAME)) {
            MDNS.addService("http", "tcp", HTTP_PORT);
            log_i("HTTP Server ready! Open http://%s.local/ in your browser\n", HOSTNAME);
        } else
            log_e("Error setting up MDNS responder");
    }
}

void setup() {
    initMatrix();

    connectBlinker.attach_ms(500, connecting);

    initAutoConnect();
    initClock();
    //initESPUI();

    connectBlinker.detach();
}

void loop() {
    Portal.handleClient();
    switch (message) {
        case MESSAGE::MSG_COMMAND_GET_SENSOR_DATA:
            message = MESSAGE::MSG_COMMAND_NOTHING;
            break;
        case MESSAGE::MSG_COMMAND_START_CLOCK:
            digitalWrite(PORT_LAMP, HIGH);
            startClock();
            message = MESSAGE::MSG_COMMAND_NOTHING;
            break;
        case MESSAGE::MSG_COMMAND_STOP_CLOCK:
            stopClock();
            message = MESSAGE::MSG_COMMAND_PRINT_TEMPERATURE_VALUE;
            break;
        case MESSAGE::MSG_COMMAND_NOTHING:
        default:;  //nothing
    }

    yield();
}
