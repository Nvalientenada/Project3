#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"


//Defining Pins
//LEDS
#define GREEN_LED GPIO_NUM_8
#define RED_LED GPIO_NUM_9
#define LOW_BEAMS GPIO_NUM_16
//BUTTONS
#define DRIVER_OCC GPIO_NUM_4
#define DRIVER_BELT GPIO_NUM_7
#define PASS_OCC GPIO_NUM_17
#define PASS_BELT GPIO_NUM_3
#define IGNITION GPIO_NUM_10
//BUZZER
#define BUZZER GPIO_NUM_14
//LIGHT SENSOR
#define LIGHT_ADC_CHAN ADC_CHANNEL_1
//POTENTIOMETER
#define POT_ADC_CHAN ADC_CHANNEL_0

//ADC
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define BITWIDTH        ADC_BITWIDTH_12

//DELAY
#define LOOP_DELAY_MS 25 // Loop delay
#define BUZZER_TIME 500 //time buzzer is on

void app_main(void) {
    //Configuring LEDS
    
    //Configuring GREEN_LED
    gpio_config_t green_led_io_conf = {
        .pin_bit_mask = (1ULL << GREEN_LED), 
        .mode = GPIO_MODE_OUTPUT,          
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring RED_LED
    gpio_config_t red_led_io_conf = {
        .pin_bit_mask = (1ULL << RED_LED), 
        .mode = GPIO_MODE_OUTPUT,          
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring LOW_BEAMS
    gpio_config_t low_beams_led_io_conf = {
        .pin_bit_mask = (1ULL << LOW_BEAMS), 
        .mode = GPIO_MODE_OUTPUT,          
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };

    //Configuring Buttons

    //Configuring DRIVER_OCC
    gpio_config_t driver_occ_io_conf = {
        .pin_bit_mask = (1ULL << DRIVER_OCC), 
        .mode = GPIO_MODE_INPUT,          
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring DRIVER_BELT
    gpio_config_t driver_belt_io_conf = {
        .pin_bit_mask = (1ULL << DRIVER_BELT), 
        .mode = GPIO_MODE_INPUT,          
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring PASS_OCC
    gpio_config_t pass_occ_io_conf = {
        .pin_bit_mask = (1ULL << PASS_OCC), 
        .mode = GPIO_MODE_INPUT,          
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring PASS_BELT
    gpio_config_t pass_belt_io_conf = {
        .pin_bit_mask = (1ULL << PASS_BELT), 
        .mode = GPIO_MODE_INPUT,          
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };
    //Configuring IGNITION
    gpio_config_t ignition_io_conf = {
        .pin_bit_mask = (1ULL << IGNITION), 
        .mode = GPIO_MODE_INPUT,          
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };

    //Configuring BUZZER
    gpio_config_t buzzer_io_conf = {
        .pin_bit_mask = (1ULL << BUZZER), 
        .mode = GPIO_MODE_OUTPUT,          
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE    
    };

    //ADC CONFIG
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };                                                  // Unit configuration
    adc_oneshot_unit_handle_t adc1_handle;              // Unit handle
    adc_oneshot_new_unit(&init_config1, &adc1_handle);  // Populate unit handle
   
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = BITWIDTH
    }; 
    
    adc_oneshot_config_channel                          // Configure the Potentiometer Channel
    (adc1_handle, POT_ADC_CHAN, &config);
    adc_oneshot_config_channel                          // Configure the Light Sensor Channel
    (adc1_handle, LIGHT_ADC_CHAN, &config);

    adc_cali_curve_fitting_config_t pot_cali_config = { //configuring curve fitting for the potentiometer
        .unit_id = ADC_UNIT_1,
        .chan = POT_ADC_CHAN,
        .atten = ADC_ATTEN,
        .bitwidth = BITWIDTH
    };

    adc_cali_curve_fitting_config_t light_cali_config = { //configuring curve fitting for the light sensor
        .unit_id = ADC_UNIT_1,
        .chan = LIGHT_ADC_CHAN,
        .atten = ADC_ATTEN,
        .bitwidth = BITWIDTH
    };
    adc_cali_handle_t adc1_cali_chan_handle;            // Calibration handle
    adc_cali_create_scheme_curve_fitting                // Populate cal handle
    (&light_cali_config, &adc1_cali_chan_handle);
    adc_cali_create_scheme_curve_fitting                // Populate cal handle
    (&pot_cali_config, &adc1_cali_chan_handle);

    //enabling configs and error checking
    ESP_ERROR_CHECK(gpio_config(&green_led_io_conf));
    ESP_ERROR_CHECK(gpio_config(&red_led_io_conf));
    ESP_ERROR_CHECK(gpio_config(&low_beams_led_io_conf));
    ESP_ERROR_CHECK(gpio_config(&driver_occ_io_conf));
    ESP_ERROR_CHECK(gpio_config(&driver_belt_io_conf));
    ESP_ERROR_CHECK(gpio_config(&pass_occ_io_conf));
    ESP_ERROR_CHECK(gpio_config(&pass_belt_io_conf));
    ESP_ERROR_CHECK(gpio_config(&ignition_io_conf));
    ESP_ERROR_CHECK(gpio_config(&buzzer_io_conf));


    //defining variables
    bool driver_occupied_prev = false;
    bool driver_occupied = false;
    bool driver_belt = false;
    bool pass_occupied = false;
    bool pass_belt = false;
    bool ignition_button_prev = false;
    bool ignition_button = false;
    bool ignition = false;
    bool can_start = false;
    int dusk_threshold = 200;
    int daylight_threshold = 500;
    int off_threshold = 3170/3;
    int auto_threshold = 2* (3170/3);
    bool lights_off;
    bool lights_auto = false;
    bool lights_on;
    int light_buffer = 0;


    while(true) {
        int light_bits;                                   // light sensor ADC reading (bits)
        adc_oneshot_read
        (adc1_handle, LIGHT_ADC_CHAN, &light_bits);          // Read ADC bits
       
        int light;                                     // ADC reading (mV)
        adc_cali_raw_to_voltage
        (adc1_cali_chan_handle, light_bits, &light);

        int pot_bits;                                   // ADC reading (bits)
        adc_oneshot_read
        (adc1_handle, POT_ADC_CHAN, &pot_bits);          // Read ADC bits
       
        int pot;                                     // ADC reading (mV)
        adc_cali_raw_to_voltage
        (adc1_cali_chan_handle, pot_bits, &pot);
        
        //update button states
        driver_occupied_prev = driver_occupied;
        driver_occupied = gpio_get_level(DRIVER_OCC) == 0;
        driver_belt = gpio_get_level(DRIVER_BELT) == 0;
        pass_occupied = gpio_get_level(PASS_OCC) == 0;
        pass_belt = gpio_get_level(PASS_BELT) == 0;
        ignition_button_prev = ignition_button;
        ignition_button = gpio_get_level(IGNITION) == 0;

        //update ADC states
        lights_off = off_threshold > pot && ignition;
        lights_auto = auto_threshold > pot && pot >= off_threshold && ignition;
        lights_on = pot >= auto_threshold && ignition;


        //upate can_start variable based on current button states
        can_start = driver_occupied && driver_belt && pass_occupied && pass_belt;

        //welcome message
        if (driver_occupied && !driver_occupied_prev) { //if driver seat is occupied
            printf("Welcome to enhanced alarm system model 218-W25\n"); //print welcome message
        }

        //green LED
        if (can_start && !ignition) { //if conditions to start the engine are met
            gpio_set_level(GREEN_LED, 1); //turn green LED on
        }
        else {      //if conditions to start the engine are not met
            gpio_set_level(GREEN_LED, 0); //turn green LED off
        }

        if (ignition_button && !ignition_button_prev) {
            if(can_start && !ignition) { //if engine is not on and ignition conditions are met
                gpio_set_level(GREEN_LED, 0); //switch LEDS and print ignition message
                gpio_set_level(RED_LED, 1);
                printf("Engine Started\n");
                ignition = true;
            }
            else if (ignition) { //if engine is on, turn engine off
                ignition = false;
                gpio_set_level(RED_LED, 0);

            }
            else {      //if the conditions for the engine starting are not met
                gpio_set_level(BUZZER, 1);      //activate the buzzer
                printf("Ignition Inhibited\n"); //print ignition failure message
                if (!driver_occupied) { 
                printf("Driver seat not occupied\n");
                }
                if (!driver_belt) {
                    printf("Driver seatbelt not fastened\n");
                }
                if (!pass_occupied) {
                    printf("Passenger seat not occupied\n");
                }
                if (!pass_belt) {
                    printf("Passenger seatbelt not fastened\n");
                }
                vTaskDelay(BUZZER_TIME / portTICK_PERIOD_MS);
                gpio_set_level(BUZZER, 0); 
            }
        }
       

        //LIGHTS

        //auto lights with delays
        

        //turn lights on if light selector is on
        if (!ignition) {
            gpio_set_level(LOW_BEAMS, 0);
        }
        else if (lights_off) {
            gpio_set_level(LOW_BEAMS, 0);
        }
        //turn lights off if light selector is off 
        else if (lights_on) {
            gpio_set_level(LOW_BEAMS, 1);
        }
        //if light selector is in auto mode
        else if (lights_auto) {
            if (light > dusk_threshold && light < daylight_threshold) {     //if between dusk and day threshold, reset buffer
                light_buffer = 0;
            }

            else if (light < dusk_threshold && light_buffer < 1000) {       //if under dusk threshold, wait 1 sec
                light_buffer += LOOP_DELAY_MS;
            }
            else if (light > daylight_threshold && light_buffer > -2000) {  //if over daylight threshold, wait 2 sec
                light_buffer -=LOOP_DELAY_MS;
            }

            if (light_buffer == 1000) {     //turn lights on if dark for 1 sec
                gpio_set_level(LOW_BEAMS, 1);
            }
            if (light_buffer == -2000) {        //turn lights off if bright for 2 sec
                gpio_set_level(LOW_BEAMS, 0);
            }
        }
        
        vTaskDelay(  LOOP_DELAY_MS / portTICK_PERIOD_MS); //loop delay to prevent bouncy inputs
    }
}