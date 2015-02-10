/*
 * Enable/disable breathing LED on Endless EC-200.
 *
 * This is done by manipulating the GP37 output of the IT8772 Super-IO
 * chip found on the board.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

/* Port addresses for access to SuperIO chip on LPC bus */
#define PORT_ADDR	0x2e
#define PORT_DATA	0x2f

/* The Super-IO chip conforms to ISA PNP standards */
#define ISAPNP_REG_LDN		0x07
#define ISAPNP_REG_DEVID	0x20

/* Logical devices */
#define IT8772_LDN_GPIO	7

/* Registers under GPIO LDN */
#define IT8772_GPIO3_PINCTRL		0x27
#define IT8772_GPIO_SIMPLE_IO_BASE	0x62
#define IT8772_GPIO3_SIMPLE_EN		0xc2
#define IT8772_GPIO3_OUTPUT_EN		0xca

static void superio_select_reg(uint8_t address)
{
	outb(address, PORT_ADDR);
}

static uint8_t superio_inb(uint8_t address)
{
	superio_select_reg(address);
	return inb(PORT_DATA);
}

static void superio_outb(uint8_t data, uint8_t address)
{
	superio_select_reg(address);
	outb(data, PORT_DATA);
}

static uint16_t superio_inw(uint8_t address)
{
	uint16_t ret;
	ret = superio_inb(address) << 8;
	ret |= superio_inb(address + 1);
	return ret;
}

static void isapnp_enter(void)
{
	superio_select_reg(0x87);
	superio_select_reg(0x01);
	superio_select_reg(0x55);
	superio_select_reg(0x55);
}

static void isapnp_exit(void)
{
	superio_outb(0x2, 0x2);
}

static bool isapnp_check_devid(void)
{
	uint16_t devid = superio_inw(ISAPNP_REG_DEVID);

	if (devid == 0x8772)
		return true;

	printf("Found unexpected ISA PNP device with id %x\n", devid);
	return false;
}

static void it8772_gp37_setup(bool high)
{
	uint8_t tmp;
	uint16_t gpio_control_reg;

	/* Select GPIO logical device */
	superio_outb(IT8772_LDN_GPIO, ISAPNP_REG_LDN);

	/* Enable GP37 GPIO in pin control register */
	tmp = superio_inb(IT8772_GPIO3_PINCTRL);
	superio_outb(tmp | (1 << 7), IT8772_GPIO3_PINCTRL);

	/* Set GP37 as a Simple I/O */
	tmp = superio_inb(IT8772_GPIO3_SIMPLE_EN);
	superio_outb(tmp | (1 << 7), IT8772_GPIO3_SIMPLE_EN);

	/* Find GPIO base address */
	gpio_control_reg = superio_inw(IT8772_GPIO_SIMPLE_IO_BASE) + 2;

	if (ioperm(gpio_control_reg, 1, 1) == 0) {
		/* Set GP37 output level */
		tmp = inb(gpio_control_reg);
		if (high)
			outb(tmp | (1 << 7), gpio_control_reg);
		else
			outb(tmp & ~(1 << 7), gpio_control_reg);
	} else {
		perror("No GPIO IO permission\n");
	}

	/* Set GP37 to output */
	tmp = superio_inb(IT8772_GPIO3_OUTPUT_EN);
	superio_outb(tmp | (1 << 7), IT8772_GPIO3_OUTPUT_EN);
}

static bool ec100_detect(void)
{
	FILE *fd = fopen("/proc/device-tree/compatible", "r");
	char compatible[14];

	if (!fd)
		return false;

	fread(compatible, 1, sizeof(compatible), fd);
	fclose(fd);

	return strncmp(compatible, "endless,ec100", sizeof(compatible)) == 0;
}

static void ec100_breathe(int enable)
{
	FILE *fd;

	if (access("/sys/class/meson_gpio", F_OK) != 0)
		return;

	fd = fopen("/sys/class/meson_gpio/breathing", "w");
	if (!fd)
		return;

	fputs(enable ? "0" : "1", fd);
	fclose(fd);
}

static bool ec200_detect(void)
{
	FILE *fd;
	char product_name[7];

	/* Check that this is an EC-200 */
	fd = fopen("/sys/class/dmi/id/product_name", "r");
	if (!fd)
		return false;

	fread(product_name, 1, sizeof(product_name), fd);
	fclose(fd);

	return strncmp(product_name, "EC-200\n", sizeof(product_name)) == 0;
}

static int ec200_breathe(int enable)
{
	/* Request access to required ports */
	if (ioperm(PORT_ADDR, 1, 1)) {
		perror("ioperm ADDR");
		return 1;
	}

	if (ioperm(PORT_DATA, 1, 1)) {
		perror("ioperm DATA");
		return 1;
	}

	isapnp_exit(); /* reset state */

	isapnp_enter();
	if (!isapnp_check_devid()) {
		isapnp_exit();
		return 1;
	}

	it8772_gp37_setup(enable);

	isapnp_exit();
	return 0;
}

int main(int argc, char *argv[])
{
	int enable;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <0/1>\n", argv[0]);
		return 1;
	}

	enable = !!atoi(argv[1]);

	if (ec100_detect())
		ec100_breathe(enable);
	else if (ec200_detect())
		ec200_breathe(enable);

	return 0;
}
