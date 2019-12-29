/**
 * @file ws2812b_rgb_led.h
 * @brief WS2812B Addressable RGB LED driver module
 *
 * @author Jeremy Ruhland <jeremy ( a t ) goopypanther.org>
 *
 * @copyright Jeremy Ruhland 2019
 * @license GPL 3
 * @version 1.0
 * @since Oct 20, 2019
 */

#ifndef WS2812B_RGB_LED_WS2812B_RGB_LED_H_
#define WS2812B_RGB_LED_WS2812B_RGB_LED_H_

/**
 * WS2812B LEDs have 24 bit registers organized G[8]R[8]B[8]
 * We will enqueue 2 LEDs at a time for transmission with a 48 word register
 */
#define WS2812B_RGB_LED_BITS_PER_COLOR 8
#define WS2812B_RGB_LED_COLORS 3
#define WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN (WS2812B_RGB_LED_BITS_PER_COLOR * WS2812B_RGB_LED_COLORS)
#define WS2812B_RGB_LED_BIT_QUEUE_FRAMES 2
#define WS2812B_RGB_LED_BIT_QUEUE_LEN (WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN * WS2812B_RGB_LED_BIT_QUEUE_FRAMES)


/**
 * State control enum
 */
typedef enum ws2812bRgbLedState {
    WS2812BRGBLED_STATE_IDLE,   /*< PWM channel idle, ready to start */
    WS2812BRGBLED_STATE_ACTIVE, /*< PWM channel currently outputting waveform by DMA */
    WS2812BRGBLED_STATE_RESET   /*< PWM channel low for LED reset */
} ws2812bRgbLedState_t;

/**
 * LED pixel struct
 * Gamma correction is applied during enqueue process
 */
typedef struct ws2812bRgbLedPixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812bRgbLedPixel_t;

/**
 * LED string control structure
 * Required for each LED string
 * Initialized by ws2812bRgbLedInit()
 */
typedef struct ws2812bRgbLed {
    TIM_HandleTypeDef    *timHandle;                               /*< Pointer to timer handle */
    uint32_t              timChannel;                              /*< Timer PWM channel, see TIM_CHANNEL defgroup */
    ws2812bRgbLedPixel_t *frameArray;                              /*< Pointer to array of LED pixels */
    size_t                frameArrayLen;                           /*< Length of LED array */
    uint32_t              bitQueue[WS2812B_RGB_LED_BIT_QUEUE_LEN]; /*< PWM values for waveform generation */
    uint32_t              currentFrame;                            /*< Most recent frame loaded into bitQueue */
    ws2812bRgbLedState_t  state;                                   /*< Current state */
} ws2812bRgbLed_t;


extern int32_t ws2812bRgbLedInit(ws2812bRgbLed_t*, TIM_HandleTypeDef*, uint32_t, ws2812bRgbLedPixel_t*, size_t);
extern int32_t ws2812bRgbLedUpdate(ws2812bRgbLed_t*);
extern int32_t ws2812bRgbLedStatus(ws2812bRgbLed_t*);
extern int32_t ws2812bRgbLedAbort(ws2812bRgbLed_t*);
extern void ws2812bRgbLedIrq(DMA_HandleTypeDef*, ws2812bRgbLed_t*);

#endif /* WS2812B_RGB_LED_WS2812B_RGB_LED_H_ */
