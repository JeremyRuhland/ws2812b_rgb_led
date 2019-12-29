WS2812B RGB LED Driver
======================

This simple SMT32 driver for WS2812 type RGB LEDs makes use of the DMA and timer peripherals to run LED strings at high speed with low processor overhead.


Example
-------

This driver is written assuming the firmware makes use of the STM32 HAL. CubeMX is not necessary but simplifies configuration.

You must configure a timer peripheral to produce a 1.25 microsecond PWM signal.

 * Clock source: internal
  * This driver assumes a 48MHz input clock which corresponds to an auto-reload register (TIMx_ARR) value of 59. If a different input clock frequency is used the WS2812B_RGB_LED_0_CODE_COUNT and WS2812B_RGB_LED_1_CODE_COUNT values must be changed to maintain a 30/60% PWM duty cycle within the WS2812 timing tolerance.
 * At least one channel set to PWM Generation
 * Output compare mode: PWM mode 1
 * DMA request: Memory to Peripheral
 * DMA mode: circular
 * Increment Address: Memory
 * Data width: word/word

Modify GPIO setting for PWM output pin.

 * GPIO Pull-up/Pull-down: Pull down

Include ws2812b_rgb_led.h in main.h or equivalent:

```
#include "ws2812b_rgb_led.h"
```

Instantiate string control structure and pixel array, in this example there are 3 LEDs:

```
ws2812bRgbLed_t leds;
ws2812bRgbLedPixel_t pixels[3];
```

Set up DMA interrupt handler. The string control structure and HAL DMA handler must be available to your interrupt handler, so if they are declared in a different file they must be global variables and declared as externs to your interrupt handler. In this example the PWM signal is produced by TIM2 CH1 which is connected to DMA1 channel 5, the string control structure and dma handler were declared as global variables in main.c and the interrupt handler is written in a separate c file. HAL is being used to clear the interrupt flag after the LED driver function returns:

```
extern DMA_HandleTypeDef hdma_tim2_ch1;
extern ws2812bRgbLed_t leds;

void DMA1_Channel5_IRQHandler(void) {
  ws2812bRgbLedIrq(&hdma_tim2_ch1, &leds);
  HAL_DMA_IRQHandler(&hdma_tim2_ch1);
}
```

Init GPIO, DMA and timers using HAL/CubeMX in main.c or elsewhere, in this example we are using TIM2 to generate the PWM signal and CubeMX init functions:

```
MX_GPIO_Init();
MX_DMA_Init();
MX_TIM2_Init();
```

Init the LED driver. Pass in pointers to the string control structure, HAL timer handler, output PWM channel, pixel array and pixel array length. In this example PWM channel 1 was configured to generate the PWM signal:

```
ws2812bRgbLedInit(&leds, &htim2, TIM_CHANNEL_1, pixels, 3);
```

Set pixel values to something and begin update:

```
pixels[0].r = 255;
pixels[0].g = 0;
pixels[0].b = 0;

pixels[1].r = 0;
pixels[1].g = 255;
pixels[1].b = 0;

pixels[2].r = 0;
pixels[2].g = 0;
pixels[2].b = 255;

ws2812bRgbLedUpdate(&leds);
```

Pixel string update will occur in the background through the DMA interrupt. Calls to ws2812bRgbLedUpdate() will return -1 if an update cycle is already in progress.