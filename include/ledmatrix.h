#include <Arduino.h>

//LED port settings
#define PORT_SE_IN  13
#define PORT_AB_IN  27
#define PORT_A3_IN  23
#define PORT_A2_IN  21
#define PORT_A1_IN  25
#define PORT_A0_IN  26
#define PORT_DG_IN  19
#define PORT_CLK_IN 18
#define PORT_WE_IN  17
#define PORT_DR_IN  16
#define PORT_ALE_IN 22
#define PORT_LAMP   5

#define PANEL_NUM   2  //LED Panel
#define R           1  //red
#define O           2  //orange
#define G           3  //green

//Write setting to LED Panel
void setRAMAdder(uint8_t lineNumber) {
    uint8_t A[4]  = {0};
    uint8_t adder = 0;

    adder = lineNumber;

    for (int i = 0; i < 4; i++) {
        A[i] = adder % 2;
        adder /= 2;
    }

    digitalWrite(PORT_A0_IN, A[0]);
    digitalWrite(PORT_A1_IN, A[1]);
    digitalWrite(PORT_A2_IN, A[2]);
    digitalWrite(PORT_A3_IN, A[3]);
}

void send_line_data(uint8_t iram_adder, uint8_t ifont_data[], uint8_t color_data[]) {
    uint8_t font[8]  = {0};
    uint8_t tmp_data = 0;
    int k            = 0;
    for (int j = 0; j < 4 * PANEL_NUM; j++) {
        tmp_data = ifont_data[j];
        for (int i = 0; i < 8; i++) {
            font[i] = tmp_data % 2;
            tmp_data /= 2;
        }

        for (int i = 7; i >= 0; i--) {
            digitalWrite(PORT_DG_IN, LOW);
            digitalWrite(PORT_DR_IN, LOW);
            digitalWrite(PORT_CLK_IN, LOW);

            if (font[i] == 1) {
                if (color_data[k] == R) {
                    digitalWrite(PORT_DR_IN, HIGH);
                }

                if (color_data[k] == G) {
                    digitalWrite(PORT_DG_IN, HIGH);
                }

                if (color_data[k] == O) {
                    digitalWrite(PORT_DR_IN, HIGH);
                    digitalWrite(PORT_DG_IN, HIGH);
                }
            } else {
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

void shift_bit_left(uint8_t dist[], uint8_t src[], int len, int n) {
    uint8_t mask = 0xFF << (8 - n);
    for (int i = 0; i < len; i++) {
        if (i < len - 1) {
            dist[i] = (src[i] << n) | ((src[i + 1] & mask) >> (8 - n));
        } else {
            dist[i] = src[i] << n;
        }
    }
}

void shift_color_left(uint8_t dist[], uint8_t src[], int len) {
    for (int i = 0; i < len * 8; i++) {
        if (i < len * 8 - 1) {
            dist[i] = src[i + 1];
        } else {
            dist[i] = 0;
        }
    }
}
////////////////////////////////////////////////////////////////////
void scrollLEDMatrix(int16_t sj_length, uint8_t font_data[][16], uint8_t color_data[], uint16_t intervals) {
    uint8_t src_line_data[sj_length]      = {0};
    uint8_t dist_line_data[sj_length]     = {0};
    uint8_t tmp_color_data[sj_length * 8] = {0};
    uint8_t tmp_font_data[sj_length][16]  = {0};
    uint8_t ram                           = LOW;

    int n = 0;
    for (int i = 0; i < sj_length; i++) {
        for (int j = 0; j < 8; j++) {
            tmp_color_data[n++] = color_data[i];
        }

        for (int j = 0; j < 16; j++) {
            tmp_font_data[i][j] = font_data[i][j];
        }
    }

    for (int k = 0; k < sj_length * 8 + 2; k++) {
        ram = ~ram;
        digitalWrite(PORT_AB_IN, ram);  //write to RAM-A/RAM-B
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < sj_length; j++) {
                src_line_data[j] = tmp_font_data[j][i];
            }

            send_line_data(i, src_line_data, tmp_color_data);
            shift_bit_left(dist_line_data, src_line_data, sj_length, 1);

            for (int j = 0; j < sj_length; j++) {
                tmp_font_data[j][i] = dist_line_data[j];
            }
        }
        shift_color_left(tmp_color_data, tmp_color_data, sj_length);
        delay(intervals);
    }
}

//Print static
void printLEDMatrix(uint16_t sj_length, uint8_t font_data[][16], uint8_t color_data[]) {
    uint8_t src_line_data[sj_length]      = {0};
    uint8_t tmp_color_data[sj_length * 8] = {0};
    uint8_t tmp_font_data[sj_length][16]  = {0};
    uint8_t ram                           = LOW;

    int n = 0;
    for (int i = 0; i < sj_length; i++) {
        for (int j = 0; j < 8; j++) {
            tmp_color_data[n++] = color_data[i];
        }

        for (int j = 0; j < 16; j++) {
            tmp_font_data[i][j] = font_data[i][j];
        }
    }

    for (int k = 0; k < sj_length * 8 + 2; k++) {
        digitalWrite(PORT_AB_IN, ram);  //write to RAM-A/RAM-B
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < sj_length; j++) {
                src_line_data[j] = tmp_font_data[j][i];
            }

            send_line_data(i, src_line_data, tmp_color_data);
        }
        ram = ~ram;
    }
}

void setAllPortOutput() {
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
    pinMode(PORT_LAMP, OUTPUT);
}

void setAllPortLow() {
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
    digitalWrite(PORT_LAMP, LOW);
}
