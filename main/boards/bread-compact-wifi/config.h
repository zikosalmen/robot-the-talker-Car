#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

// RC-Car Motor Configuration (HG7881 / L9110 Dual Channel)
// Motor A (pins 11/12) = DRIVE motor  (forward / backward)
// Motor B (pins 13/14) = STEER motor  (left / right)
#define ENABLE_ROBOT_MOTOR   1
#define MOTOR_DRIVE_A_PIN    GPIO_NUM_11   // Drive IN1
#define MOTOR_DRIVE_B_PIN    GPIO_NUM_12   // Drive IN2
#define MOTOR_STEER_A_PIN    GPIO_NUM_13   // Steer IN1 (right)
#define MOTOR_STEER_B_PIN    GPIO_NUM_14   // Steer IN2 (left)
#define MOTOR_DRIVE_INVERT   false
#define MOTOR_STEER_INVERT   false

// Battery monitoring via ADC voltage divider
// Connect battery+ → 100kΩ → GPIO10 → 47kΩ → GND
// Divider ratio: 47/(100+47) = 0.3197
#define ENABLE_BATTERY_MONITOR  1
#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_9   // GPIO10 on ESP32-S3
#define BATTERY_UPPER_RESISTOR  100000.0f       // 100 kΩ
#define BATTERY_LOWER_RESISTOR  47000.0f        // 47 kΩ

#endif // _BOARD_CONFIG_H_
