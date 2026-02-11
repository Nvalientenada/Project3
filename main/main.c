#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_rom_sys.h" 

// =================
// PIN DEFINITIONS 
// =================

// LEDs
#define GREEN_LED       GPIO_NUM_10     // "ignition enabled"
#define YELLOW_LED      GPIO_NUM_11     // "engine running"

// Buttons (active-low with pullups)
#define DRIVER_OCC      GPIO_NUM_5
#define DRIVER_BELT     GPIO_NUM_7

#define PASS_OCC        GPIO_NUM_4     
#define PASS_BELT       GPIO_NUM_6
#define IGNITION_BTN    GPIO_NUM_8

// Buzzer 
#define BUZZER          GPIO_NUM_17

// Servo signal 
#define SERVO_GPIO      GPIO_NUM_16

// ===================================
// LCD (EXERCISE 11 PIN ASSIGNMENTS)
// ===================================
#define LCD_RS          GPIO_NUM_38
#define LCD_E           GPIO_NUM_37
#define LCD_D4          GPIO_NUM_36
#define LCD_D5          GPIO_NUM_35
#define LCD_D6          GPIO_NUM_48
#define LCD_D7          GPIO_NUM_47
// LCD VDD = 3.3V, Backlight A = 3.3V, GND = GND, RW = GND (hardware)

// ====================
// LEDC SERVO CONFIG 
// ====================
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO   (SERVO_GPIO)
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY   (50)

// 13-bit max = 8191
#define SERVO_DUTY_0_DEG    (307)   // ~3.75%
#define SERVO_DUTY_90_DEG   (615)   // ~7.5% midpoint
#define SERVO_DUTY_180_DEG  (922)   // ~11.25% reference

// ====================
// TIMING / SPEED
// ====================
#define LOOP_MS   10
#define BUZZ_MS   500

#define LO_STEP   2
#define HI_STEP   5

// ===========
// WIPER MODE
// ===========
typedef enum { W_OFF, W_INT, W_LO, W_HI } wmode_t;

// ==============================================
// LCD LOW-LEVEL FUNCTIONS (HD44780, 4-bit mode)
// ==============================================

static void lcd_set_data_nibble(uint8_t nibble)
{
    gpio_set_level(LCD_D4, (nibble >> 0) & 1);
    gpio_set_level(LCD_D5, (nibble >> 1) & 1);
    gpio_set_level(LCD_D6, (nibble >> 2) & 1);
    gpio_set_level(LCD_D7, (nibble >> 3) & 1);
}

static void lcd_pulse_enable(void)
{
    gpio_set_level(LCD_E, 1);
    esp_rom_delay_us(1);
    gpio_set_level(LCD_E, 0);
    esp_rom_delay_us(50);
}

static void lcd_write4(uint8_t nibble)
{
    lcd_set_data_nibble(nibble);
    lcd_pulse_enable();
}

static void lcd_send(uint8_t value, int rs)
{
    gpio_set_level(LCD_RS, rs);
    lcd_write4((value >> 4) & 0x0F);
    lcd_write4(value & 0x0F);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_send(cmd, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_data(uint8_t ch)
{
    lcd_send(ch, 1);
}

static void lcd_clear(void)
{
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_set_cursor(int col, int row)
{
    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    addr += (uint8_t)col;
    lcd_cmd(0x80 | addr);
}

static void lcd_print(const char *s)
{
    while (*s) lcd_data((uint8_t)*s++);
}

static void lcd_gpio_init(void)
{
    gpio_config_t lcd_out = {
        .pin_bit_mask = (1ULL<<LCD_RS) | (1ULL<<LCD_E) |
                        (1ULL<<LCD_D4) | (1ULL<<LCD_D5) |
                        (1ULL<<LCD_D6) | (1ULL<<LCD_D7),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&lcd_out));

    gpio_set_level(LCD_RS, 0);
    gpio_set_level(LCD_E, 0);
}

static void lcd_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level(LCD_RS, 0);

    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_write4(0x02); // 4-bit mode

    lcd_cmd(0x28); // 4-bit, 2-line
    lcd_cmd(0x0C); // display on, cursor off
    lcd_cmd(0x06); // entry mode
    lcd_clear();
}

static void lcd_show_status(wmode_t mode, int delay_ms, bool engine_on)
{
    char line1[21];
    char line2[21];

    snprintf(line1, sizeof(line1), "Engine: %s", engine_on ? "ON " : "OFF");
    for (int i = (int)strlen(line1); i < 20; i++) line1[i] = ' ';
    line1[20] = '\0';

    if (!engine_on) {
        snprintf(line2, sizeof(line2), "Wiper: disabled");
    } else if (mode == W_OFF) {
        snprintf(line2, sizeof(line2), "Wiper: OFF");
    } else if (mode == W_HI) {
        snprintf(line2, sizeof(line2), "Wiper: HI");
    } else if (mode == W_LO) {
        snprintf(line2, sizeof(line2), "Wiper: LO");
    } else {
        const char *d = (delay_ms == 1000) ? "SHORT" :
                        (delay_ms == 3000) ? "MED" : "LONG";
        snprintf(line2, sizeof(line2), "Wiper: INT %s", d);
    }
    for (int i = (int)strlen(line2); i < 20; i++) line2[i] = ' ';
    line2[20] = '\0';

    lcd_set_cursor(0, 0);
    lcd_print(line1);

    lcd_set_cursor(0, 1);
    lcd_print(line2);
}

// ===========
// SERVO LEDC 
// ===========
static void servo_ledc_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void servo_set_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

// ============================================================
// ADC: single pot on GPIO15
// ============================================================

#define WIPER_ADC_UNIT      ADC_UNIT_2
#define WIPER_ADC_CHAN      ADC_CHANNEL_4

static wmode_t decode_mode_from_one_pot(int raw)
{
    if (raw < 1024) return W_OFF;
    if (raw < 2048) return W_INT;
    if (raw < 3072) return W_LO;
    return W_HI;
}

static int decode_delay_from_one_pot(int raw)
{
    if (raw < 1024 || raw >= 2048) return 3000;

    int within = raw - 1024;
    if (within < 341) return 1000;
    if (within < 682) return 3000;
    return 5000;
}

void app_main(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL<<GREEN_LED) | (1ULL<<YELLOW_LED) | (1ULL<<BUZZER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    gpio_config_t in_conf = {
        .pin_bit_mask =
            (1ULL<<DRIVER_OCC) | (1ULL<<DRIVER_BELT) |
            (1ULL<<PASS_OCC)   | (1ULL<<PASS_BELT)   |
            (1ULL<<IGNITION_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&in_conf));

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = WIPER_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, WIPER_ADC_CHAN, &adc_cfg));

    servo_ledc_init();
    int servo_duty = SERVO_DUTY_0_DEG;
    servo_set_duty(servo_duty);

    lcd_gpio_init();
    lcd_init();

    bool engine_on = false;
    bool driver_occ_prev = false;
    bool ignition_prev = false;

    int buzzer_timer = 0;
    int direction = +1;
    int pause_ms = 0;

    wmode_t last_mode = (wmode_t)99;
    int last_delay = -1;
    bool last_engine = false;

    lcd_show_status(W_OFF, 3000, engine_on);

    while (1)
    {
        bool driver_occ   = (gpio_get_level(DRIVER_OCC) == 0);
        bool driver_belt  = (gpio_get_level(DRIVER_BELT) == 0);
        bool pass_occ     = (gpio_get_level(PASS_OCC) == 0);
        bool pass_belt    = (gpio_get_level(PASS_BELT) == 0);
        bool ignition_btn = (gpio_get_level(IGNITION_BTN) == 0);

        if (driver_occ && !driver_occ_prev) {
            printf("Welcome to enhanced alarm system model 218-W26\n");
        }
        driver_occ_prev = driver_occ;

        bool can_start = driver_occ && driver_belt && pass_occ && pass_belt;

        gpio_set_level(GREEN_LED, (can_start && !engine_on) ? 1 : 0);

        if (ignition_btn && !ignition_prev) {
            if (!engine_on) {
                if (can_start) {
                    engine_on = true;
                    gpio_set_level(YELLOW_LED, 1);
                    printf("Engine started.\n");
                } else {
                    printf("Ignition inhibited\n");
                    gpio_set_level(BUZZER, 1);
                    buzzer_timer = BUZZ_MS;

                    if (!driver_occ)  printf("Driver seat not occupied\n");
                    if (!driver_belt) printf("Driver seatbelt not fastened\n");
                    if (!pass_occ)    printf("Passenger seat not occupied\n");
                    if (!pass_belt)   printf("Passenger seatbelt not fastened\n");
                }
            } else {
                engine_on = false;
                gpio_set_level(YELLOW_LED, 0);
                printf("Engine stopped.\n");
            }
        }
        ignition_prev = ignition_btn;

        if (buzzer_timer > 0) {
            buzzer_timer -= LOOP_MS;
            if (buzzer_timer <= 0) {
                buzzer_timer = 0;
                gpio_set_level(BUZZER, 0);
            }
        }

        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, WIPER_ADC_CHAN, &raw));

        wmode_t mode = decode_mode_from_one_pot(raw);
        int delay_ms = decode_delay_from_one_pot(raw);

        if (mode != last_mode || delay_ms != last_delay || engine_on != last_engine) {
            lcd_show_status(mode, delay_ms, engine_on);
            last_mode = mode;
            last_delay = delay_ms;
            last_engine = engine_on;
        }

        bool stop_request = (!engine_on) || (mode == W_OFF);

        if (stop_request && pause_ms > 0) {
            servo_duty = SERVO_DUTY_0_DEG;
            servo_set_duty(servo_duty);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        if (stop_request) {
            if (servo_duty > SERVO_DUTY_0_DEG) {
                servo_duty -= LO_STEP;
                if (servo_duty < SERVO_DUTY_0_DEG) servo_duty = SERVO_DUTY_0_DEG;
            }
            servo_set_duty(servo_duty);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        if (mode == W_INT && pause_ms > 0) {
            pause_ms -= LOOP_MS;
            if (pause_ms < 0) pause_ms = 0;

            servo_set_duty(SERVO_DUTY_0_DEG);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        int step = (mode == W_HI) ? HI_STEP : LO_STEP;
        servo_duty += direction * step;

        if (servo_duty >= SERVO_DUTY_90_DEG) {
            servo_duty = SERVO_DUTY_90_DEG;
            direction = -1;
        }

        if (servo_duty <= SERVO_DUTY_0_DEG) {
            servo_duty = SERVO_DUTY_0_DEG;
            direction = +1;

            if (mode == W_INT) pause_ms = delay_ms;
        }

        servo_set_duty(servo_duty);
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}
