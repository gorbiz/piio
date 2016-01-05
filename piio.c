#include "piio.h"

#ifndef PIIO_KERNEL_MODULE
//
// User mode headers
//
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <stdint.h>

static int piio_model		= 0;
static int piio_board_rev	= 0;

#else
//
// Check compile-time configuration
//
#ifndef PIIO_KERNEL_GPIO_BASE
	#error To build PIIO for kernel mode, you must define PIIO_KERNEL_GPIO_BASE to either PIIO_KERNEL_GPIO_BASE_PI1 or PIIO_KERNEL_GPIO_BASE_PI2
#endif

#if PIIO_KERNEL_GPIO_BASE == PIIO_KERNEL_GPIO_BASE_PI2
	#ifndef PIIO_KERNEL_BOARD_REV
		#define PIIO_KERNEL_BOARD_REV PIIO_BOARD_REV_2
	#elif PIIO_KERNEL_BOARD_REV != PIIO_BOARD_REV_2
		#error Invalid configuration, PIIO_KERNEL_GPIO_BASE requires PIIO_KERNEL_BOARD_REV == PIIO_BOARD_REV_2
	#endif
#elif PIIO_KERNEL_GPIO_BASE == PIIO_KERNEL_GPIO_BASE_PI1
	#ifndef PIIO_KERNEL_BOARD_REV
		#error You must define PIIO_KERNEL_BOARD_REV to build PIIO as a kernel module
	#elif (PIIO_KERNEL_BOARD_REV != PIIO_BOARD_REV_1) && (PIIO_KERNEL_BOARD_REV != PIIO_BOARD_REV_2)
		#error Invalid value PIIO_KERNEL_BOARD_REV
	#endif
#else
	#error Invalid value of PIIO_KERNEL_GPIO_BASE
#endif
//
// Kernel mode headers
//
#include <asm/io.h>
#include <linux/kernel.h>

static const int piio_board_rev = PIIO_KERNEL_BOARD_REV;

#endif

#ifdef PIIO_CLAMP_READ
	#define PIIO_FIX_LEVEL(x) !(!(x))
#else
	#define PIIO_FIX_LEVEL(x) (x)
#endif

#pragma pack(push, 1)

typedef struct piio_gpio
{
	uint32_t modereg[6];
	uint32_t reserved0; // ?
	uint32_t setreg[2];
	uint32_t reserved1; // ?
	uint32_t clrreg[2];
	uint32_t reserved2; // ?
	uint32_t lvlreg[2];
}
piio_gpio_t;

#pragma pack(pop)

#define PIIO_MAX_PINS 64

static volatile piio_gpio_t *gpio	= NULL;
static const int *pin_to_bcm		= NULL;
static const int *gpio_to_bcm		= NULL;

static const uint8_t bcm_to_rwreg[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const uint8_t bcm_to_modereg[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

static const uint8_t bcm_to_modeshift[] =
{
	0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
	0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
	0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
	0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
	0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
};

static const int gpio_to_bcm_v1[PIIO_MAX_PINS] =
{
	17, 18, 21, 22, 23, 24, 25, 4,	// GPIO 0 through 7			; wpi  0 -  7
	0,  1,							// I2C  - SDA1, SCL1		; wpi  8 -  9
	8,  7,							// SPI  - CE1, CE0			; wpi 10 - 11
	10,  9, 11, 					// SPI  - MOSI, MISO, SCLK	; wpi 12 - 14
	14, 15,							// UART - Tx, Rx			; wpi 15 - 16

	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
} ;

static const int gpio_to_bcm_v2[PIIO_MAX_PINS] =
{
	17, 18, 27, 22, 23, 24, 25, 4,	// GPIO 0 through 7				; wpi  0 -  7
	2,  3,							// I2C  - SDA0, SCL0			; wpi  8 -  9
	8,  7,							// SPI  - CE1, CE0				; wpi 10 - 11
	10,  9, 11, 					// SPI  - MOSI, MISO, SCLK		; wpi 12 - 14
	14, 15,							// UART - Tx, Rx				; wpi 15 - 16
	28, 29, 30, 31,					// v2: New GPIOs 8 though 11	; wpi 17 - 20
	5,  6, 13, 19, 26,				// B+							; wpi 21, 22, 23, 24, 25
	12, 16, 20, 21,					// B+							; wpi 26, 27, 28, 29
	0,  1,							// B+							; wpi 30, 31
	
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
} ;


static const int pin_to_bcm_v1[PIIO_MAX_PINS] =
{
	-1,
	-1,	-1,
	 0,	-1,
	 1,	-1,
	 4,	14,
	-1,	15,
	17,	18,
	21,	-1,
	22,	23,
	-1,	24,
	10,	-1,
	 9,	25,
	11,	 8,
	-1,	 7,

	-1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static const int pin_to_bcm_v2[PIIO_MAX_PINS] =
{
	-1,
	-1,	-1,
	 2,	-1,
	 3,	-1,
	 4,	14,
	-1,	15,
	17,	18,
	27,	-1,
	22,	23,
	-1,	24,
	10,	-1,
	 9,	25,
	11,	 8,
	-1,	 7,
	//
	// B+
	//
	 0,	 1,
	 5,	-1,
	 6,	12,
	13, -1,
	19, 16,
	26, 20,
	-1, 21,
	//
	// P5
	//
	-1,	-1,
	-1,	-1,
	-1,	-1,
	-1,	-1,
	-1,	-1,
	28,	29,
	30,	31,
	-1,	-1,
	-1,	-1,
	-1,	-1,
	-1,	-1,
};

#ifndef PIIO_KERNEL_MODULE

static const char *get_cpuinfo_param(const char *name, char *line)
{
	char *p = line + strlen(line) - 1;

	if(0 != strncasecmp(line, name, strlen(name)))
	{
		return NULL;
	}

	while('\n' == *p || '\r' == *p)
	{
		*(p--) = 0;
	}

	while(p > line && !isspace(*p))
	{
		--p;
	}

	if(p == line)
	{
		return NULL;
	}

	return (p + 1);
}

static piio_error_t load_pi_version(void)
{

	FILE *fp;
	char line[256];

	if(NULL == (fp = fopen("/proc/cpuinfo", "rb")))
	{
		return PIIO_ERROR_UNEXPECTED;
	}

	while(NULL != fgets(line, sizeof(line), fp))
	{
		const char *param;

		if(NULL != (param = get_cpuinfo_param("Hardware", line)))
		{
			if(0 == strcasecmp(param, "BCM2709"))
			{
				piio_model		= 2;
				piio_board_rev	= 2;

				goto cleanup;
			}
			else if(0 == strcasecmp(param, "BCM2708"))
			{
				piio_model = 1;
			}
			else
			{
				return PIIO_ERROR_UNSUPPORTED_PI;
			}
		}
		else if(NULL != (param = get_cpuinfo_param("Revision", line)))
		{
			const size_t offset = (strlen(param) - 4);

			if(0 == strcmp(param + offset, "0002")
			|| 0 == strcmp(param + offset, "0003"))
			{
				piio_board_rev = 1;
			}
			else
			{
				piio_board_rev = 2;
			}
		}

	}

	//
	// TODO : Fix v1
	//

cleanup:
	fclose(fp);
	return (0 != piio_model) && (0 != piio_board_rev)
		? PIIO_OK
		: PIIO_ERROR_UNSUPPORTED_PI;
}

static piio_error_t map_peripherals(void)
{
	static const int open_flags = (O_RDWR | O_CLOEXEC | O_SYNC);

	piio_error_t rc	= PIIO_ERROR_FAILED_TO_MAP;
	uint64_t base	= 0;

	int fd = open("/dev/gpiomem", open_flags);

	if(fd < 0)
	{
		if(2 == piio_model)
		{
			base = PIIO_KERNEL_GPIO_BASE_PI2;
		}
		else if(1 == piio_model)
		{
			base = PIIO_KERNEL_GPIO_BASE_PI1;
		}
		else
		{
			return PIIO_ERROR_UNSUPPORTED_PI;
		}

		fd = open("/dev/mem", open_flags);

		if(fd < 0)
		{
			return PIIO_ERROR_FAILED_TO_MAP;
		}
	}

	if(NULL == (gpio = (piio_gpio_t *)mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base + 0x200000)))
	{
		goto cleanup;
	}

	rc = PIIO_OK;
cleanup:
	return rc;
}

#else
//
// PIIO_KERNEL_MODULE
//
static piio_error_t map_peripherals(void)
{
	if(NULL == (gpio = (piio_gpio_t *)ioremap_nocache(PIIO_KERNEL_GPIO_BASE + 0x200000, 0x1000)))
	{
		return PIIO_ERROR_FAILED_TO_MAP;
	}

	return PIIO_OK;
}

#endif

piio_error_t piio_initialize(void)
{
	piio_error_t rc;

#ifndef PIIO_KERNEL_MODULE
	if(0 != geteuid())
	{
		return PIIO_ERROR_NOT_ROOT;
	}
#endif

	if(PIIO_OK != (rc = load_pi_version()))
	{
		return rc;
	}

	if(PIIO_OK != (rc = map_peripherals()))
	{
		return rc;
	}

	pin_to_bcm	= (1 == piio_board_rev) ? pin_to_bcm_v1 : pin_to_bcm_v2;
	gpio_to_bcm	= (1 == piio_board_rev) ? gpio_to_bcm_v1 : gpio_to_bcm_v2;

	return PIIO_OK;
}

static piio_error_t set_pin_mode(int bcm, piio_mode_t mode)
{
#ifdef PIIO_CLUMSY
	if(-1 == bcm)
	{
		return PIIO_ERROR_INVALID_PIN;
	}
	else
#endif
	{
		volatile uint32_t * const modereg = gpio->modereg + bcm_to_modereg[bcm];
		const uint32_t modeshift = bcm_to_modeshift[bcm];

		if(PIIO_MODE_INPUT == mode)
		{
			*modereg = *modereg & ~(7 << modeshift);
			return PIIO_OK;
		}
#ifdef PIIO_CLUMSY
		else if(PIIO_MODE_OUTPUT != mode)
		{
			return PIIO_ERROR_INVALID_MODE;
		}
#endif

		*modereg = (*modereg & ~(7 << modeshift)) | (1 << modeshift);
	}

	return PIIO_OK;
}

piio_error_t piio_set_mode_pin(uint32_t pin, piio_mode_t mode)
{
	return set_pin_mode(pin_to_bcm[pin & (PIIO_MAX_PINS - 1)], mode);
}

piio_error_t piio_set_mode_gpio(uint32_t gpio, piio_mode_t mode)
{
	return set_pin_mode(gpio_to_bcm[gpio & (PIIO_MAX_PINS - 1)], mode);
}

static uint32_t read_bcm(int bcm)
{
#ifdef PIIO_CLUMSY
	if(-1 == bcm)
	{
		return 0;
	}
#endif

	return PIIO_FIX_LEVEL(gpio->lvlreg[bcm_to_rwreg[bcm]] & (1 << (bcm & 0x1F)));
}

uint32_t piio_read_pin(uint32_t pin)
{
	return read_bcm(pin_to_bcm[pin & (PIIO_MAX_PINS - 1)]);
}

uint32_t piio_read_gpio(uint32_t gpio)
{
	return read_bcm(gpio_to_bcm[gpio & (PIIO_MAX_PINS - 1)]);
}

static void write_bcm(int bcm, uint32_t val)
{
#ifdef PIIO_CLUMSY
	if(-1 == bcm)
	{
		return;
	}
#endif

	if(val)
	{
		gpio->setreg[bcm_to_rwreg[bcm]] = (1 << (bcm & 0x1F));
	}
	else
	{
		gpio->clrreg[bcm_to_rwreg[bcm]] = (1 << (bcm & 0x1F));
	}
}

void piio_write_pin(uint32_t pin, uint32_t val)
{
	write_bcm(pin_to_bcm[pin & (PIIO_MAX_PINS - 1)], val);
}

void piio_write_gpio(uint32_t gpio, uint32_t val)
{
	write_bcm(gpio_to_bcm[gpio & (PIIO_MAX_PINS - 1)], val);
}

static uint32_t rdtsc32(void)
{
	uint32_t r = 0;
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(r) );
	return r;
}

int hack(void)
{
	uint32_t i;

	printf("Pin 0: %d\n", piio_set_mode_pin(0, PIIO_MODE_OUTPUT));
	printf("Pin 1: %d\n", piio_set_mode_pin(1, PIIO_MODE_OUTPUT));
	printf("Pin 3: %d\n", piio_set_mode_pin(3, PIIO_MODE_OUTPUT));

	for(i = 0; ; ++i)
	{
		piio_write_pin(3, i & 1);
		sleep(1);

		printf("Hello\n");
	}
}

int main(int argc, const char *argv[])
{
	piio_error_t rc;

	if(PIIO_OK != (rc = piio_initialize()))
	{
		printf("Initialize failed with: %d\n", rc);
		return -1;
	}

	printf("Successfully initialized PIIO\n");

	hack();

	return 0;
}




