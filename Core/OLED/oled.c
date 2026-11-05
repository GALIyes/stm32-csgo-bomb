/* ==========================================================================
 * SSD1306 OLED, software I2C, HAL port of the Zhongjingyuan demo.
 *
 * Changes vs the original:
 *   - StdPeriph (GPIO_SetBits / GPIO_Init / RCC_APB2PeriphClockCmd) -> HAL
 *   - sys.h / delay.h removed, u8/u32 -> unsigned char / unsigned long
 *   - pins moved to PB13(SCL) / PB12(SDA), see oled.h
 *   - OLED_Refresh() added as a no-op (GDDRAM is written directly)
 *   - size 24 now really renders 12x16 (the vendor table only has 6x8 and 8x16)
 * ======================================================================== */

#include "main.h"
#include "oled.h"
#include "oledfont.h"

static void IIC_Delay(void);

/* Pin roles are variables, not macros: the auto-detect below may have to swap
 * SCL and SDA at run time when the two wires are the wrong way round. */
static GPIO_TypeDef  *g_scl_port = OLED_SCL_PORT;
static GPIO_TypeDef  *g_sda_port = OLED_SDA_PORT;
static unsigned short g_scl_pin  = OLED_SCL_PIN;
static unsigned short g_sda_pin  = OLED_SDA_PIN;

/* slave address, already shifted left - this is the byte put on the bus */
static unsigned char g_i2c_addr = OLED_I2C_ADDR;

/* auto-detect outcome, read back through OLED_DiagText() */
static unsigned char g_probe_ok;
static unsigned char g_pins_swapped;

/* 128 x 64 monochrome frame buffer, one byte per column page.
 * 1024 bytes of RAM buys graphics primitives, flicker-free redraws and
 * arbitrary upscaling - all impossible when each call writes the panel. */
static unsigned char g_fb[8][128];

/* 1 = glyphs set pixels, 0 = glyphs clear them (text knocked out of a solid
 * fill, used for the full-screen BOOM flash). */
static unsigned char g_draw_on = 1u;

static void SCL_Hi(void) { HAL_GPIO_WritePin(g_scl_port, g_scl_pin, GPIO_PIN_SET); }
static void SCL_Lo(void) { HAL_GPIO_WritePin(g_scl_port, g_scl_pin, GPIO_PIN_RESET); }
static void SDA_Hi(void) { HAL_GPIO_WritePin(g_sda_port, g_sda_pin, GPIO_PIN_SET); }
static void SDA_Lo(void) { HAL_GPIO_WritePin(g_sda_port, g_sda_pin, GPIO_PIN_RESET); }

static void OLED_SetPinRoles(unsigned char swapped)
{
    if (swapped != 0u) {
        g_scl_port = OLED_SDA_PORT; g_scl_pin = OLED_SDA_PIN;
        g_sda_port = OLED_SCL_PORT; g_sda_pin = OLED_SCL_PIN;
    } else {
        g_scl_port = OLED_SCL_PORT; g_scl_pin = OLED_SCL_PIN;
        g_sda_port = OLED_SDA_PORT; g_sda_pin = OLED_SDA_PIN;
    }
    g_pins_swapped = swapped;
}

/* ---------------------------------------------------------------------------
 * GPIO setup. Done here on purpose: the CubeMX project does not configure
 * these two pins, and keeping it inside the driver means re-generating code
 * from the .ioc cannot break the display.
 *
 * Both GPIOA and GPIOB clocks are enabled so swapping the pins in oled.h
 * (e.g. to PA12/PA13) does not require touching this file. Two idle port
 * clocks cost a few microamps.
 * ------------------------------------------------------------------------ */
static void OLED_GPIO_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(g_scl_port, g_scl_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(g_sda_port, g_sda_pin, GPIO_PIN_SET);

    g.Pin   = g_scl_pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(g_scl_port, &g);

    g.Pin   = g_sda_pin;
    HAL_GPIO_Init(g_sda_port, &g);

    HAL_GPIO_WritePin(g_scl_port, g_scl_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(g_sda_port, g_sda_pin, GPIO_PIN_SET);
}

/* ------------------------------ soft I2C -------------------------------- */
/* START: SDA falls while SCL is high. SDA is released first so the previous
 * frame's last bit cannot be mistaken for a stop condition. */
static void IIC_Start(void)
{
    SDA_Hi();
    IIC_Delay();
    SCL_Hi();
    IIC_Delay();
    SDA_Lo();
    IIC_Delay();
    SCL_Lo();
    IIC_Delay();
}

/* STOP: SDA rises while SCL is high. Change SDA with SCL low, then clock up. */
static void IIC_Stop(void)
{
    SCL_Lo();
    IIC_Delay();
    SDA_Lo();
    IIC_Delay();
    SCL_Hi();
    IIC_Delay();
    SDA_Hi();
    IIC_Delay();
}

/*
 * Bit-bang delay.
 *
 * Speed matters: this project builds at -O3, where HAL_GPIO_WritePin gets
 * inlined down to a single register write. Without the delay below the bit
 * clock lands around 1.5-2.5 MHz and the SSD1306 (400 kHz max) silently
 * ignores every byte - the vendor demo only worked because it was built with
 * the slower StdPeriph library and no optimisation.
 *
 * n = 16 puts one bit at roughly 3 us, i.e. 320 kHz.
 */
static void IIC_Delay(void)
{
    volatile unsigned char n = 16;
    while (n--) {
        __NOP();
    }
}

static void IIC_WriteByte(unsigned char b)
{
    unsigned char i;

    SCL_Lo();
    for (i = 0; i < 8; i++) {
        if ((b & 0x80u) != 0u) {
            SDA_Hi();
        } else {
            SDA_Lo();
        }
        b <<= 1;
        IIC_Delay();
        SCL_Hi();
        IIC_Delay();
        SCL_Lo();
    }
}

/*
 * Read the ACK slot (the 9th clock).
 *
 * Standard I2C: the master releases SDA, then clocks the 9th bit high; the
 * slave pulls SDA low *while SCL is high*. Sampling before SCL rises reads the
 * idle-high line and every probe would report NACK, so the order below matters.
 *
 * Returns 0 when the slave acknowledged. SDA is left as a push-pull high so the
 * caller can carry on writing.
 */
static unsigned char IIC_WaitAck(void)
{
    GPIO_InitTypeDef g;
    unsigned char v;

    g.Pin   = g_sda_pin;
    g.Mode  = GPIO_MODE_INPUT;      /* release the line, the slave drives it */
    g.Pull  = GPIO_PULLUP;          /* keeps it defined if the module has no pull-up */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(g_sda_port, &g);

    IIC_Delay();
    SCL_Hi();
    IIC_Delay();                    /* let the slave drive the bit */
    v = (HAL_GPIO_ReadPin(g_sda_port, g_sda_pin) == GPIO_PIN_SET) ? 1u : 0u;
    SCL_Lo();
    IIC_Delay();

    HAL_GPIO_WritePin(g_sda_port, g_sda_pin, GPIO_PIN_SET);   /* idle high before switching back */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(g_sda_port, &g);

    return v;                       /* 0 = ACK, 1 = NACK */
}

/* Ninth clock pulse without sampling. Used when streaming the frame buffer:
 * the panel has already proven it is there, and re-configuring SDA as an input
 * 1024 times would cost more than the transfer itself. */
static void IIC_ClockAck(void)
{
    SCL_Hi();
    IIC_Delay();
    SCL_Lo();
    IIC_Delay();
}

/* 1 = something answered at this address byte */
static unsigned char IIC_Probe(unsigned char addr_byte)
{
    unsigned char nack;

    IIC_Start();
    IIC_WriteByte(addr_byte);
    nack = IIC_WaitAck();
    IIC_Stop();

    return (nack == 0u) ? 1u : 0u;
}

static void IIC_WriteCmd(unsigned char c)
{
    IIC_Start();
    IIC_WriteByte(g_i2c_addr);
    IIC_WaitAck();
    IIC_WriteByte(0x00u);
    IIC_WaitAck();
    IIC_WriteByte(c);
    IIC_WaitAck();
    IIC_Stop();
}

static void IIC_WriteData(unsigned char d)
{
    IIC_Start();
    IIC_WriteByte(g_i2c_addr);
    IIC_WaitAck();
    IIC_WriteByte(0x40u);
    IIC_WaitAck();
    IIC_WriteByte(d);
    IIC_WaitAck();
    IIC_Stop();
}

/*
 * Try every plausible combination and keep the first one that answers.
 *
 *   1. address 0x78, SCL/SDA as declared in oled.h   (the normal case)
 *   2. address 0x7A                                  (SA0 strapped high)
 *   3. both of the above with the two wires swapped  (SCL/SDA crossed)
 *
 * Two attempts per combination: after power-up the bus can be left mid-frame
 * by whatever ran before, and the first START then gets swallowed. A dummy
 * STOP before each probe puts the slave back into a known state.
 */
static unsigned char OLED_AutoDetect(void)
{
    static const unsigned char addr_list[2] = {0x78u, 0x7Au};
    unsigned char sw, a, tries;

    for (sw = 0; sw < 2u; sw++) {
        OLED_SetPinRoles(sw);
        OLED_GPIO_Init();

        for (a = 0; a < 2u; a++) {
            g_i2c_addr = addr_list[a];

            IIC_Stop();                        /* resync a slave stuck mid-frame */

            for (tries = 0; tries < 3u; tries++) {
                if (IIC_Probe(addr_list[a]) != 0u) {
                    g_probe_ok = 1u;
                    return 1u;
                }
            }
        }
    }

    /* nothing answered - fall back to the declared configuration so the old
     * blind-write behaviour is preserved and the caller still gets a picture */
    OLED_SetPinRoles(0u);
    OLED_GPIO_Init();
    g_i2c_addr = OLED_I2C_ADDR;
    g_probe_ok = 0u;

    return 0u;
}

void OLED_WR_Byte(unsigned char dat, unsigned char cmd)
{
    if (cmd != 0u) {
        IIC_WriteData(dat);
    } else {
        IIC_WriteCmd(dat);
    }
}

/* ------------------------------ addressing ------------------------------ */
void OLED_Set_Pos(unsigned char x, unsigned char y)
{
    OLED_WR_Byte((unsigned char)(0xB0u + y), OLED_CMD);
    OLED_WR_Byte((unsigned char)(((x & 0xF0u) >> 4) | 0x10u), OLED_CMD);
    OLED_WR_Byte((unsigned char)(x & 0x0Fu), OLED_CMD);
}

/* ---------------------------- frame buffer ------------------------------ */
void OLED_ClearBuf(void)
{
    unsigned int i;

    for (i = 0u; i < sizeof(g_fb); i++) {
        ((unsigned char *)g_fb)[i] = 0x00u;
    }
}

/* Clears the buffer only. The panel follows on OLED_Refresh(), so a caller can
 * build a whole screen without the erase ever being visible. */
void OLED_Clear(void)
{
    OLED_ClearBuf();
}

void OLED_SetPixel(unsigned char x, unsigned char y, unsigned char on)
{
    if ((x >= X_WIDTH) || (y >= Y_WIDTH)) {
        return;
    }
    if (on != 0u) {
        g_fb[y >> 3][x] |= (unsigned char)(1u << (y & 7u));
    } else {
        g_fb[y >> 3][x] &= (unsigned char)(~(1u << (y & 7u)));
    }
}

void OLED_HLine(unsigned char x0, unsigned char x1, unsigned char y, unsigned char on)
{
    unsigned char t;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    for (t = x0; ; t++) {
        OLED_SetPixel(t, y, on);
        if (t == x1) {
            break;
        }
    }
}

void OLED_VLine(unsigned char x, unsigned char y0, unsigned char y1, unsigned char on)
{
    unsigned char t;

    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (t = y0; ; t++) {
        OLED_SetPixel(x, t, on);
        if (t == y1) {
            break;
        }
    }
}

void OLED_Rect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char on)
{
    unsigned char t;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    OLED_HLine(x0, x1, y0, on);
    OLED_HLine(x0, x1, y1, on);
    OLED_VLine(x0, y0, y1, on);
    OLED_VLine(x1, y0, y1, on);
}

void OLED_FillRect(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char on)
{
    unsigned char t, y;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    for (y = y0; ; y++) {
        OLED_HLine(x0, x1, y, on);
        if (y == y1) {
            break;
        }
    }
}

/* Midpoint circle. Negative coordinates fall outside the buffer and are
 * dropped by OLED_SetPixel, so no clipping is needed here. */
void OLED_Circle(int cx, int cy, int r, unsigned char on)
{
    int x, y, err;

    if (r <= 0) {
        return;
    }

    x   = r;
    y   = 0;
    err = 1 - r;

    while (x >= y) {
        OLED_SetPixel((unsigned char)(cx + x), (unsigned char)(cy + y), on);
        OLED_SetPixel((unsigned char)(cx + y), (unsigned char)(cy + x), on);
        OLED_SetPixel((unsigned char)(cx - y), (unsigned char)(cy + x), on);
        OLED_SetPixel((unsigned char)(cx - x), (unsigned char)(cy + y), on);
        OLED_SetPixel((unsigned char)(cx - x), (unsigned char)(cy - y), on);
        OLED_SetPixel((unsigned char)(cx - y), (unsigned char)(cy - x), on);
        OLED_SetPixel((unsigned char)(cx + y), (unsigned char)(cy - x), on);
        OLED_SetPixel((unsigned char)(cx + x), (unsigned char)(cy - y), on);

        y++;
        if (err < 0) {
            err += (2 * y) + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ------------------------------- init ----------------------------------- */
void OLED_Init(void)
{
    OLED_SetPinRoles(0u);
    OLED_GPIO_Init();

    HAL_Delay(300);                 /* panel power-on reset; raise if unstable */

    (void)OLED_AutoDetect();        /* leaves GPIOs + g_i2c_addr on the winning combo */

    OLED_WR_Byte(0xAE, OLED_CMD);   /* display off */
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0x10, OLED_CMD);
    OLED_WR_Byte(0x40, OLED_CMD);
    OLED_WR_Byte(0xB0, OLED_CMD);
    OLED_WR_Byte(0x81, OLED_CMD);   /* contrast */
    OLED_WR_Byte(0xFF, OLED_CMD);
    OLED_WR_Byte(0xA1, OLED_CMD);   /* segment remap */
    OLED_WR_Byte(0xA6, OLED_CMD);   /* normal video */
    OLED_WR_Byte(0xA8, OLED_CMD);   /* multiplex ratio */
    OLED_WR_Byte(0x3F, OLED_CMD);
    OLED_WR_Byte(0xC8, OLED_CMD);   /* COM scan direction */
    OLED_WR_Byte(0xD3, OLED_CMD);   /* display offset */
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0xD5, OLED_CMD);   /* osc / clock divide */
    OLED_WR_Byte(0x80, OLED_CMD);
    OLED_WR_Byte(0xD8, OLED_CMD);   /* area colour mode */
    OLED_WR_Byte(0x05, OLED_CMD);
    OLED_WR_Byte(0xD9, OLED_CMD);   /* pre-charge period */
    OLED_WR_Byte(0xF1, OLED_CMD);
    OLED_WR_Byte(0xDA, OLED_CMD);   /* COM pins */
    OLED_WR_Byte(0x12, OLED_CMD);
    OLED_WR_Byte(0xDB, OLED_CMD);   /* VCOMH */
    OLED_WR_Byte(0x30, OLED_CMD);
    OLED_WR_Byte(0x8D, OLED_CMD);   /* charge pump */
    OLED_WR_Byte(0x14, OLED_CMD);
    OLED_WR_Byte(0xAF, OLED_CMD);   /* display on */

    OLED_Clear();                   /* buffer only ... */
    OLED_Refresh();                 /* ... then push the blank frame */
}

void OLED_Display_On(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);
    OLED_WR_Byte(0xAF, OLED_CMD);
}

void OLED_Display_Off(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x10, OLED_CMD);
    OLED_WR_Byte(0xAE, OLED_CMD);
}

/*
 * Push the whole buffer.
 *
 * One START per page rather than one per byte: the 1024-byte frame goes out in
 * about 30 ms instead of 90 ms, which is what makes a 10 Hz redraw of the
 * countdown affordable.
 */
void OLED_Refresh(void)
{
    unsigned char page, col;

    for (page = 0u; page < 8u; page++) {
        OLED_WR_Byte((unsigned char)(0xB0u + page), OLED_CMD);
        OLED_WR_Byte(0x00u, OLED_CMD);
        OLED_WR_Byte(0x10u, OLED_CMD);

        IIC_Start();
        IIC_WriteByte(g_i2c_addr);
        IIC_WaitAck();
        IIC_WriteByte(0x40u);
        IIC_WaitAck();
        for (col = 0u; col < 128u; col++) {
            IIC_WriteByte(g_fb[page][col]);
            IIC_ClockAck();
        }
        IIC_Stop();
    }
}

/* ---------------------------------------------------------------------------
 * Diagnostics for the Bring-up screen.
 *
 * OLED_Init() probes the bus before it starts drawing, so these tell you what
 * the wiring actually is instead of what oled.h claims:
 *
 *   "OK"       declared config works (0x78, SCL=PB13 / SDA=PB12)
 *   "ADDR 7A"  the module's SA0 is strapped high
 *   "SWAP"     SCL and SDA are the wrong way round
 *   "SWAP+7A"  both of the above
 *   "NO ACK"   nothing answered on any combination - it is wiring or power,
 *              not a software setting
 * ------------------------------------------------------------------------ */
unsigned char OLED_ProbeOK(void)
{
    return g_probe_ok;
}

const char *OLED_DiagText(void)
{
    if (g_probe_ok == 0u) {
        return "NO ACK";
    }
    if (g_pins_swapped != 0u) {
        return (g_i2c_addr == 0x7Au) ? "SWAP+7A" : "SWAP";
    }
    return (g_i2c_addr == 0x7Au) ? "ADDR 7A" : "OK";
}

/* ------------------------------- drawing -------------------------------- */
static unsigned char OLED_GlyphW(unsigned char size, unsigned char scale)
{
    unsigned char w = (size == 16u) ? 8u : 6u;

    return (unsigned char)(w * ((scale == 0u) ? 1u : scale));
}

static unsigned char OLED_GlyphH(unsigned char size, unsigned char scale)
{
    unsigned char h = (size == 16u) ? 16u : 8u;

    return (unsigned char)(h * ((scale == 0u) ? 1u : scale));
}

/*
 * Render one glyph into the buffer.
 *
 * scale >= 1 replicates every pixel into a scale x scale square, so the big
 * countdown digits come out of the same table as the small labels - no extra
 * font data in flash.
 */
static void OLED_BufGlyph(unsigned char x, unsigned char y, unsigned char chr,
                          unsigned char size, unsigned char scale)
{
    unsigned char c, i, r, b, part, parts, w, dx, dy;
    const unsigned char *base;

    if (chr < ' ') {
        chr = ' ';
    }
    if (scale == 0u) {
        scale = 1u;
    }

    c = (unsigned char)(chr - ' ');

    if (size == 16u) {
        if (c >= 95u) {                 /* F8X16 covers ' ' .. '~' */
            c = 0u;
        }
        w     = 8u;
        parts = 2u;
        base  = &F8X16[(unsigned int)c * 16u];
    } else {
        if (c > 91u) {                  /* F6x8 stops at the custom bar glyph */
            c = 0u;
        }
        w     = 6u;
        parts = 1u;
        base  = F6x8[c];
    }

    for (part = 0u; part < parts; part++) {
        for (i = 0u; i < w; i++) {
            b = base[i + (unsigned int)part * 8u];
            for (r = 0u; r < 8u; r++) {
                if ((b & (unsigned char)(1u << r)) == 0u) {
                    continue;
                }
                for (dy = 0u; dy < scale; dy++) {
                    for (dx = 0u; dx < scale; dx++) {
                        OLED_SetPixel((unsigned char)(x + i * scale + dx),
                                      (unsigned char)(y + ((part * 8u) + r) * scale + dy),
                                      g_draw_on);
                    }
                }
            }
        }
    }
}

void OLED_SetDrawMode(unsigned char on)
{
    g_draw_on = (on != 0u) ? 1u : 0u;
}

void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char chr, unsigned char size)
{
    OLED_BufGlyph(x, y, chr, size, 1u);
}

void OLED_ShowStringScale(unsigned char x, unsigned char y, const char *p,
                          unsigned char size, unsigned char scale)
{
    unsigned char gw, gh;

    if (scale == 0u) {
        scale = 1u;
    }
    gw = OLED_GlyphW(size, scale);
    gh = OLED_GlyphH(size, scale);

    while ((p != NULL) && (*p != '\0')) {
        if ((unsigned int)x + gw > X_WIDTH) {
            x = 0;
            y = (unsigned char)(y + gh);
        }
        if ((unsigned int)y + gh > Y_WIDTH) {
            break;                          /* off screen, stop instead of wrapping */
        }
        OLED_BufGlyph(x, y, (unsigned char)(*p), size, scale);
        x = (unsigned char)(x + gw);
        p++;
    }
}

void OLED_ShowString(unsigned char x, unsigned char y, const char *p, unsigned char size)
{
    OLED_ShowStringScale(x, y, p, size, 1u);
}

void OLED_ShowNumScale(unsigned char x, unsigned char y, unsigned long num,
                       unsigned char len, unsigned char size, unsigned char scale)
{
    unsigned char t, k, gw;
    unsigned long p10;

    if (scale == 0u) {
        scale = 1u;
    }
    gw = OLED_GlyphW(size, scale);

    for (t = 0u; t < len; t++) {
        p10 = 1u;
        for (k = 0u; k < (unsigned char)(len - t - 1u); k++) {
            p10 *= 10u;
        }
        OLED_BufGlyph((unsigned char)(x + t * gw), y,
                      (unsigned char)('0' + ((num / p10) % 10u)), size, scale);
    }
}

void OLED_ShowNum(unsigned char x, unsigned char y, unsigned long num,
                  unsigned char len, unsigned char size)
{
    OLED_ShowNumScale(x, y, num, len, size, 1u);
}
