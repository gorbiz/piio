#ifndef __PIIO_H_DEF__
#define __PIIO_H_DEF__

//
// START Compile-time configuration
//

//
// Uncomment PIIO_CLAMP_READ to have the read functions clamp the return
// value to 1 or 0. If not defined, 0 is low and anything else is high.
//
// #define PIIO_CLAMP_READ

//
// Uncomment PIIO_CLUMSY to have PIIO perform additional error-checking.
// For instance, passing an invalid PIN to a function will error instead
// of possibly crashing.
//
// #define PIIO_CLUMSY

//
// Uncomment PIIO_KERNEL_MODULE to use PIIO in a kernel module.
// If you uncomment this, you _must_ set PIIO_KERNEL_GPIO_BASE and
// PIIO_KERNEL_BOARD_REV correctly. These are auto detected for user-mode
// applications.
//
//#define PIIO_KERNEL_MODULE
//#define PIIO_KERNEL_GPIO_BASE PIIO_KERNEL_GPIO_BASE_PI2
// #define PIIO_KERNEL_MODULE
// #define PIIO_KERNEL_GPIO_BASE /* PIIO_KERNEL_GPIO_BASE_PI1
//                               or PIIO_KERNEL_GPIO_BASE_PI2 */
// #define PIIO_KERNEL_BOARD_REV /* PIIO_BOARD_REV_1 (Rev 1, Rev 1.1)
//                               or PIIO_BOARD_REV_2 (B+, PI2)

//
// END Compile-time configuration
//

typedef enum piio_error
{
	PIIO_ERROR_NONE = 0,
	PIIO_ERROR_NOT_ROOT,
	PIIO_ERROR_UNSUPPORTED_PI,
	PIIO_ERROR_FAILED_TO_MAP,
	PIIO_ERROR_INVALID_PIN,
	PIIO_ERROR_INVALID_MODE,
	PIIO_ERROR_UNEXPECTED,
}
piio_error_t;

#define PIIO_OK PIIO_ERROR_NONE

typedef enum piio_mode
{
	PIIO_MODE_INPUT,
	PIIO_MODE_OUTPUT
}
piio_mode_t;

#define PIIO_KERNEL_GPIO_BASE_PI1 0x20000000
#define PIIO_KERNEL_GPIO_BASE_PI2 0x3F000000

#define PIIO_BOARD_REV_1 1
#define PIIO_BOARD_REV_2 2

#ifdef PIIO_KERNEL_MODULE
	#include <linux/types.h>
#else
	#include <stdint.h>
#endif

//
// piio_initialize
//
// Initializes PIIO. Call it before calling any other function of the
// library.
//
piio_error_t piio_initialize(void);

//
// piio_set_mode_pin & piio_set_mode_gpio
//
piio_error_t piio_set_mode_pin(uint32_t pin, piio_mode_t mode);
piio_error_t piio_set_mode_gpio(uint32_t gpio, piio_mode_t mode);

//
// piio_read_pin & piio_read_gpio
//
uint32_t piio_read_pin(uint32_t pin);
uint32_t piio_read_gpio(uint32_t gpio);

//
// piio_write_pin & piio_write_gpio
//
void piio_write_pin(uint32_t pin, uint32_t val);
void piio_write_gpio(uint32_t gpio, uint32_t val);

//
// piio_write_word_pin & piio_write_word_gpio
//
// Writes a word "atomically". Convenient for writing to a data bus.
// Provide the pins corresponding to the word with the most significant
// bit-pin first.
//
// ex:
// If you call it with: piio_write_word_pin(0x0A, 4, 11, 13, 15, 16),
// pins 11 and 15 go high, and pins 13 and 16 low.
//
void piio_write_word_pin(uint32_t word, int num_pins, ...);
void piio_write_word_gpio(uint32_t word, int num_pins, ...);


#endif



