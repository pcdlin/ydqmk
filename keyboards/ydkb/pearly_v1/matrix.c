#include <stdint.h>
#include <stdbool.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/wdt.h>
#include "avr_config.h"
#include "print.h"
#include "debug.h"
#include "util.h"
#include "action.h"
#include "command.h"
#include "keyboard.h"
#include "timer.h"
#include "matrix.h"
#include "suspend.h"
#include "lufa.h"
#include "rgblight.h"
#include "ble51.h"
#include "ble51_task.h"
#include "backlight.h"

#define DEBOUNCE_DN_MASK (uint8_t)(~(0x80 >> 5))
#define DEBOUNCE_UP_MASK (uint8_t)(0x80 >> 5)

bool is_ble_version = 1;

extern rgblight_config_t rgblight_config;
extern backlight_config_t backlight_config;

static matrix_row_t matrix[MATRIX_ROWS] = {0};


static uint8_t matrix_current_row = 0;
static uint16_t matrix_scan_timestamp = 0;
static uint8_t matrix_debouncing[MATRIX_ROWS][MATRIX_COLS] = {0};
static uint8_t encoder_state_prev[2][2] = {0};

static void init_cols(void);
static void select_row(uint8_t row);
static uint8_t get_key(uint8_t col);

static void init_cols(void);
__attribute__ ((weak))
void matrix_scan_user(void) {}

__attribute__ ((weak))
void matrix_scan_kb(void) {
    matrix_scan_user();
    hook_keyboard_loop();
}

inline
uint8_t matrix_rows(void)
{
    return MATRIX_ROWS;
}

inline
uint8_t matrix_cols(void)
{
    return MATRIX_COLS;
}

void hook_early_init()
{
    if (pgm_read_byte(0x7e72) == 0x4c) {
        // USB Only version
        is_ble_version = 0;
        ble51_boot_on = 0;
    } else {
        // PD1 for BLE Reset, PB3 for BT_SW
        DDRD  &= ~(1<<1);
        PORTD |=  (1<<1);
        DDRB  &= ~(1<<3);
        PORTB |=  (1<<3);
        _delay_ms(2);
        if ((~PINB & (1<<3))) ble51_boot_on = 0;
        //BLE Reset
        if (ble_reset_key == 0xBBAA) {
            if (ble51_boot_on) {
                // PE6 for BLE Reset
                DDRD  |=  (1<<1);
                PORTD &= ~(1<<1);
                bt_power_init();
                // light CapsLED
                DDRB  |= (1<<2);
                PORTB |= (1<<2);
                _delay_ms(5000);
                bootloader_jump(); 
            }
        }
    }
}

void matrix_init(void)
{
    DDRB  |=  (1<<2 | 1<<1);
    PORTB &= ~(1<<2 | 1<<1);
    init_cols();
    rgblight_init();
}

uint8_t matrix_scan(void)
{
    
    uint16_t time_check = timer_read();
    if (matrix_scan_timestamp == time_check) return 1;
    matrix_scan_timestamp = time_check;

    uint8_t *debounce = &matrix_debouncing[0][0];
    for (uint8_t row=0; row<matrix_rows(); row++) {
        select_row(row);
        matrix_current_row = row;
        _delay_us(6);
        for (uint8_t col=0; col<matrix_cols(); col++, *debounce++) {
            uint8_t key = get_key(col);
            *debounce = (*debounce >> 1) | key;
            
            if (BLE51_PowerState >= 2 && BLE51_PowerState < 10 && key == 0x80) return 100;
            //if ((*debounce > 0) && (*debounce < 255)) {
            if (1) {
                matrix_row_t *p_row = &matrix[row];
                matrix_row_t col_mask = ((matrix_row_t)1 << col);
                if        (*debounce >= DEBOUNCE_DN_MASK) {
                    *p_row |=  col_mask;
                } else if (*debounce <= DEBOUNCE_UP_MASK) {
                    *p_row &= ~col_mask;
                }
            } 
        }
        if (matrix[row] > 0) {
            if (!rgblight_config.enable && !backlight_config.enable) kb_idle_times = 12; 
            else kb_idle_times = 0;
        }
        //unselect_rows();
    }
    
    matrix_scan_quantum();
    return 1;
}


inline
bool matrix_is_on(uint8_t row, uint8_t col)
{
    return (matrix[row] & ((matrix_row_t)1<<col));
}

inline
matrix_row_t matrix_get_row(uint8_t row)
{
    return matrix[row];
}

void matrix_print(void)
{
    print("\nr/c 01234567\n");
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        print_hex8(row); print(": ");
        print_bin_reverse8(matrix_get_row(row));
        print("\n");
    }
}

uint8_t matrix_key_count(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        count += bitpop16(matrix[i]);
    }
    return count;
}

static const struct AVR_PINS col_PIN[] = { PD(6), PD(7), PB(4), PB(5), PB(6), PC(6), PC(7), PE(2), PF(7), PF(6), PB(0), PF(5) };
static const struct AVR_PINS row_PIN[] = {PF(4), PF(1), PF(0), PE(6)};

/* Column pin configuration
 *  col: 0  1  2  3  4  5  6  7  8  9  10  
 *  pin: D6 D7 B4 B5 B6 C6 C7 E2 F7 F6 F5 
 */
static void  init_cols(void)
{
    for (uint8_t col=0; col<matrix_cols(); col++) {
        _SFR_IO8(col_PIN[col].pin + 1) &= ~col_PIN[col].mask;
        _SFR_IO8(col_PIN[col].pin + 2) |=  col_PIN[col].mask;
    }
}
/* Column pin configuration
 *  col: 0  1  2  3  4  5  6  7  8  9  10  
 *  pin: D6 D7 B4 B5 B6 C6 C7 E2 F7 F6 F5 
 */
 
static uint8_t get_col_f(uint8_t col) {
    return (_SFR_IO8(col_PIN[col].pin) & col_PIN[col].mask) ? 0:0x80;
}

static uint8_t get_key(uint8_t col)
{
    uint8_t value = get_col_f(col);
    if (matrix_current_row == 3 && (col == 0 || col == 4 || col == 6 || col == 11)) {
        static uint8_t encoder_debounce = 0;
        uint8_t encoder_state_new = 0;
        static uint16_t encoder_idle_timer;
        uint8_t direction = (col == 0 || col == 6)? 1 : 0;

        if (timer_elapsed(encoder_idle_timer) > 400) encoder_debounce = direction?0b111:0;
        encoder_state_new = value? 1 : 0;

        uint8_t num = (col < 5)? 0:1;
        if (encoder_state_new != encoder_state_prev[num][direction]) { 
            encoder_state_prev[num][direction] = encoder_state_new;
            if (encoder_state_new == 0) { 
                if (encoder_state_prev[num][direction?0:1] == 0) {
                    encoder_debounce = (encoder_debounce<<1) + direction;
                    if ((encoder_debounce & 0b111) == (direction?0b111:0)) {
                        encoder_idle_timer = timer_read();
                        return DEBOUNCE_DN_MASK;
                    }
                } 
            } 
        }
        return 0;
    }
    return value; 
}


/* Row pin configuration
 * row: 0   1   2   3   
 * pin: F4  F1  F0  E6  
 */
void unselect_rows(void)
{
}

static void select_row(uint8_t row)
{
    for (uint8_t i=0; i<matrix_rows(); i++) {
        if (row == i) _SFR_IO8(row_PIN[i].pin + 1) |= row_PIN[i].mask;
        else _SFR_IO8(row_PIN[i].pin+1) &= ~row_PIN[i].mask;
    }
}

bool suspend_wakeup_condition(void)
{
    if (BLE51_PowerState >= 10) {
        matrix_scan();
        // K14 F, K17 J
        uint8_t *debounce = &matrix_debouncing[0][0];
        uint8_t matrix_keys_down = 0;
        for (uint8_t i=0; i< MATRIX_ROWS * MATRIX_COLS; i++, *debounce++) {
            if (*debounce > 0) {
                if (i == KP(1,4) || i == KP(1,7)) matrix_keys_down += 100;
                else matrix_keys_down++;
            }
        }
        if (matrix_keys_down == 200) {
            return true;
        } else if (!ble51_boot_on && matrix_keys_down) return true;
    } else {
        //check encoder
        select_row(3);
        _delay_us(6);
        uint8_t encoder_state_check[2][2];
        encoder_state_check[0][1] = PIND&(1<<PD6) ? 0 : 1;
        encoder_state_check[0][0] = PINB&(1<<PB6) ? 0 : 1;
        encoder_state_check[1][1] = PINC&(1<<PC7) ? 0 : 1;
        encoder_state_check[1][0] = PINF&(1<<PF5) ? 0 : 1;
        if (memcmp(&encoder_state_check[0][0], &encoder_state_prev[0][0], 4) != 0 || matrix_scan() == 100) {
            return true;
        }
    }
    return false;
}
