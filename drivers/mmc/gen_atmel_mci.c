/*
 * Copyright (C) 2010
 * Rob Emanuele <rje@crystalfontz.com>
 * Crystalfontz America, Inc. <www.crystalfontz.com>
 *
 * Original Driver:
 * Copyright (C) 2004-2006 Atmel Corporation
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <config.h>
#include <common.h>
#include <command.h>
#include <hwconfig.h>
#include <mmc.h>
#include <part.h>
#include <malloc.h>
#include <mmc.h>
#include <asm/io.h>
#include <asm/errno.h>
#include <asm/byteorder.h>
#include <asm/arch/clk.h>
#include <asm/arch/memory-map.h>
#include "atmel_mci.h"

DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_SYS_MMC_CLK_OD
#define CONFIG_SYS_MMC_CLK_OD		150000
#endif
#define MMC_DEFAULT_BLKLEN	512

void dump_cmd(uint cmdr, uint arg, uint status, const char* msg)
{
	printf("gen_atmel_mci: CMDR 0x%08x (%2u) ARGR 0x%08x (SR: 0x%08x) %s\n",
	       cmdr, cmdr&0x3F, arg, status, msg);
}

/* Setup for MCI Clock and Block Size */
static void mci_set_mode(unsigned long hz, unsigned long blklen)
{
	unsigned long bus_hz;
	unsigned long clkdiv;

	if (hz > 0) {
		bus_hz = get_mck_clk_rate();
		clkdiv = (bus_hz / hz) / 2 - 1;
	} else {
		clkdiv = ~0UL;
	}

	printf("mmc: setting clock %lu Hz, block size %lu\n",
	       hz, blklen);

	if (clkdiv & ~255UL) {
		clkdiv = 255;
		printf("mmc: clock %lu too low; setting CLKDIV to 255\n",
		       hz);
	}

	blklen &= 0xfffc;
	/* On some platforms RDPROOF and WRPROOF are ignored */
	mmci_writel(MR, (MMCI_BF(CLKDIV, clkdiv)
			 | MMCI_BF(BLKLEN, blklen)
			 | MMCI_BIT(RDPROOF)
			 | MMCI_BIT(WRPROOF)));
}

/* Return the CMDR with flags for a given command and data packet */
static uint atmel_encode_cmd(struct mmc_cmd *cmd, struct mmc_data *data, uint* error_flags)
{
	uint cmdr = 0;
	/* Default Flags for Errors */
	*error_flags |= (MMCI_BIT(DTOE) | MMCI_BIT(RDIRE) | MMCI_BIT(RENDE) | MMCI_BIT(RINDE) | MMCI_BIT(RTOE));

	/* Default Flags for the Command */
	cmdr |= MMCI_BIT(MAXLAT);

	if (data) {
		cmdr |= MMCI_BF(TRCMD,1);

		if (data->blocks > 1) {
			cmdr |= MMCI_BF(TRTYP,1);
		}

		if (data->flags & MMC_DATA_READ)
			cmdr |= MMCI_BIT(TRDIR);
	}

	if (cmd->resp_type & MMC_RSP_CRC)
		*error_flags |= MMCI_BIT(RCRCE);

	if (cmd->resp_type & MMC_RSP_136)
		cmdr |= MMCI_BF(RSPTYP,2);
	else if (cmd->resp_type & MMC_RSP_BUSY)
		cmdr |= MMCI_BF(RSPTYP,3);
	else if (cmd->resp_type & MMC_RSP_PRESENT)
		cmdr |= MMCI_BF(RSPTYP,1);

	return cmdr | MMCI_BF(CMDNB,cmd->cmdidx);
}

static uint atmel_data_read(uint* data, uint error_flags)
{
	uint status;
	do {
		status = mmci_readl(SR);
		if (status & (error_flags | MMCI_BIT(OVRE)))
			goto io_fail;
	} while (!(status & MMCI_BIT(RXRDY)));

	if (status & MMCI_BIT(RXRDY)) {
		*data = mmci_readl(RDR);
		status = 0;
	}
io_fail:
	return status;
}

static uint atmel_data_write(uint* data, uint error_flags)
{
	uint status;
	do {
		status = mmci_readl(SR);
		if (status & (error_flags | MMCI_BIT(UNRE)))
			goto io_fail;
	} while (!(status & MMCI_BIT(TXRDY)));

	if (status & MMCI_BIT(TXRDY)) {
		mmci_writel(TDR,*data);
		status = 0;
	}
io_fail:
	return status;
}

/*
 * Sends a command out on the bus and deals with the block data.
 * Takes the mmc pointer, a command pointer, and an optional data pointer.
 */
static int
atmel_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd, struct mmc_data *data)
{
	uint cmdr;
	uint error_flags = 0;
	uint status;

	/* Figure out the transfer arguments */
	cmdr = atmel_encode_cmd(cmd, data, &error_flags);

	/* Send the command */
	mmci_writel(ARGR,cmd->cmdarg);
	mmci_writel(CMDR, cmdr);

#if GEN_ATMEL_MCI_DEBUG
	dump_cmd(cmdr, cmd->cmdarg, 0, "COMMAND Dbg Msg");
#endif

	/* Wait for the command to complete */
	while (!((status = mmci_readl(SR)) & MMCI_BIT(CMDRDY)));

	if (status & error_flags) {
		dump_cmd(cmdr, cmd->cmdarg, status, "COMMAND Failed");
		return COMM_ERR;
	}

	/* Copy the response to the response buffer */
	if (cmd->resp_type & MMC_RSP_136) {
		cmd->response[0] = mmci_readl(RSPR);
		cmd->response[1] = mmci_readl(RSPR1);
		cmd->response[2] = mmci_readl(RSPR2);
		cmd->response[3] = mmci_readl(RSPR3);
	} else
		cmd->response[0] = mmci_readl(RSPR);

	/* transfer all of the blocks */
	if (data) {
		uint word_count, block_count;
		uint* ioptr;
		uint sys_blocksize, dummy, i;
		uint (*atmel_data_op)(uint* data, uint error_flags);

		if (data->flags & MMC_DATA_READ) {
			atmel_data_op = atmel_data_read;
			sys_blocksize = mmc->read_bl_len;
			ioptr = (uint*)data->dest;
		} else {
			atmel_data_op = atmel_data_write;
			sys_blocksize = mmc->write_bl_len;
			ioptr = (uint*)data->src;
		}

		status = 0;
		for(block_count = 0; block_count < data->blocks && !status; block_count++) {
			word_count = 0;
			do {
				status = atmel_data_op(ioptr,error_flags);
				word_count++;
				ioptr++;
			} while(!status && word_count < (data->blocksize / 4));
#if GEN_ATMEL_MCI_DEBUG
			if (data->flags & MMC_DATA_READ)
			{
				char *mem_args[3];
				char mem_addr_str[16];
				char mem_len_str[16];
				printf("Command caused a block read for %u bytes (read %u bytes)\n", data->blocksize, word_count*4);
				mem_args[0] = "md.l";
				sprintf(mem_addr_str,"0x%08x",(uint)(data->dest));
				mem_args[1] = mem_addr_str;
				sprintf(mem_len_str,"0x%x",word_count);
				mem_args[2] = mem_len_str;
				printf("Dumping with: %s %s %s\n", mem_args[0], mem_args[1], mem_args[2]);
				do_mem_md(NULL, 0, 3, mem_args);
			}
#endif
#if GEN_ATMEL_MCI_DEBUG
			if (!status && word_count < (sys_blocksize / 4))
				printf("sponging....\n");
#endif
			/* sponge the rest of a full block */
			while (!status && word_count < (sys_blocksize / 4)) {
				status = atmel_data_op(&dummy,error_flags);
				word_count++;
			}
			if (status) {
				dump_cmd(cmdr, cmd->cmdarg, status, "XFER Failed");
				return COMM_ERR;
			}
		}

		/* Wait for Transfer End */
		i = 0;
		do {
			status = mmci_readl(SR);

			if (status & error_flags) {
				dump_cmd(cmdr, cmd->cmdarg, status, "XFER DTIP Wait Failed");
				return COMM_ERR;
			}
			i++;
		} while ((status & MMCI_BIT(DTIP)) && i < 10000);
		if (status & MMCI_BIT(DTIP)) {
			dump_cmd(cmdr, cmd->cmdarg, status, "XFER DTIP never unset, ignoring");
		}
	}

	return 0;
}

static void atmel_set_ios(struct mmc *mmc)
{
	/* Set the clock speed */
	mci_set_mode(mmc->clock, MMC_DEFAULT_BLKLEN);

	/* set the bus width and select slot A for this interface
	 * there is no capability for multiple slots on the same interface yet
	 */
	/* Bitfield SCDBUS needs to be expanded to 2 bits for 8-bit buses
	 */
	if (mmc->bus_width == 4)
		mmci_writel(SDCR, MMCI_BF(SCDBUS,0x1)|MMCI_BF(SCDSEL,0x0));
	else
		mmci_writel(SDCR, MMCI_BF(SCDBUS,0x0)|MMCI_BF(SCDSEL,0x0));
}

static int atmel_init(struct mmc *mmc)
{
	/* Initialize controller */
	mmci_writel(CR, MMCI_BIT(SWRST));  /* soft reset */
	mmci_writel(CR, MMCI_BIT(PWSDIS)); /* disable power save */
	mmci_writel(CR, MMCI_BIT(MCIEN));  /* enable mci */

	/* Initial Time-outs */
	mmci_writel(DTOR, 0x7f);
	/* Disable Interrupts */
	mmci_writel(IDR, ~0UL);

	/* Set defualt clocks and blocklen */
	mci_set_mode(CONFIG_SYS_MMC_CLK_OD, MMC_DEFAULT_BLKLEN);

	return 0;
}

static int atmel_initialize(bd_t *bis)
{
	struct mmc *mmc;

	mmc = calloc(1,sizeof(struct mmc));

	sprintf(mmc->name, "Atmel MCI");
	/* We set this but atmel_mci.h makes use of MMCI_BASE directly */
	mmc->priv = (void *)MMCI_BASE;
	mmc->send_cmd = atmel_send_cmd;
	mmc->set_ios = atmel_set_ios;
	mmc->init = atmel_init;

	/* need to be able to pass these in on a baord by board basis */
	mmc->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->host_caps = MMC_MODE_4BIT;

	/* High Speed Support? */

	mmc->f_min = get_mck_clk_rate() / (2*(0xFF+1));
	mmc->f_max = get_mck_clk_rate() / (2);

	mmc_register(mmc);

	return 0;
}

int atmel_mmc_init(bd_t *bis)
{
	return atmel_initialize(bis);
}
