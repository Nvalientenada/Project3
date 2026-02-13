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
// NOTE: These must match the hardware wiring on the ESP32-S3.
// Buttons are active-low (pressed = 0) because we use internal pull-ups.

// LEDs
#define GREEN_LED       GPIO_NUM_10     // "ignition enabled"
#define YELLOW_LED      GPIO_NUM_11     // "engine running"

// Buttons (active-low with pullups)
// When the button is pressed, the GPIO reads 0 (LOW).
// When released, the GPIO reads 1 (HIGH) due to pull-up.
#define DRIVER_OCC      GPIO_NUM_5
#define DRIVER_BELT     GPIO_NUM_7

#define PASS_OCC        GPIO_NUM_4     
#define PASS_BELT       GPIO_NUM_6
#define IGNITION_BTN    GPIO_NUM_8

// Buzzer 
// Buzzer is driven as a simple digital output (HIGH = ON).
#define BUZZER          GPIO_NUM_21

// Servo signal 
// Servo is controlled with PWM (LEDC peripheral) at ~50 Hz.
#define SERVO_GPIO      GPIO_NUM_16

// ===================================
// LCD (EXERCISE 11 PIN ASSIGNMENTS)
// ===================================
// LCD is the classic HD44780-type display in 4-bit mode.
// We use RS, E, and data lines D4-D7.
// LCD VDD = 3.3V, Backlight A = 3.3V, GND = GND, RW = GND (hardware)
#define LCD_RS          GPIO_NUM_38
#define LCD_E           GPIO_NUM_37
#define LCD_D4          GPIO_NUM_36
#define LCD_D5          GPIO_NUM_35
#define LCD_D6          GPIO_NUM_48
#define LCD_D7          GPIO_NUM_47

// ====================
// LEDC SERVO CONFIG 
// ====================
// LEDC is ESP32's hardware PWM generator.
// We set 50 Hz for standard servo control (20 ms period).
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO   (SERVO_GPIO)
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY   (50)

// 13-bit max = 8191
// These duty values correspond to pulse widths appropriate for servo angles.
// In this project we only sweep between 0 degrees and ~90 degrees to model wiper motion.
#define SERVO_DUTY_0_DEG    (307)   // ~3.75%
#define SERVO_DUTY_90_DEG   (615)   // ~7.5% midpoint
#define SERVO_DUTY_180_DEG  (922)   // ~11.25% reference

// ====================
// TIMING / SPEED
// ====================
// LOOP_MS controls how fast our main loop repeats.
// Smaller LOOP_MS = more responsive but more CPU usage.
// BUZZ_MS is how long the buzzer stays ON after an inhibited start attempt.
#define LOOP_MS   10
#define BUZZ_MS   500

// Servo sweep step sizes:
// LO_STEP is slower movement, HI_STEP is faster movement.
#define LO_STEP   2
#define HI_STEP   5

// ===========
// WIPER MODE
// ===========
// OFF: parked at 0 degrees
// INT: sweep once, then pause, then repeat
// LO: continuous sweep slow
// HI: continuous sweep faster
typedef enum { W_OFF, W_INT, W_LO, W_HI } wmode_t;

// ==============================================
// LCD LOW-LEVEL FUNCTIONS (HD44780, 4-bit mode)
// ==============================================
// These helper functions implement the "4-bit parallel" LCD interface.
// The LCD expects commands/data as 8-bit values, but we send them as two 4-bit nibbles.

static void lcd_set_data_nibble(uint8_t nibble)
{
    // Map bits 0..3 of nibble onto D4..D7
    gpio_set_level(LCD_D4, (nibble >> 0) & 1);
    gpio_set_level(LCD_D5, (nibble >> 1) & 1);
    gpio_set_level(LCD_D6, (nibble >> 2) & 1);
    gpio_set_level(LCD_D7, (nibble >> 3) & 1);
}

static void lcd_pulse_enable(void)
{
    // LCD "E" pin gets a short HIGH pulse to latch the data lines
    gpio_set_level(LCD_E, 1);
    esp_rom_delay_us(1);      // tiny pulse width
    gpio_set_level(LCD_E, 0);
    esp_rom_delay_us(50);     // allow LCD to settle
}

static void lcd_write4(uint8_t nibble)
{
    // Send one 4-bit nibble to the LCD
    lcd_set_data_nibble(nibble);
    lcd_pulse_enable();
}

static void lcd_send(uint8_t value, int rs)
{
    // rs=0 means command, rs=1 means character data
    gpio_set_level(LCD_RS, rs);

    // Send upper nibble then lower nibble
    lcd_write4((value >> 4) & 0x0F);
    lcd_write4(value & 0x0F);
}

static void lcd_cmd(uint8_t cmd)
{
    // Send a command byte to the LCD
    lcd_send(cmd, 0);
    vTaskDelay(pdMS_TO_TICKS(2)); // commands need time to execute
}

static void lcd_data(uint8_t ch)
{
    // Send a single character to the LCD
    lcd_send(ch, 1);
}

static void lcd_clear(void)
{
    // Clear display (slow command)
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_set_cursor(int col, int row)
{
    // Row 0 starts at address 0x00, row 1 starts at address 0x40 on most 16x2 LCDs.
    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    addr += (uint8_t)col;
    lcd_cmd(0x80 | addr);
}

static void lcd_print(const char *s)
{
    // Print a C-string to LCD at the current cursor position
    while (*s) lcd_data((uint8_t)*s++);
}

static void lcd_gpio_init(void)
{
    // Configure LCD pins as outputs
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

    // Default state
    gpio_set_level(LCD_RS, 0);
    gpio_set_level(LCD_E, 0);
}

static void lcd_init(void)
{
    // HD44780 initialization sequence for 4-bit mode.
    // Must wait after power-up before sending commands.
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level(LCD_RS, 0);

    // Force LCD into 8-bit mode first (3 times), then switch to 4-bit mode.
    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4(0x03);
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_write4(0x02); // 4-bit mode

    // Function set: 4-bit, 2-line, 5x8 font
    lcd_cmd(0x28);
    // Display on, cursor off, blink off
    lcd_cmd(0x0C);
    // Entry mode: move cursor right
    lcd_cmd(0x06);

    lcd_clear();
}

static void lcd_show_status(wmode_t mode, int delay_ms, bool engine_on)
{
    // Builds two 20-character strings (padded with spaces) to overwrite previous text cleanly
    char line1[21];
    char line2[21];

    // Line 1: Engine status
    snprintf(line1, sizeof(line1), "Engine: %s", engine_on ? "ON " : "OFF");
    for (int i = (int)strlen(line1); i < 20; i++) line1[i] = ' ';
    line1[20] = '\0';

    // Line 2: Wiper status
    if (!engine_on) {
        snprintf(line2, sizeof(line2), "Wiper: disabled");
    } else if (mode == W_OFF) {
        snprintf(line2, sizeof(line2), "Wiper: OFF");
    } else if (mode == W_HI) {
        snprintf(line2, sizeof(line2), "Wiper: HI");
    } else if (mode == W_LO) {
        snprintf(line2, sizeof(line2), "Wiper: LO");
    } else {
        // Intermittent mode: show which delay region is selected
        const char *d = (delay_ms == 1000) ? "SHORT" :
                        (delay_ms == 3000) ? "MED" : "LONG";
        snprintf(line2, sizeof(line2), "Wiper: INT %s", d);
    }
    for (int i = (int)strlen(line2); i < 20; i++) line2[i] = ' ';
    line2[20] = '\0';

    // Write both lines to LCD
    lcd_set_cursor(0, 0);
    lcd_print(line1);

    lcd_set_cursor(0, 1);
    lcd_print(line2);
}

// ===========
// SERVO LEDC 
// ===========
// Servo is controlled by PWM duty values.
// We update duty gradually to create smooth motion.

static void servo_ledc_init(void)
{
    // Configure the LEDC timer for 50 Hz (servo control)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configure LEDC channel to output PWM on SERVO_GPIO
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
    // Apply a new PWM duty cycle to the servo
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

// ============================================================
// ADC: single pot on GPIO15
// ============================================================
// The potentiometer provides an analog value (0..4095).
// We split that range into four regions for OFF/INT/LO/HI.
// Inside INT we further split into SHORT/MED/LONG delays.

#define WIPER_ADC_UNIT      ADC_UNIT_2
#define WIPER_ADC_CHAN      ADC_CHANNEL_4

static wmode_t decode_mode_from_one_pot(int raw)
{
    // Map ADC reading to wiper mode
    if (raw < 1024) return W_OFF;   // ~0-25% of pot rotation
    if (raw < 2048) return W_INT;   // ~25-50%
    if (raw < 3072) return W_LO;    // ~50-75%
    return W_HI;                    // ~75-100%
}

static int decode_delay_from_one_pot(int raw)
{
    // Only meaningful when in intermittent range.
    // Return a default if not intermittent (doesn't matter).
    if (raw < 1024 || raw >= 2048) return 3000;

    // How far into the intermittent region are we?
    int within = raw - 1024; // 0..1023
    if (within < 341) return 1000;  // SHORT delay
    if (within < 682) return 3000;  // MED delay
    return 5000;                    // LONG delay
}

void app_main(void)
{
    // -------------------------
    // GPIO CONFIGURATION
    // -------------------------

    // Configure outputs: LEDs + buzzer
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL<<GREEN_LED) | (1ULL<<YELLOW_LED) | (1ULL<<BUZZER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    // Configure inputs: buttons (active-low)
    // Internal pullups keep inputs HIGH when buttons are not pressed.
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

    // -------------------------
    // ADC CONFIGURATION
    // -------------------------
    // Configure ADC oneshot reading for the potentiometer.
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = WIPER_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_12,           // allows higher voltage range
        .bitwidth = ADC_BITWIDTH_12         // 12-bit resolution (0..4095)
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, WIPER_ADC_CHAN, &adc_cfg));

    // -------------------------
    // SERVO + LCD INITIALIZATION
    // -------------------------
    servo_ledc_init();
    int servo_duty = SERVO_DUTY_0_DEG;      // start "parked"
    servo_set_duty(servo_duty);

    lcd_gpio_init();
    lcd_init();

    // -------------------------
    // STATE VARIABLES
    // -------------------------
    bool engine_on = false;                 // engine state (OFF at start)

    // previous button states used to detect rising edge (press events)
    bool driver_occ_prev = false;
    bool ignition_prev = false;

    // buzzer_timer counts down to turn buzzer off automatically after BUZZ_MS
    int buzzer_timer = 0;

    // Servo sweep direction and intermittent pause timer
    int direction = +1;                     // +1 = sweeping up, -1 = sweeping down
    int pause_ms = 0;                       // pause time remaining in intermittent mode

    // Keep last displayed LCD state to avoid re-writing the LCD every loop
    wmode_t last_mode = (wmode_t)99;
    int last_delay = -1;
    bool last_engine = false;

    // Show initial status
    lcd_show_status(W_OFF, 3000, engine_on);

    // -------------------------
    // MAIN LOOP
    // -------------------------
    // Loop repeats every LOOP_MS and updates:
    // 1) ignition logic + buzzer
    // 2) ADC read -> wiper mode
    // 3) LCD status
    // 4) servo movement
    while (1)
    {
        // -------------------------
        // READ BUTTONS (active-low)
        // -------------------------
        bool driver_occ   = (gpio_get_level(DRIVER_OCC) == 0);
        bool driver_belt  = (gpio_get_level(DRIVER_BELT) == 0);
        bool pass_occ     = (gpio_get_level(PASS_OCC) == 0);
        bool pass_belt    = (gpio_get_level(PASS_BELT) == 0);
        bool ignition_btn = (gpio_get_level(IGNITION_BTN) == 0);

        // Welcome message when driver becomes occupied (rising edge of driver_occ)
        if (driver_occ && !driver_occ_prev) {
            printf("Welcome to enhanced alarm system model 218-W26\n");
        }
        driver_occ_prev = driver_occ;

        // Engine can start only if all safety conditions are met
        bool can_start = driver_occ && driver_belt && pass_occ && pass_belt;

        // Green LED indicates "ready to start" only when engine is currently off
        gpio_set_level(GREEN_LED, (can_start && !engine_on) ? 1 : 0);

        // -------------------------
        // IGNITION BUTTON LOGIC
        // -------------------------
        // Detect a press event using a rising edge (pressed now, not pressed last loop)
        if (ignition_btn && !ignition_prev) {
            if (!engine_on) {
                // Attempt to start engine
                if (can_start) {
                    engine_on = true;
                    gpio_set_level(YELLOW_LED, 1);
                    printf("Engine started.\n");
                } else {
                    // Inhibited start attempt: buzzer + messages
                    printf("Ignition inhibited\n");
                    gpio_set_level(BUZZER, 1);
                    buzzer_timer = BUZZ_MS;

                    if (!driver_occ)  printf("Driver seat not occupied\n");
                    if (!driver_belt) printf("Driver seatbelt not fastened\n");
                    if (!pass_occ)    printf("Passenger seat not occupied\n");
                    if (!pass_belt)   printf("Passenger seatbelt not fastened\n");
                }
            } else {
                // Turn engine off
                engine_on = false;
                gpio_set_level(YELLOW_LED, 0);
                printf("Engine stopped.\n");
            }
        }
        ignition_prev = ignition_btn;

        // -------------------------
        // BUZZER AUTO-OFF TIMER
        // -------------------------
        // Once buzzer_timer expires, buzzer turns off automatically.
        if (buzzer_timer > 0) {
            buzzer_timer -= LOOP_MS;
            if (buzzer_timer <= 0) {
                buzzer_timer = 0;
                gpio_set_level(BUZZER, 0);
            }
        }

        // -------------------------
        // READ POTENTIOMETER (ADC)
        // -------------------------
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, WIPER_ADC_CHAN, &raw));

        // Decode mode + intermittent delay from ADC raw value
        wmode_t mode = decode_mode_from_one_pot(raw);
        int delay_ms = decode_delay_from_one_pot(raw);

        // -------------------------
        // LCD UPDATE (only on change)
        // -------------------------
        if (mode != last_mode || delay_ms != last_delay || engine_on != last_engine) {
            lcd_show_status(mode, delay_ms, engine_on);
            last_mode = mode;
            last_delay = delay_ms;
            last_engine = engine_on;
        }

        // -------------------------
        // WIPER / SERVO LOGIC
        // -------------------------
        // If engine is off OR mode is OFF, wipers must park at 0 degrees.
        bool stop_request = (!engine_on) || (mode == W_OFF);

        // If we are currently pausing in intermittent mode and a stop is requested,
        // keep parked at 0 degrees 
        if (stop_request && pause_ms > 0) {
            servo_duty = SERVO_DUTY_0_DEG;
            servo_set_duty(servo_duty);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // If stop requested, ramp the servo back down to 0 degrees smoothly.
        if (stop_request) {
            if (servo_duty > SERVO_DUTY_0_DEG) {
                servo_duty -= LO_STEP;
                if (servo_duty < SERVO_DUTY_0_DEG) servo_duty = SERVO_DUTY_0_DEG;
            }
            servo_set_duty(servo_duty);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // Intermittent pause behavior:
        // When pause_ms > 0, hold servo at parked position and count down time.
        if (mode == W_INT && pause_ms > 0) {
            pause_ms -= LOOP_MS;
            if (pause_ms < 0) pause_ms = 0;

            servo_set_duty(SERVO_DUTY_0_DEG);
            vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
            continue;
        }

        // Continuous sweep: choose step size based on LO vs HI.
        int step = (mode == W_HI) ? HI_STEP : LO_STEP;

        // Move servo duty in current sweep direction
        servo_duty += direction * step;

        // Upper bound: 90 degrees (we don't go to 180 for the wiper model)
        if (servo_duty >= SERVO_DUTY_90_DEG) {
            servo_duty = SERVO_DUTY_90_DEG;
            direction = -1; // reverse direction
        }

        // Lower bound: 0 degrees (parked position)
        if (servo_duty <= SERVO_DUTY_0_DEG) {
            servo_duty = SERVO_DUTY_0_DEG;
            direction = +1; // reverse direction

            // If intermittent, start the pause once we return to parked position
            if (mode == W_INT) pause_ms = delay_ms;
        }

        // Apply servo movement
        servo_set_duty(servo_duty);

        // Loop delay controls overall responsiveness and sweep speed
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}
