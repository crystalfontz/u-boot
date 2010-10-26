/*
 * (C) Copyright 2010
 * Rob Emanuele <rje@crystalfontz.com>
 * Crystalfontz America, Inc. <www.crystalfontz.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <asm/sizes.h>
#include <asm/arch/at91sam9g45.h>
#include <asm/arch/at91sam9_matrix.h>
#include <asm/arch/at91sam9_smc.h>
#include <asm/arch/at91_common.h>
#include <asm/arch/at91_pmc.h>
#include <asm/arch/at91_rstc.h>
#include <asm/arch/clk.h>
#include <asm/arch/gpio.h>
#include <asm/arch/io.h>
#include <asm/arch/hardware.h>
#include <lcd.h>
#include <atmel_lcdc.h>
#if defined(CONFIG_RESET_PHY_R) && defined(CONFIG_MACB)
#include <net.h>
#endif
#include <netdev.h>

DECLARE_GLOBAL_DATA_PTR;

extern int atmel_mmc_init(bd_t *bis);

/* ------------------------------------------------------------------------- */
/*
 * Miscelaneous platform dependent initialisations
 */

#ifdef CONFIG_MACB
static void at91sam9m10g45ek_macb_hw_init(void)
{
	unsigned long wait;

	/* Enable clock */
	at91_sys_write(AT91_PMC_PCER, 1 << AT91SAM9G45_ID_EMAC);

	/* Disable processor pull-ups on KSZ8041NL strapping pins */
	writel(pin_to_mask(AT91_PIN_PA12) |
	       pin_to_mask(AT91_PIN_PA13) |
	       pin_to_mask(AT91_PIN_PA15) |
	       pin_to_mask(AT91_PIN_PA16),
	       pin_to_controller(AT91_PIN_PA0) + PIO_PUDR);

	/* PHY Starts out in reset */
	/* Bring the phy out of reset now that the processor
	   pull-ups are disbled */
	at91_set_gpio_output(AT91_PIN_PD4, 0);

	/* Wait for the PHY to be done reading the config line values */
	/*  KSZ8041NL docs say wait 100ns before programming
	    4ns/cycle on this processor, ~2 cycles per loop */
	wait = 0;
	while(wait++ < 13) asm("nop");

	/* Re-enable pull-ups */
	writel(pin_to_mask(AT91_PIN_PA12) |
	       pin_to_mask(AT91_PIN_PA13) |
	       pin_to_mask(AT91_PIN_PA15) |
	       pin_to_mask(AT91_PIN_PA16),
	       pin_to_controller(AT91_PIN_PA0) + PIO_PUER);

	at91_macb_hw_init();
}

static void enable_phy_clk(void)
{
	/* Set programmable clock 0 (PCK0) to use the PLLA clock / 8 */ 
	at91_sys_write(AT91_PMC_PCKR(0), 0xe);

	/* Enable PCK0 */
	at91_sys_write(AT91_PMC_SCER, AT91_PMC_PCK0);

	/* Wait for ready on PCK0 */
	while(!(at91_sys_read(AT91_PMC_SR) & AT91_PMC_PCK0RDY));

	// configure PD26 as Peripheral A (PCK0), with a Pull-Up
	at91_set_A_periph(AT91_PIN_PD26, 1);
}
#endif

#ifdef CONFIG_MMC
static void at91sam9m10g45ek_mmc_hw_init(void)
{
	at91_set_A_periph(AT91_PIN_PA0, 0); /* CLK */
	at91_set_A_periph(AT91_PIN_PA1, 1); /* CDA */
	at91_set_A_periph(AT91_PIN_PA2, 1); /* D0 */
	at91_set_A_periph(AT91_PIN_PA3, 1); /* D1 */
	at91_set_A_periph(AT91_PIN_PA4, 1); /* D2 */
	at91_set_A_periph(AT91_PIN_PA5, 1); /* D3 */

	at91_sys_write(AT91_PMC_PCER, 1 << AT91SAM9G45_ID_MCI0);
}

int board_mmc_init(bd_t *bd)
{
	at91sam9m10g45ek_mmc_hw_init();
	return atmel_mmc_init(bd);
}
#endif


int board_init(void)
{
	/* Enable Ctrlc */
	console_init_f();

	/* arch number */
	gd->bd->bi_arch_number = MACH_TYPE_CFA_10022;

	/* adress of boot parameters */
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x100;

	/* normally you'd set up the serial pins here, we assume the first stage has done it.
	   this way if you have no console the pins aren't floating */
	/* at91_serial_hw_init(); no thank you */

#ifdef CONFIG_HAS_DATAFLASH
	at91_spi0_hw_init(1 << 0);
#endif

#ifdef CONFIG_MACB
	enable_phy_clk();
	at91sam9m10g45ek_macb_hw_init();
#endif

	return 0;
}

int dram_init(void)
{
	gd->bd->bi_dram[0].start = PHYS_SDRAM;
	gd->bd->bi_dram[0].size = PHYS_SDRAM_SIZE;
	return 0;
}

#ifdef CONFIG_RESET_PHY_R
void reset_phy(void)
{
#ifdef CONFIG_MACB
	/*
	 * Initialize ethernet HW addr prior to starting Linux,
	 * needed for nfsroot
	 */
	eth_init(gd->bd);
#endif
}
#endif

int board_eth_init(bd_t *bis)
{
	int rc = 0;
#ifdef CONFIG_MACB
	rc = macb_eth_initialize(0, (void *)AT91SAM9G45_BASE_EMAC, 0x01);
#endif
	return rc;
}


