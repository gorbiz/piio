#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <stdint.h>

#ifdef PIIO_CLAM_READ
	#define PIIO_FIX_LEVEL(x) !(!(x))
#else
	#define PIIO_FIX_LEVEL(x) (x)
#endif

typedef enum piio_error
{
	PIIO_ERROR_NONE = 0,
	PIIO_NOT_ROOT,
	PIIO_UNSUPPORTED_PI,
	PIIO_FAILED_TO_MAP,
	PIIO_INVALID_PIN,
	PIIO_INVALID_MODE,
}
piio_error_t;

#define PIIO_OK PIIO_ERROR_NONE

typedef enum piio_pin_mode
{
	PIIO_INPUT,
	PIIO_OUTPUT
}
piio_pin_mode_t;

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

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef int bool_t;

#define PIIO_MAX_PINS 64

static volatile piio_gpio_t *gpio	= NULL;
static const int *phys_to_bcm		= NULL;
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


static const int phys_to_bcm_v1[PIIO_MAX_PINS] =
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

static const int phys_to_bcm_v2[PIIO_MAX_PINS] =
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

static int get_pi_version(void)
{
	FILE *fp;
	char line[256];
	int version = 0;

	if(NULL == (fp = fopen("/proc/cpuinfo", "rb")))
	{
		return 0;
	}

	while(NULL != fgets(line, sizeof(line), fp))
	{
		char *p = line + strlen(line) - 1;

		if(0 != strncasecmp(line, "Hardware", 8))
		{
			continue;
		}

		while('\n' == *p || '\r' == *p)
		{
			*(p--) = 0;
		}

		while(p > line && ' ' != *p)
		{
			--p;
		}

		if(0 == strcasecmp(p + 1, "BCM2709"))
		{
			version = 2;
			goto cleanup;
		}
	}

	//
	// TODO : Fix v1
	//

cleanup:
	fclose(fp);
	return version;
}

static piio_error_t map_peripherals(int version)
{
	static const int open_flags = (O_RDWR | O_CLOEXEC | O_SYNC);

	piio_error_t rc	= PIIO_FAILED_TO_MAP;
	uint32_t base	= 0;

	int fd = open("/dev/gpiomem", open_flags);

	if(fd < 0)
	{
		if(2 == version)
		{
			base = 0x3F000000;
		}
		else if(1 == version)
		{
			base = 0x20000000;
		}
		else
		{
			return PIIO_UNSUPPORTED_PI;
		}

		fd = open("/dev/mem", open_flags);

		if(fd < 0)
		{
			return PIIO_FAILED_TO_MAP;
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

piio_error_t piio_initialize(void)
{
	piio_error_t rc;
	int version;

	if(0 != geteuid())
	{
		return PIIO_NOT_ROOT;
	}

	version = get_pi_version();

	if(0 == version)
	{
		return PIIO_UNSUPPORTED_PI;
	}

	if(PIIO_OK != (rc = map_peripherals(version)))
	{
		return rc;
	}

	phys_to_bcm = (1 == version) ? phys_to_bcm_v1 : phys_to_bcm_v2;
	gpio_to_bcm = (1 == version) ? gpio_to_bcm_v1 : gpio_to_bcm_v2;

	return PIIO_OK;
}

static piio_error_t set_pin_mode(int bcm, piio_pin_mode_t mode)
{
	if(-1 == bcm)
	{
		return PIIO_INVALID_PIN;
	}
	else
	{
		volatile uint32_t * const modereg = gpio->modereg + bcm_to_modereg[bcm];
		const uint32_t modeshift = bcm_to_modeshift[bcm];

		if(PIIO_INPUT == mode)
		{
			*modereg = *modereg & ~(7 << modeshift);
		}
		else if(PIIO_OUTPUT == mode)
		{
			*modereg = (*modereg & ~(7 << modeshift)) | (1 << modeshift);
		}
		else
		{
			return PIIO_INVALID_MODE;
		}
	}

	return PIIO_OK;
}

piio_error_t piio_set_mode_phys(uint32_t phys, piio_pin_mode_t mode)
{
	return set_pin_mode(phys_to_bcm[phys & (PIIO_MAX_PINS - 1)], mode);
}

piio_error_t piio_set_mode_gpio(uint32_t gpio, piio_pin_mode_t mode)
{
	return set_pin_mode(gpio_to_bcm[gpio & (PIIO_MAX_PINS - 1)], mode);
}

static uint32_t read_bcm(int bcm)
{
	if(-1 == bcm)
	{
		return 0;
	}

	return PIIO_FIX_LEVEL(gpio->lvlreg[bcm_to_rwreg[bcm]] & (1 << (bcm & 0x1F)));
}

uint32_t piio_read_phys(uint32_t phys)
{
	return read_bcm(phys_to_bcm[phys & (PIIO_MAX_PINS - 1)]);
}

uint32_t piio_read_gpio(uint32_t gpio)
{
	return read_bcm(gpio_to_bcm[gpio & (PIIO_MAX_PINS - 1)]);
}

static void write_bcm(int bcm, uint32_t val)
{
	if(-1 == bcm)
	{
		return;
	}

	if(val)
	{
		gpio->setreg[bcm_to_rwreg[bcm]] = (1 << (bcm & 0x1F));
	}
	else
	{
		gpio->clrreg[bcm_to_rwreg[bcm]] = (1 << (bcm & 0x1F));
	}
}

void piio_write_phys(uint32_t phys, uint32_t val)
{
	write_bcm(phys_to_bcm[phys & (PIIO_MAX_PINS - 1)], val);
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

	printf("Pin 0: %d\n", piio_set_mode_phys(0, PIIO_OUTPUT));
	printf("Pin 1: %d\n", piio_set_mode_phys(1, PIIO_OUTPUT));
	printf("Pin 3: %d\n", piio_set_mode_phys(3, PIIO_OUTPUT));

	for(i = 0; ; ++i)
	{
		piio_write_phys(3, i & 1);
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




