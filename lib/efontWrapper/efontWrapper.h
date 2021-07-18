
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Adafruit_GFX.h>

class efontWrapper : public Adafruit_GFX {
   public:
    efontWrapper(int16_t w, int16_t h);
    ~efontWrapper(void);

    void printEfont(const char *str);
    void printEfont(const char *str, int x, int y);
    void printEfont(const char *str, int x, int y, uint8_t textsize);

   private:
    const char *_efontUFT8toUTF16(uint16_t *pUTF16, const char *pUTF8);
    void _getefontData(uint8_t *font, uint16_t fontUTF16);
};

#ifdef __cplusplus
}
#endif
