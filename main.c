/* ============================================================================
 * Smart Weather Station ? I2C / DHT22 / DS3231 Edition
 * PIC16F877A @ 20 MHz, XC8 Compiler (Anti-Freeze Timeout Edition)
 * ============================================================================
 */

#pragma config FOSC  = HS
#pragma config WDTE  = OFF
#pragma config PWRTE = ON
#pragma config BOREN = ON
#pragma config LVP   = OFF
#pragma config CPD   = OFF
#pragma config WRT   = OFF
#pragma config CP    = OFF

#include <xc.h>

#define _XTAL_FREQ      20000000UL

/* --- I2C Addresses --- */
#define DS3231_ADDR     0xD0        
#define LCD_I2C_ADDR    0x4E        

/* --- LCD PCF8574 Bit Map --- */
#define LCD_RS          0x01
#define LCD_RW          0x02
#define LCD_E           0x04
#define LCD_BL          0x08        

/* --- Hardware Pins --- */
// Changed to RD2/TRISD2 to match your physical wiring setup
#define DHT22_DATA      PORTDbits.RD2
#define DHT22_TRIS      TRISDbits.TRISD2
#define ALERT_LED       PORTBbits.RB6
#define ALERT_TRIS      TRISBbits.TRISB6

#define TEMP_ALERT_X10  300         

/* --- Data Logging Layout --- */
#define DS3231_SRAM_BASE    0x14
#define DS3231_SRAM_SIZE    236
#define SRAM_HEADER_SIZE    2
#define SAMPLE_SIZE         10
#define MAX_SAMPLES         ((DS3231_SRAM_SIZE - SRAM_HEADER_SIZE) / SAMPLE_SIZE)  

/* ========================================================================== */
/* GLOBALS & INTERRUPT SERVICE ROUTINE                                        */
/* ========================================================================== */
static unsigned char rtc_sec = 0, rtc_min = 30, rtc_hr = 14;
static unsigned char rtc_date = 28, rtc_mon = 5, rtc_yr = 26;

volatile unsigned char timer1_ticks = 0;
volatile unsigned char take_sample_flag = 0;

static unsigned char sram_index = 0;
static unsigned char sram_count = 0;

void __interrupt() ISR(void) {
    if (PIR1bits.TMR1IF) {
        TMR1H = 0x0B; 
        TMR1L = 0xDC;
        PIR1bits.TMR1IF = 0;
        timer1_ticks++;
        if (timer1_ticks >= 20) {       
            timer1_ticks = 0;
            take_sample_flag = 1;
        }
    }
}

/* ========================================================================== */
/* BCD UTILITIES                                                              */
/* ========================================================================== */
static unsigned char BCDtoDEC(unsigned char b) { return ((b >> 4) * 10) + (b & 0x0F); }
static unsigned char DECtoBCD(unsigned char d) { return ((d / 10) << 4) | (d % 10); }

/* ========================================================================== */
/* ANTI-FREEZE I2C MASTER DRIVER                                             */
/* ========================================================================== */
static void I2C_Init(void) {
    TRISC3 = 1; TRISC4 = 1;             
    SSPSTAT = 0x80;                     
    SSPCON  = 0x28;                     
    SSPCON2 = 0x00;
    SSPADD  = 99;                       
}

static void I2C_Wait(void) { 
    unsigned int timeout = 5000;
    while (((SSPCON2 & 0x1F) || (SSPSTAT & 0x04)) && timeout--) { /* Safe Protection Timeout */ } 
}

static void I2C_Start(void) {
    I2C_Wait(); 
    SEN = 1; 
    unsigned int timeout = 5000;
    while (SEN && timeout--) { }
}

static void I2C_Stop(void) {
    I2C_Wait(); 
    PEN = 1; 
    unsigned int timeout = 5000;
    while (PEN && timeout--) { }
}

static void I2C_Write(unsigned char b) {
    unsigned int timeout = 0;
    I2C_Wait(); 
    SSPIF = 0; 
    SSPBUF = b;
    while (!SSPIF && ++timeout < 5000) { }
}

static unsigned char I2C_Read(unsigned char ack) {
    unsigned char d = 0;
    unsigned int timeout = 5000;
    I2C_Wait(); 
    RCEN = 1; 
    while (!BF && timeout--) { }
    d = SSPBUF;
    I2C_Wait(); 
    ACKDT = ack ? 0 : 1; 
    ACKEN = 1;
    timeout = 5000;
    while (ACKEN && timeout--) { }
    return d;
}

/* ========================================================================== */
/* DS3231 HARDWARE RTC                                                        */
/* ========================================================================== */
static void DS3231_ReadTime(void) {
    I2C_Start();
    I2C_Write(DS3231_ADDR);
    I2C_Write(0x00);                  
    I2C_Start();
    I2C_Write(DS3231_ADDR | 0x01);     
    rtc_sec  = BCDtoDEC(I2C_Read(1));
    rtc_min  = BCDtoDEC(I2C_Read(1));
    rtc_hr   = BCDtoDEC(I2C_Read(1));
    I2C_Read(1);                      
    rtc_date = BCDtoDEC(I2C_Read(1));
    rtc_mon  = BCDtoDEC(I2C_Read(1));
    rtc_yr   = BCDtoDEC(I2C_Read(0));     
    I2C_Stop();
}

static void DS3231_SetTime(unsigned char hr, unsigned char min, unsigned char sec,
                           unsigned char date, unsigned char mon, unsigned char yr) {
    I2C_Start();
    I2C_Write(DS3231_ADDR);
    I2C_Write(0x00);
    I2C_Write(DECtoBCD(sec));
    I2C_Write(DECtoBCD(min));
    I2C_Write(DECtoBCD(hr));            
    I2C_Write(0x01);                    
    I2C_Write(DECtoBCD(date));
    I2C_Write(DECtoBCD(mon));
    I2C_Write(DECtoBCD(yr));
    I2C_Stop();
}

/* ========================================================================== */
/* COOPERATIVE MEMORY LOGGING STORAGE                                         */
/* ========================================================================== */
static unsigned char SRAM_Read(unsigned char addr) {
    unsigned char d;
    I2C_Start();
    I2C_Write(DS3231_ADDR);
    I2C_Write(DS3231_SRAM_BASE + addr);
    I2C_Start();
    I2C_Write(DS3231_ADDR | 0x01);
    d = I2C_Read(0);
    I2C_Stop();
    return d;
}

static void SRAM_Write(unsigned char addr, unsigned char data) {
    I2C_Start();
    I2C_Write(DS3231_ADDR);
    I2C_Write(DS3231_SRAM_BASE + addr);
    I2C_Write(data);
    I2C_Stop();
}

static void SRAM_LoadHeader(void) {
    sram_index = SRAM_Read(0);
    sram_count = SRAM_Read(1);
    if (sram_index >= MAX_SAMPLES) { sram_index = 0; sram_count = 0; }
}

static void SRAM_SaveHeader(void) {
    SRAM_Write(0, sram_index);  __delay_ms(5);
    SRAM_Write(1, sram_count);  __delay_ms(5);
}

static void SRAM_SaveSample(unsigned char hr, unsigned char min, unsigned char sec,
                            unsigned char date, unsigned char mon, unsigned char yr,
                            signed int temp_x10, unsigned char hum,
                            unsigned char light, unsigned char alert) {
    unsigned char base = SRAM_HEADER_SIZE + (sram_index * SAMPLE_SIZE);
    SRAM_Write(base + 0, hr);       __delay_ms(2);
    SRAM_Write(base + 1, min);      __delay_ms(2);
    SRAM_Write(base + 2, sec);      __delay_ms(2);
    SRAM_Write(base + 3, date);     __delay_ms(2);
    SRAM_Write(base + 4, mon);      __delay_ms(2);
    SRAM_Write(base + 5, yr);       __delay_ms(2);
    SRAM_Write(base + 6, (unsigned char)(temp_x10 & 0xFF));        __delay_ms(2);
    SRAM_Write(base + 7, (unsigned char)((temp_x10 >> 8) & 0xFF)); __delay_ms(2);
    SRAM_Write(base + 8, hum);      __delay_ms(2);
    SRAM_Write(base + 9, (unsigned char)(light | (alert ? 0x80 : 0))); __delay_ms(2);

    sram_index++;
    if (sram_index >= MAX_SAMPLES) sram_index = 0;
    if (sram_count < MAX_SAMPLES) sram_count++;
    SRAM_SaveHeader();
}

static void SRAM_ReadSample(unsigned char idx, unsigned char *hr, unsigned char *min, unsigned char *sec,
                            unsigned char *date, unsigned char *mon, unsigned char *yr,
                            signed int *temp_x10, unsigned char *hum,
                            unsigned char *light, unsigned char *alert) {
    unsigned char base = SRAM_HEADER_SIZE + (idx * SAMPLE_SIZE);
    *hr  = SRAM_Read(base + 0);
    *min = SRAM_Read(base + 1);
    *sec = SRAM_Read(base + 2);
    *date = SRAM_Read(base + 3);
    *mon  = SRAM_Read(base + 4);
    *yr   = SRAM_Read(base + 5);
    *temp_x10 = (signed int)(SRAM_Read(base + 6) | ((unsigned int)SRAM_Read(base + 7) << 8));
    *hum  = SRAM_Read(base + 8);
    unsigned char hl = SRAM_Read(base + 9);
    *light = hl & 0x7F;
    *alert = (hl & 0x80) ? 1 : 0;
}

/* ========================================== */
/* I2C LCD CORE ENGINE                        */
/* ========================================== */
static void LCD_I2C_SendNibble(unsigned char nibble, unsigned char rs) {
    unsigned char val = (nibble & 0xF0) | LCD_BL | (rs ? LCD_RS : 0);
    I2C_Start();
    I2C_Write(LCD_I2C_ADDR);
    I2C_Write(val | LCD_E);  __delay_us(25);
    I2C_Write(val & ~LCD_E); __delay_us(50);
    I2C_Stop();
}

static void LCD_I2C_SendByte(unsigned char b, unsigned char rs) {
    unsigned char high = b & 0xF0;
    unsigned char low  = (b << 4) & 0xF0;
    unsigned char base = LCD_BL | (rs ? LCD_RS : 0);
    I2C_Start();
    I2C_Write(LCD_I2C_ADDR);
    I2C_Write(high | base | LCD_E);  __delay_us(25);
    I2C_Write(high | base);            __delay_us(50);
    I2C_Write(low  | base | LCD_E);  __delay_us(25);
    I2C_Write(low  | base);            __delay_us(50);
    I2C_Stop();
}

static void LCD_Cmd(unsigned char c)  { LCD_I2C_SendByte(c, 0); __delay_ms(2); }
static void LCD_Data(unsigned char d) { LCD_I2C_SendByte(d, 1); }

static void LCD_Init(void) {
    __delay_ms(50);
    LCD_I2C_SendNibble(0x30, 0); __delay_ms(5);
    LCD_I2C_SendNibble(0x30, 0); __delay_us(150);
    LCD_I2C_SendNibble(0x30, 0); __delay_us(150);
    LCD_I2C_SendNibble(0x20, 0); __delay_us(150);   
    LCD_Cmd(0x28);                                    
    LCD_Cmd(0x0C);                                    
    LCD_Cmd(0x06);                                    
    LCD_Cmd(0x01);                                    
    __delay_ms(2);
}

static void LCD_Goto(unsigned char row, unsigned char col) {
    LCD_Cmd((unsigned char)(((row == 0) ? 0x80 : 0xC0) + col));
}

static void LCD_Print(const char *s) { while (*s) LCD_Data((unsigned char)*s++); }

static void LCD_Num(unsigned int n) {
    unsigned char buf[5], i = 0;
    if (n == 0) { LCD_Data('0'); return; }
    while (n > 0) { buf[i++] = (unsigned char)((n % 10) + '0'); n /= 10; }
    while (i--) LCD_Data(buf[i]);
}

static void LCD_Num2(unsigned char n) { LCD_Num(n / 10); LCD_Num(n % 10); }

static void LCD_NumFrac(unsigned int n, unsigned char den) {
    LCD_Num(n / den);
    LCD_Data('.');
    LCD_Num(n % den);
}

/* ========================================== */
/* DHT22 HUMIDITY DIGITAL SENSOR MODULE DRIVER */
/* ========================================== */
static unsigned char DHT22_Read(signed int *temp_x10, unsigned int *hum_x10) {
    unsigned char data[5] = {0};
    unsigned char i, j;
    unsigned int timeout;

    DHT22_TRIS = 0;
    DHT22_DATA = 0;
    __delay_ms(18);
    DHT22_DATA = 1;
    __delay_us(30);
    DHT22_TRIS = 1;

    timeout = 1000; while (DHT22_DATA && --timeout); if (!timeout) return 1;
    timeout = 1000; while (!DHT22_DATA && --timeout); if (!timeout) return 1;
    timeout = 1000; while (DHT22_DATA && --timeout); if (!timeout) return 1;

    for (i = 0; i < 5; i++) {
        data[i] = 0;
        for (j = 0; j < 8; j++) {
            timeout = 1000; while (!DHT22_DATA && --timeout); if (!timeout) return 1;
            __delay_us(35); 
            if (DHT22_DATA) {
                data[i] |= (1 << (7 - j));
                timeout = 1000; while (DHT22_DATA && --timeout); if (!timeout) return 1;
            }
        }
    }

    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) return 2;

    *hum_x10 = ((unsigned int)data[0] << 8) | data[1];
    signed int t = ((unsigned int)(data[2] & 0x7F) << 8) | data[3];
    if (data[2] & 0x80) t = -t;
    *temp_x10 = t;

    return 0;
}

/* ========================================== */
/* INTERNAL ADC & UART HARDWARE MODULE CHANNELS*/
/* ========================================== */
static void ADC_Init(void) {
    TRISAbits.TRISA1 = 1; TRISAbits.TRISA2 = 1;
    ADCON1 = 0b10000000; ADCON0 = 0b01000001; __delay_us(50);
}

static unsigned int ADC_Read(unsigned char ch) {
    ADCON0 = (unsigned char)((ADCON0 & 0b11000111) | ((ch & 0x07) << 3));
    __delay_us(50); ADCON0bits.GO_DONE = 1; while (ADCON0bits.GO_DONE) { }
    return (unsigned int)(((unsigned int)ADRESH << 8) | ADRESL);
}

static void UART_Init(void) {
    TRISCbits.TRISC6 = 1; TRISCbits.TRISC7 = 1;
    SPBRG = 129; TXSTA = 0b00100100; RCSTA = 0b10010000; 
}

static void UART_Write(char c) { while (!TXIF) { } TXREG = (unsigned char)c; }
static void UART_Print(const char *s) { while (*s) UART_Write(*s++); }

static void UART_Num(unsigned int n) {
    unsigned char buf[5], i = 0;
    if (n == 0) { UART_Write('0'); return; }
    while (n > 0) { buf[i++] = (unsigned char)((n % 10) + '0'); n /= 10; }
    while (i--) UART_Write(buf[i]);
}

static void UART_Num2(unsigned char n) { UART_Num(n / 10); UART_Num(n % 10); }

static void UART_NumFrac(unsigned int n, unsigned char den) {
    UART_Num(n / den); UART_Write('.'); UART_Num(n % den);
}

/* ========================================== */
/* MAIN FIRMWARE EXECUTION LOOP               */
/* ========================================== */
void main(void) {
    unsigned int lm35_raw, ldr_raw;
    unsigned int lm35_x10, ldr_pct;
    unsigned char hum_pct;
    unsigned char i, samples_to_show;
    signed int dht_temp_x10;
    unsigned int dht_hum_x10;
    unsigned char hr, min, sec, date, mon, yr, light, alert;
    signed int temp_x10;

    OPTION_REG = 0xFF;
    INTCON = 0x00;
    PIE1 = 0x00; PIR1 = 0x00;
    TRISE = 0x00;
    TRISD = 0x00;       
    PORTD = 0x00;
    ALERT_TRIS = 0;
    ALERT_LED = 0;

    I2C_Init();
    LCD_Init();
    ADC_Init();
    UART_Init();

    T1CON = 0x31;
    TMR1H = 0x0B; TMR1L = 0xDC;
    PIE1bits.TMR1IE = 1;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE  = 1;

    SRAM_LoadHeader();
    samples_to_show = (sram_count > 8) ? 8 : sram_count;

    LCD_Goto(0,0); LCD_Print("Smart Weather   ");
    LCD_Goto(1,0); LCD_Print("Station I2C v4  ");
    __delay_ms(2000); LCD_Cmd(0x01); __delay_ms(2);

    while (1) {
        if (take_sample_flag) {
            take_sample_flag = 0;

            // 1. Fetch Time (If clock is missing, it will timeout instead of crashing)
            DS3231_ReadTime();

            // 2. Sample Analog Environmental Data Channels
            lm35_raw = ADC_Read(1);
            ldr_raw  = ADC_Read(2);
            lm35_x10 = (unsigned int)(((unsigned long)lm35_raw * 5000UL) / 1024UL);
            ldr_pct  = (unsigned int)(((unsigned long)ldr_raw  * 100UL) / 1023UL);
            if (ldr_pct > 100) ldr_pct = 100;

            // 3. Process Digital Humidity
            GIE = 0; 
            unsigned char dht_err = DHT22_Read(&dht_temp_x10, &dht_hum_x10);
            GIE = 1;

            if (dht_err) {
                hum_pct = 0; 
            } else {
                hum_pct = (unsigned char)(dht_hum_x10 / 10);
                if (hum_pct > 100) hum_pct = 100;
            }

            // 4. Over-Temperature Threshold Evaluation
            ALERT_LED = (unsigned char)((lm35_x10 > TEMP_ALERT_X10) ? 1 : 0);

            // 5. Render Screen Dynamic Content Update Windows
            LCD_Goto(0, 0);
            LCD_Num2(rtc_hr);  LCD_Data(':'); LCD_Num2(rtc_min); LCD_Data(':'); LCD_Num2(rtc_sec); LCD_Data(' ');
            LCD_Data('T'); LCD_Data(':'); LCD_NumFrac(lm35_x10, 10); LCD_Data('C'); LCD_Print("  ");

            LCD_Goto(1, 0);
            LCD_Data('H'); LCD_Data(':'); LCD_Num(hum_pct); LCD_Data('%'); LCD_Print(" ");
            LCD_Data('L'); LCD_Data(':'); LCD_Num(ldr_pct); LCD_Data('%'); LCD_Print(" ");
            LCD_Data('A'); LCD_Data(':'); LCD_Num(ALERT_LED); LCD_Print("  ");

            // 6. Output Log Stream directly to Serial Monitor Out
            UART_Num2(rtc_hr); UART_Write(':'); UART_Num2(rtc_min); UART_Write(':'); UART_Num2(rtc_sec); UART_Write(',');
            UART_NumFrac(lm35_x10, 10); UART_Write(',');
            UART_Num(hum_pct); UART_Write(','); UART_Num(ldr_pct); UART_Write(','); UART_Num(ALERT_LED); UART_Print("\r\n");

            // 7. Write Data Entry Packets to Storage Bank
            SRAM_SaveSample(rtc_hr, rtc_min, rtc_sec,
                            rtc_date, rtc_mon, rtc_yr,
                            (signed int)lm35_x10, hum_pct, (unsigned char)ldr_pct, ALERT_LED);
        }
    }
}