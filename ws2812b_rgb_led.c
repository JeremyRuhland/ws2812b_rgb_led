/**
 * @file ws2812b_rgb_led.c
 * @brief WS2812B Addressable RGB LED driver module
 *
 * @author Jeremy Ruhland <jeremy ( a t ) goopypanther.org>
 *
 * @copyright Jeremy Ruhland 2019
 * @license GPL 3
 * @version 1.0
 * @since Oct 20, 2019
 *
 *
 */

#include "main.h"
#include "ws2812b_rgb_led.h"


// Defines

/**
 * The timer clock runs at 48MHz, the tick period is 21ns.
 * Each bit in the data waveform must be 1.25us and the LEDs are enabled by a
 * 50us low period. This is 40 cycles of the data waveform, which rounds to 2
 * frames of the bitqueue.
 */
#define WS2812B_RGB_LED_PWM_PERIOD_NS 1250
#define WS2812B_RGB_LED_RESET_PERIOD_NS 50000
#define WS2812B_RGB_LED_RESET_CYCLES 2

/**
 * Clock calculations @ 48MHz, 21ns/tick
 * 0 code: 400ns ~ 19 ticks (399ns)
 * 1 code: 800ns ~ 38 ticks (798ns)
 */
#define WS2812B_RGB_LED_0_CODE_COUNT 19
#define WS2812B_RGB_LED_1_CODE_COUNT 38


// Function prototypes

int32_t ws2812bRgbLedInit(ws2812bRgbLed_t*, TIM_HandleTypeDef*, uint32_t, ws2812bRgbLedPixel_t*, size_t);
int32_t ws2812bRgbLedUpdate(ws2812bRgbLed_t*);
int32_t ws2812bRgbLedStatus(ws2812bRgbLed_t*);
int32_t ws2812bRgbLedAbort(ws2812bRgbLed_t*);
void ws2812bRgbLedIrq(DMA_HandleTypeDef*, ws2812bRgbLed_t*);

static void ws2812bRgbLedEnqueueFrame(uint32_t*, const ws2812bRgbLedPixel_t*);
static void ws2812bRgbLedEnqueueReset(uint32_t*);


// Static variables


/**
 * Init LED data struct
 * Call this function after declaring all necessary variables and
 *
 * @param rgbString     Pointer to data struct representing one string of WS2812 RGB LEDs
 *
 * @param timHandle     Pointer to timer handle controlling LED string's PWM channel
 *
 * @param timChannel    Output PWM channel of timer, options: TIM_CHANNEL_1,
 *                                                            TIM_CHANNEL_2,
 *                                                            TIM_CHANNEL_3,
 *                                                            TIM_CHANNEL_4
 *
 * @param frameArray    Array of pixels, element 0 is first LED in string
 *
 * @param frameArrayLen Length of LED string
 *
 * @return 0 -- Init is complete
 */
int32_t ws2812bRgbLedInit(ws2812bRgbLed_t *rgbString, TIM_HandleTypeDef *timHandle, uint32_t timChannel,
                          ws2812bRgbLedPixel_t *frameArray, size_t frameArrayLen) {

    uint32_t i;
    const ws2812bRgbLedPixel_t pix = {.r = 0, .g = 0, .b = 0};

    rgbString->timHandle = timHandle;
    rgbString->timChannel = timChannel;
    rgbString->frameArray = frameArray;
    rgbString->frameArrayLen = frameArrayLen;
    rgbString->state = WS2812BRGBLED_STATE_IDLE;

    // Clear all pixels
    for (i = 0; i < frameArrayLen; i++) {
        frameArray[i] = pix;
    }

    return (0);
}


/**
 * Start DMA transfer to LED string
 *
 * Calling this function will begin the asynchronous process to control the LED
 * string. Current status can be checked by calling ws2812bRgbLedStatus().
 * At any time the current transmission can be canceled with
 * ws2812bRgbLedAbort(). This will not necessarily result in the string
 * partially illuminating unless the PWM line happens to idle low for at least
 * 50 microseconds.
 *
 * @param rgbString Pointer to data struct representing one string of WS2812 RGB LEDs
 *
 * @return 0 -- Update has begun
 *        -1 -- Update aborted, DMA is already busy
 *
 */
int32_t ws2812bRgbLedUpdate(ws2812bRgbLed_t *rgbString) {

    // Exit immediately if DMA is currently running
    if (rgbString->state == WS2812BRGBLED_STATE_IDLE) {

        // In case of 1 LED array load LED frame and first frame of reset period, immediately go into reset state
        if (rgbString->frameArrayLen == 1) {
            rgbString->state = WS2812BRGBLED_STATE_RESET;
            rgbString->currentFrame = 0;

            // Enqueue LED and reset values into bitqueue
            ws2812bRgbLedEnqueueFrame(rgbString->bitQueue, rgbString->frameArray);
            ws2812bRgbLedEnqueueReset(&(rgbString->bitQueue[WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN]));

        // LED string long enough to fill bitqueue
        } else {
            rgbString->state = WS2812BRGBLED_STATE_ACTIVE;
            rgbString->currentFrame = 1;

            // Enqueue LEDs into both frames of bitqueue
            ws2812bRgbLedEnqueueFrame(rgbString->bitQueue, rgbString->frameArray);
            ws2812bRgbLedEnqueueFrame(&(rgbString->bitQueue[WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN]), &(rgbString->frameArray[1]));

        }
        // Begin DMA transfer, further control handled by ws2812bRgbLedIrq
        HAL_TIM_PWM_Start_DMA(rgbString->timHandle,
                              rgbString->timChannel,
                              rgbString->bitQueue,
                              WS2812B_RGB_LED_BIT_QUEUE_LEN);
    } else {
        return (-1); // Exit with error if DMA already busy
    }

    return (0);
}


/**
 * Check status of LED string
 *
 * @param rgbString Pointer to data struct representing one string of WS2812 RGB LEDs
 *
 * @return 0 -- LED string idle
 *       < 0 -- Approx microseconds until DMA transfer complete, count will
 *              floor at -1 until string becomes idle
 */
int32_t ws2812bRgbLedStatus(ws2812bRgbLed_t *rgbString) {
    int32_t remainingNs;

    switch (rgbString->state) {
    case WS2812BRGBLED_STATE_ACTIVE:

        // Number of bits remaining
        remainingNs  = (rgbString->currentFrame - rgbString->frameArrayLen + 1); // Number of remaining frames
        remainingNs *= WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN; // Number of remaining bits
        remainingNs *= WS2812B_RGB_LED_PWM_PERIOD_NS; // Number of remaining nanoseconds
        remainingNs -= WS2812B_RGB_LED_RESET_PERIOD_NS; // Include reset time

        return (remainingNs / 1000);
        break;

    case WS2812BRGBLED_STATE_RESET:
        remainingNs  = (rgbString->currentFrame - rgbString->frameArrayLen + 1); // Number of remaining frames
        remainingNs *= WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN; // Number of remaining bits
        remainingNs *= WS2812B_RGB_LED_PWM_PERIOD_NS; // Number of remaining nanoseconds

        // Wait until string goes idle
        if (remainingNs == 0) {
            return (-1);
        } else {
            return (remainingNs / 1000);
        }

        break;

    case WS2812BRGBLED_STATE_IDLE:
        return (0);
        break;
    }

    return (0);
}


/**
 * Abort a currently running DMA transfer to an LED string
 *
 * @param rgbString Pointer to data struct representing one string of WS2812 RGB LEDs
 *
 * @return 0 -- DMA transmit aborted
 *        -1 -- DMA was not running
 */
int32_t ws2812bRgbLedAbort(ws2812bRgbLed_t *rgbString) {

    if (rgbString->state != WS2812BRGBLED_STATE_IDLE) {
        // Stop DMA transfer
        HAL_TIM_PWM_Stop_DMA(rgbString->timHandle, rgbString->timChannel);

        rgbString->state = WS2812BRGBLED_STATE_IDLE;

        return (0);

    // DMA transfer was not running
    } else {
        return (-1);
    }
}


/**
 * The DMA interrupt routine associated with the timer controlling the RGB LED
 * string PWM line must call this interrupt handler to handle state control and
 * enqueue new pixel data into the bitqueue. This function must be passed both
 * the dmaHandle so that the source of the interrupt may be determined and the
 * rgbString struct pointer.
 *
 * After returning from this function the DMA interrupt routine must clear the
 * associated interrupt flags. HAL_DMA_IRQHandler does this for you if you
 * are using HAL/CubeMX. ws2812bRgbLedIrq must be called before HAL clears the
 * interrupt flags, not after.
 *
 * @param dmaHandle Handle for DMA associated with calling interrupt routine
 *
 * @param rgbString Pointer to data struct representing one string of WS2812 RGB LEDs
 */
void ws2812bRgbLedIrq(DMA_HandleTypeDef *dmaHandle, ws2812bRgbLed_t *rgbString) {
    uint32_t bitQueueFrame;
    uint32_t interruptFlags;

    // Identify which frame of bitqueue requires new data
    interruptFlags = dmaHandle->DmaBaseAddress->ISR;

    // Half transfer complete interrupt implies 0th frame
    if (READ_BIT(interruptFlags, (DMA_FLAG_HT1 << dmaHandle->ChannelIndex))) {
        bitQueueFrame = 0;

    // Transfer complete interrupt implies 1st frame
    } else if (READ_BIT(interruptFlags, (DMA_FLAG_TC1 << dmaHandle->ChannelIndex))) {
        bitQueueFrame = WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN;

    } else {
        // Error handling, immediately abort
        bitQueueFrame = 0;
        rgbString->state = WS2812BRGBLED_STATE_IDLE;
    }

    switch (rgbString->state) {
    case WS2812BRGBLED_STATE_IDLE:
        // Stop DMA transfer
        HAL_TIM_PWM_Stop_DMA(rgbString->timHandle, rgbString->timChannel);

        break;

    case WS2812BRGBLED_STATE_ACTIVE:
        // Check if another fame is available to be queued
        if ((rgbString->currentFrame) < (rgbString->frameArrayLen - 1)) {

            rgbString->currentFrame++;
            ws2812bRgbLedEnqueueFrame(&(rgbString->bitQueue[bitQueueFrame]), &(rgbString->frameArray[rgbString->currentFrame]));

        // All frames enqueued, start queuing reset period
        } else {

            rgbString->state = WS2812BRGBLED_STATE_RESET;
            rgbString->currentFrame = 0;
            ws2812bRgbLedEnqueueReset(&(rgbString->bitQueue[bitQueueFrame]));
        }

        break;

    case WS2812BRGBLED_STATE_RESET:
        // Check if reset period is still occurring
        if ((rgbString->currentFrame) < (WS2812B_RGB_LED_RESET_CYCLES - 1)) {
            rgbString->currentFrame++;
            ws2812bRgbLedEnqueueReset(&(rgbString->bitQueue[bitQueueFrame]));

        // All reset periods have been enqueued, prepare to halt DMA on completion of last frame
        } else {
            rgbString->state = WS2812BRGBLED_STATE_IDLE;
        }

        break;
    }
}


/**
 * Convert pixel data struct into array of PWM values for waveform generation
 * with gamma correction applied.
 *
 * @param bitQueue Pointer to array of PWM codes transfered by DMA
 *
 * @param led Pointer to RGB LED pixel
 */
static void ws2812bRgbLedEnqueueFrame(uint32_t *bitQueue, const ws2812bRgbLedPixel_t *led) {
    uint32_t i;

    // Green
    for (i = 0; i < 8; i++) {
    if (READ_BIT(led->g, (0x80>>i))) {
        bitQueue[i] = WS2812B_RGB_LED_1_CODE_COUNT;
        } else {
            bitQueue[i] = WS2812B_RGB_LED_0_CODE_COUNT;
        }
    }

    // Blue
    for (i = 0; i < 8; i++) {
    if (READ_BIT(led->r, (0x80>>i))) {
        bitQueue[i+WS2812B_RGB_LED_BITS_PER_COLOR] = WS2812B_RGB_LED_1_CODE_COUNT;
        } else {
            bitQueue[i+WS2812B_RGB_LED_BITS_PER_COLOR] = WS2812B_RGB_LED_0_CODE_COUNT;
        }
    }

    // Red
    for (i = 0; i < 8; i++) {
        if (READ_BIT(led->b, (0x80>>i))) {
            bitQueue[i+(WS2812B_RGB_LED_BITS_PER_COLOR*2)] = WS2812B_RGB_LED_1_CODE_COUNT;
        } else {
            bitQueue[i+(WS2812B_RGB_LED_BITS_PER_COLOR*2)] = WS2812B_RGB_LED_0_CODE_COUNT;
        }
    }
}


/**
 * Fill bitQueue frame with 0 to cause LEDs to reset and latch
 *
 * @param bitQueue Pointer to array of PWM codes transfered by DMA
 */
static void ws2812bRgbLedEnqueueReset(uint32_t *bitQueue) {
    uint32_t i;

    for (i = 0; i < WS2812B_RGB_LED_BIT_QUEUE_FRAME_LEN; i++) {
        bitQueue[i] = 0;
    }
}
