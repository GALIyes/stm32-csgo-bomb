#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/* ==========================================================================
 * 0.96/0.69 inch SSD1306, software I2C, write-only.
 *
 * Ported from the vendor (Zhongjingyuan) StdPeriph demo to HAL.
 *
 * Hardware: 4-pin module, 2 signal wires (SCL / SDA) plus 3V3 and GND.
 *
 * The vendor code never read the ACK bit (its IIC_Wait_Ack() was an empty
 * function), so a miswired panel produced no error at all. This port samples
 * the ACK slot, and OLED_Init() uses that to probe the bus: it tries 0x78 and
 * 0x7A, with SCL/SDA in both orders, and keeps whatever answers.
 *
 * The defines below are therefore the *preferred* configuration, not the final
 * one - if the panel lights up with a swapped pair you do not have to rewire
 * anything, but OLED_DiagText() will say "SWAP" and you may prefer to fix the
 * cable and get "OK".
 * ======================================================================== */

/* ---- pins: change only these six lines ---- */
#define OLED_SCL_PORT     GPIOB
#define OLED_SCL_PIN      GPIO_PIN_13
#define OLED_SDA_PORT     GPIOB
#define OLED_SDA_PIN      GPIO_PIN_12

#define OLED_SCLK_Clr()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_RESET)
#define OLED_SCLK_Set()   HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_SET)
#define OLED_SDIN_Clr()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_RESET)
#define OLED_SDIN_Set()   HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_SET)

#define OLED_CMD          0
#define OLED_DATA         1
#define OLED_I2C_ADDR     0x78

#define Max_Column        128
#define Max_Row           64
#define X_WIDTH           128
#define Y_WIDTH           64

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Set_Pos(unsigned char x, unsigned char y);
void OLED_WR_Byte(unsigned char dat, unsigned char cmd);
void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char chr, unsigned char size);
void OLED_ShowString(unsigned char x, unsigned char y, const char *p, unsigned char size);
void OLED_ShowNum(unsigned char x, unsigned char y, unsigned long num,
                  unsigned char len, unsigned char size);

/* ---- frame buffer drawing ----------------------------------------------
 * Everything below draws into the local 128x64 buffer; nothing reaches the
 * panel until OLED_Refresh(). That is what makes graphics (lines, boxes,
 * bars) possible, and it also removes the flicker you get when erasing and
 * redrawing straight on the panel.
 *
 * Coordinates are pixels, (0,0) is the top-left corner. Out-of-range points
 * are clipped silently, so drawing off the edge is safe.
 * ---------------------------------------------------------------------- */
void OLED_ClearBuf(void);
void OLED_SetPixel(unsigned char x, unsigned char y, unsigned char on);
void OLED_HLine(unsigned char x0, unsigned char x1, unsigned char y, unsigned char on);
void OLED_VLine(unsigned char x, unsigned char y0, unsigned char y1, unsigned char on);
void OLED_Rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char on);
void OLED_FillRect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char on);
void OLED_Circle(int cx, int cy, int r, unsigned char on);

/* 1 = draw glyphs normally, 0 = clear pixels instead (inverted text) */
void OLED_SetDrawMode(unsigned char on);

/* size 8 or 16, scale 1..4 - scale 2 on size 16 gives a 16x32 digit */
void OLED_ShowStringScale(unsigned char x, unsigned char y, const char *p,
                          unsigned char size, unsigned char scale);
void OLED_ShowNumScale(unsigned char x, unsigned char y, unsigned long num,
                       unsigned char len, unsigned char size, unsigned char scale);

/*
 * The vendor 0.69" demo writes straight into GDDRAM, there is no frame buffer,
 * so nothing has to be flushed. The main program calls OLED_Refresh() anyway -
 * keep it as a no-op so the calling code does not have to change.
 */
void OLED_Refresh(void);

/*
 * Bring-up diagnostics. OLED_Init() probes the bus first and will swap SCL/SDA
 * or fall back to address 0x7A on its own if the wiring differs from the
 * defines above, so the panel can light up even when a wire is crossed.
 *
 * OLED_ProbeOK()  : 1 if the slave acknowledged, 0 if nothing answered.
 * OLED_DiagText() : short string for the screen, see oled.c.
 */
unsigned char OLED_ProbeOK(void);
const char *OLED_DiagText(void);

#endif /* __OLED_H */
