/* 
The MIT License (MIT)

Copyright (c) 2020 riraotech.com

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

#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <Ticker.h>
#include <ESP32_SPIFFS_ShinonomeFNT.h>
#include <ESP32_SPIFFS_UTF8toSJIS.h>
#include <esp32-hal-log.h>

#define HOSTNAME "esp32_clock"
#define MONITOR_SPEED 115200
#define AP_NAME "ESP32-G-AP"
#define MSG_CONNECTED "        WiFi Started."

//ポート設定
#define PORT_SE_IN 13
#define PORT_AB_IN 27
#define PORT_A3_IN 23
#define PORT_A2_IN 21
#define PORT_A1_IN 25
#define PORT_A0_IN 26
#define PORT_DG_IN 19
#define PORT_CLK_IN 18
#define PORT_WE_IN 17
#define PORT_DR_IN 16
#define PORT_ALE_IN 22

#define PANEL_NUM 2 //パネル枚数
#define R 1         //赤色
#define O 2         //橙色
#define G 3         //緑色

#define CLOCK_EN_S 6  //Start AM6:00 (set 24hour)
#define CLOCK_EN_E 22 //End   PM9:00 (set 24hour)

const char *UTF8SJIS_file = "/Utf8Sjis.tbl";
const char *Shino_Half_Font_file = "/shnm8x16.bdf"; //半角フォントファイル名
const char *dummy = "";

ESP32_SPIFFS_ShinonomeFNT SFR;
DNSServer dns;
AsyncWebServer server(80);
AsyncWiFiManager wifiManager(&server, &dns);
Ticker clocker;
Ticker blinker;
Ticker checker;

//void log_v(format, ...); // verbose 詳細
//void log_d(format, ...); // debug
//void log_i(format, ...); // info
//void log_w(format, ...); // warning
//void log_e(format, ...); // error

//LEDマトリクスの書き込みアドレスを設定
void setRAMAdder(uint8_t lineNumber)
{
  uint8_t A[4] = {0};
  uint8_t adder = 0;

  adder = lineNumber;

  for (int i = 0; i < 4; i++)
  {
    A[i] = adder % 2;
    adder /= 2;
  }

  digitalWrite(PORT_A0_IN, A[0]);
  digitalWrite(PORT_A1_IN, A[1]);
  digitalWrite(PORT_A2_IN, A[2]);
  digitalWrite(PORT_A3_IN, A[3]);
}

////////////////////////////////////////////////////////////////////////////////////
//データをLEDマトリクスへ1行だけ書き込む
//
//iram_addr:データを書き込むアドレス（0~15）
//ifont_data:フォント表示データ(32*PANEL_NUM bit)
//color_data:フォント表示色配列（32*PANEL_NUM bit）Red:1 Orange:2 Green:3
////////////////////////////////////////////////////////////////////////////////////
void send_line_data(uint8_t iram_adder, uint8_t ifont_data[], uint8_t color_data[])
{
  uint8_t font[8] = {0};
  uint8_t tmp_data = 0;
  int k = 0;
  for (int j = 0; j < 4 * PANEL_NUM; j++)
  {
    //ビットデータに変換
    tmp_data = ifont_data[j];
    for (int i = 0; i < 8; i++)
    {
      font[i] = tmp_data % 2;
      tmp_data /= 2;
    }

    for (int i = 7; i >= 0; i--)
    {
      digitalWrite(PORT_DG_IN, LOW);
      digitalWrite(PORT_DR_IN, LOW);
      digitalWrite(PORT_CLK_IN, LOW);

      if (font[i] == 1)
      {
        if (color_data[k] == R)
        {
          digitalWrite(PORT_DR_IN, HIGH);
        }

        if (color_data[k] == G)
        {
          digitalWrite(PORT_DG_IN, HIGH);
        }

        if (color_data[k] == O)
        {
          digitalWrite(PORT_DR_IN, HIGH);
          digitalWrite(PORT_DG_IN, HIGH);
        }
      }
      else
      {
        digitalWrite(PORT_DR_IN, LOW);
        digitalWrite(PORT_DG_IN, LOW);
      }

      delayMicroseconds(1);
      digitalWrite(PORT_CLK_IN, HIGH);
      delayMicroseconds(1);

      k++;
    }
  }
  //アドレスをポートに入力
  setRAMAdder(iram_adder);
  //ALE　Highでアドレスセット
  digitalWrite(PORT_ALE_IN, HIGH);
  //WE Highでデータを書き込み
  digitalWrite(PORT_WE_IN, HIGH);
  //WE Lowをセット
  digitalWrite(PORT_WE_IN, LOW);
  //ALE Lowをセット
  digitalWrite(PORT_ALE_IN, LOW);
}

///////////////////////////////////////////////////////////////
//配列をnビット左へシフトする関数
//
//dist:格納先の配列
//src:入力元の配列
//len:配列の要素数
//n:一度に左シフトするビット数
///////////////////////////////////////////////////////////////
void shift_bit_left(uint8_t dist[], uint8_t src[], int len, int n)
{
  uint8_t mask = 0xFF << (8 - n);
  for (int i = 0; i < len; i++)
  {
    if (i < len - 1)
    {
      dist[i] = (src[i] << n) | ((src[i + 1] & mask) >> (8 - n));
    }
    else
    {
      dist[i] = src[i] << n;
    }
  }
}

void shift_color_left(uint8_t dist[], uint8_t src[], int len)
{
  for (int i = 0; i < len * 8; i++)
  {
    if (i < len * 8 - 1)
    {
      dist[i] = src[i + 1];
    }
    else
    {
      dist[i] = 0;
    }
  }
}

////////////////////////////////////////////////////////////////////
//フォントをスクロールしながら表示するメソッド
//
//sj_length:半角文字数
//font_data:フォントデータ（東雲フォント）
//color_data:フォントカラーデータ（半角毎に設定する）
//intervals:スクロール間隔(ms)
////////////////////////////////////////////////////////////////////
void scrollLEDMatrix(int16_t sj_length, uint8_t font_data[][16], uint8_t color_data[], uint16_t intervals)
{
  uint8_t src_line_data[sj_length] = {0};
  uint8_t dist_line_data[sj_length] = {0};
  uint8_t tmp_color_data[sj_length * 8] = {0};
  uint8_t tmp_font_data[sj_length][16] = {0};
  uint8_t ram = LOW;

  int n = 0;
  for (int i = 0; i < sj_length; i++)
  {

    //8ビット毎の色情報を1ビット毎に変換する
    for (int j = 0; j < 8; j++)
    {
      tmp_color_data[n++] = color_data[i];
    }

    //フォントデータを作業バッファにコピー
    for (int j = 0; j < 16; j++)
    {
      tmp_font_data[i][j] = font_data[i][j];
    }
  }

  for (int k = 0; k < sj_length * 8 + 2; k++)
  {
    ram = ~ram;
    digitalWrite(PORT_AB_IN, ram); //RAM-A/RAM-Bに書き込み
    for (int i = 0; i < 16; i++)
    {
      for (int j = 0; j < sj_length; j++)
      {
        //フォントデータをビットシフト元バッファにコピー
        src_line_data[j] = tmp_font_data[j][i];
      }

      send_line_data(i, src_line_data, tmp_color_data);
      shift_bit_left(dist_line_data, src_line_data, sj_length, 1);

      //font_dataにシフトしたあとのデータを書き込む
      for (int j = 0; j < sj_length; j++)
      {
        tmp_font_data[j][i] = dist_line_data[j];
      }
    }
    shift_color_left(tmp_color_data, tmp_color_data, sj_length);
    delay(intervals);
  }
}

////////////////////////////////////////////////////////////////////
//フォントを静的に表示するメソッド
//
//sj_length:半角文字数
//font_data:フォントデータ（東雲フォント）
//color_data:フォントカラーデータ（半角毎に設定する）
////////////////////////////////////////////////////////////////////
void printLEDMatrix(int16_t sj_length, uint8_t font_data[][16], uint8_t color_data[])
{
  uint8_t src_line_data[sj_length] = {0};
  uint8_t tmp_color_data[sj_length * 8] = {0};
  uint8_t tmp_font_data[sj_length][16] = {0};
  uint8_t ram = LOW;

  int n = 0;
  for (int i = 0; i < sj_length; i++)
  {
    //8ビット毎の色情報を1ビット毎に変換する
    for (int j = 0; j < 8; j++)
    {
      tmp_color_data[n++] = color_data[i];
    }

    //フォントデータを作業バッファにコピー
    for (int j = 0; j < 16; j++)
    {
      tmp_font_data[i][j] = font_data[i][j];
    }
  }

  for (int k = 0; k < sj_length * 8 + 2; k++)
  {
    digitalWrite(PORT_AB_IN, ram); //RAM-A/RAM-Bに書き込み
    for (int i = 0; i < 16; i++)
    {
      for (int j = 0; j < sj_length; j++)
      {
        //フォントデータをビットシフト元バッファにコピー
        src_line_data[j] = tmp_font_data[j][i];
      }

      send_line_data(i, src_line_data, tmp_color_data);
    }
    ram = ~ram;
  }
}

void setAllPortOutput()
{
  pinMode(PORT_SE_IN, OUTPUT);
  pinMode(PORT_AB_IN, OUTPUT);
  pinMode(PORT_A3_IN, OUTPUT);
  pinMode(PORT_A2_IN, OUTPUT);
  pinMode(PORT_A1_IN, OUTPUT);
  pinMode(PORT_A0_IN, OUTPUT);
  pinMode(PORT_DG_IN, OUTPUT);
  pinMode(PORT_CLK_IN, OUTPUT);
  pinMode(PORT_WE_IN, OUTPUT);
  pinMode(PORT_DR_IN, OUTPUT);
  pinMode(PORT_ALE_IN, OUTPUT);
}

void setAllPortLow()
{
  //digitalWrite(PORT_SE_IN, LOW);
  digitalWrite(PORT_AB_IN, LOW);
  digitalWrite(PORT_A3_IN, LOW);
  digitalWrite(PORT_A2_IN, LOW);
  digitalWrite(PORT_A1_IN, LOW);
  digitalWrite(PORT_A0_IN, LOW);
  digitalWrite(PORT_DG_IN, LOW);
  digitalWrite(PORT_CLK_IN, LOW);
  digitalWrite(PORT_WE_IN, LOW);
  digitalWrite(PORT_DR_IN, LOW);
  digitalWrite(PORT_ALE_IN, LOW);
}

void setAllPortHigh()
{
  digitalWrite(PORT_SE_IN, HIGH);
  digitalWrite(PORT_AB_IN, HIGH);
  digitalWrite(PORT_A3_IN, HIGH);
  digitalWrite(PORT_A2_IN, HIGH);
  digitalWrite(PORT_A1_IN, HIGH);
  digitalWrite(PORT_A0_IN, HIGH);
  digitalWrite(PORT_DG_IN, HIGH);
  digitalWrite(PORT_CLK_IN, HIGH);
  digitalWrite(PORT_WE_IN, HIGH);
  digitalWrite(PORT_DR_IN, HIGH);
  digitalWrite(PORT_ALE_IN, HIGH);
}

void PrintTime(String &str, int flag)
{
  char tmp_str[10] = {0};
  struct tm *tm;

  time_t t = time(NULL);
  tm = localtime(&t);

  if (flag == 0)
  {
    sprintf(tmp_str, "  %02d:%02d ", tm->tm_hour, tm->tm_min);
  }
  else
  {
    sprintf(tmp_str, "  %02d %02d ", tm->tm_hour, tm->tm_min);
  }

  str = tmp_str;
}

void printTimeLEDMatrix()
{
  //フォントデータバッファ
  uint8_t time_font_buf[8][16] = {0};
  String str;

  static int flag = 0;

  flag = ~flag;
  PrintTime(str, flag);

  //フォント色データ　str（半角文字毎に設定する）
  uint8_t time_font_color[8] = {G, G, G, G, G, G, G, G};
  uint16_t sj_length = SFR.StrDirect_ShinoFNT_readALL(str, time_font_buf);
  printLEDMatrix(sj_length, time_font_buf, time_font_color);
}

void blink()
{
  printTimeLEDMatrix();
}

void connecting()
{
  uint16_t sj_length = 0;
  uint8_t _font_buf[8][16] = {0};
  uint8_t _font_color[8] = {O, O, O, O, O, O, O, O};

  static int num = 0;

  num = ~num;

  if (num)
  {
    sj_length = SFR.StrDirect_ShinoFNT_readALL("        ", _font_buf);
    printLEDMatrix(sj_length, _font_buf, _font_color);
  }
  else
  {
    sj_length = SFR.StrDirect_ShinoFNT_readALL("       .", _font_buf);
    printLEDMatrix(sj_length, _font_buf, _font_color);
  }
}

void initWiFi()
{
  wifiManager.setDebugOutput(true);

  if (!wifiManager.autoConnect(AP_NAME))
  {
    ESP.restart();
  }

  blinker.detach();

  Serial.println("WiFi Started");

  uint16_t sj_length = 0;
  //フォントデータバッファ
  uint8_t font_buf[32][16] = {0};
  //フォント色データ（半角文字毎に設定する）
  uint8_t font_color1[32] = {G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G};

  sj_length = SFR.StrDirect_ShinoFNT_readALL(MSG_CONNECTED, font_buf);
  scrollLEDMatrix(sj_length, font_buf, font_color1, 30);

  sj_length = SFR.StrDirect_ShinoFNT_readALL("        " + WiFi.localIP().toString(), font_buf);
  scrollLEDMatrix(sj_length, font_buf, font_color1, 30);
}

void print_blank()
{
  uint8_t _font_buf[8][16] = {0};
  uint8_t _font_color[8] = {G, G, G, G, G, G, G, G};
  printLEDMatrix(8, _font_buf, _font_color);
}

void initLCDMatrix()
{
  setAllPortOutput();

  //手動で表示バッファを切り替えるモードに設定(HIGH:ON, LOW:OFF)
  digitalWrite(PORT_SE_IN, HIGH);
  print_blank();
  print_blank();

  blinker.attach_ms(500, connecting);
}

void initSerial()
{
  Serial.begin(MONITOR_SPEED);
}

void initOta()
{
  ArduinoOTA.onStart([]() {
    String type;

    clocker.detach();
    checker.detach();

    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";

    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.setHostname(HOSTNAME);

  Serial.print("Hostname: ");
  Serial.println(ArduinoOTA.getHostname() + ".local");

  ArduinoOTA.begin();
  Serial.println("OTA Started");
}

/**
 * @brief 
 *
 *
 * @param 
 * @return
 *      - true
 *      - false
 */
bool check_clock_enable(uint8_t start_hour, uint8_t end_hour)
{
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);

  log_i("HH:MM:SS = %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);

  if (start_hour <= tm->tm_hour && tm->tm_hour < end_hour)
  {
    return true;
  }
  else
  {
    return false;
  }
}

//60秒間隔でチェック
void check_clock()
{
  bool IsClock = check_clock_enable(CLOCK_EN_S, CLOCK_EN_E);

  static bool lock = true;

  if (IsClock == true)
  {
    if (lock == true)
    {
      clocker.attach_ms(500, blink);
      lock = false;
    }
  }
  else
  {
    if (lock == false)
    {
      clocker.detach();
      print_blank();
      print_blank();
      lock = true;
    }
  }
}

void initClock()
{
  //Get NTP Time
  configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  delay(2000);

  check_clock();

  checker.attach(60, check_clock);
}

void setup()
{
  initSerial();
  //フォントをメモリに展開
  SFR.SPIFFS_Shinonome_Init3F(UTF8SJIS_file, Shino_Half_Font_file, dummy);
  initLCDMatrix();
  initWiFi();
  initClock();
  initOta();
}

void loop()
{
  ArduinoOTA.handle();
}
