/*
 * SwitchPi DAWN TDM 2FXO+X Interface card Driver for DAHDI Telephony interface.
 * This driver is based on Digium WCTDM driver and developed to support SwitchPi DAWN 2FX0+X board only,
 * you can use it by freely, but there is no warranty as it is.
 * Written by Xin Li <xin.li@switchpi.com>
 *
 * Copyright (C) 2017-2018, SwitchPi, Inc.
 *
 * All rights reserved.
 *
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <asm/io.h>
#include "proslic.h"
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

/*
 *  Define for audio vs. register based ring detection
 *
 */
/* #define AUDIO_RINGCHECK  */

/*
  Experimental max loop current limit for the proslic
  Loop current limit is from 20 mA to 41 mA in steps of 3
  (according to datasheet)
  So set the value below to:
  0x00 : 20mA (default)
  0x01 : 23mA
  0x02 : 26mA
  0x03 : 29mA
  0x04 : 32mA
  0x05 : 35mA
  0x06 : 37mA
  0x07 : 41mA
*/

static int loopcurrent = 20;
#define POLARITY_XOR (\
		(reversepolarity != 0) ^ (fxs->reversepolarity != 0) ^\
		(fxs->vmwi_lrev != 0) ^\
		((fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVAC) != 0))

static int reversepolarity = 0;

static alpha  indirect_regs[] =
{
{0,255,"DTMF_ROW_0_PEAK",0x55C2},
{1,255,"DTMF_ROW_1_PEAK",0x51E6},
{2,255,"DTMF_ROW2_PEAK",0x4B85},
{3,255,"DTMF_ROW3_PEAK",0x4937},
{4,255,"DTMF_COL1_PEAK",0x3333},
{5,255,"DTMF_FWD_TWIST",0x0202},
{6,255,"DTMF_RVS_TWIST",0x0202},
{7,255,"DTMF_ROW_RATIO_TRES",0x0198},
{8,255,"DTMF_COL_RATIO_TRES",0x0198},
{9,255,"DTMF_ROW_2ND_ARM",0x0611},
{10,255,"DTMF_COL_2ND_ARM",0x0202},
{11,255,"DTMF_PWR_MIN_TRES",0x00E5},
{12,255,"DTMF_OT_LIM_TRES",0x0A1C},
{13,0,"OSC1_COEF",0x7B30},
{14,1,"OSC1X",0x0063},
{15,2,"OSC1Y",0x0000},
{16,3,"OSC2_COEF",0x7870},
{17,4,"OSC2X",0x007D},
{18,5,"OSC2Y",0x0000},
{19,6,"RING_V_OFF",0x0000},
{20,7,"RING_OSC",0x7EF0},
{21,8,"RING_X",0x0160},
{22,9,"RING_Y",0x0000},
{23,255,"PULSE_ENVEL",0x2000},
{24,255,"PULSE_X",0x2000},
{25,255,"PULSE_Y",0x0000},
//{26,13,"RECV_DIGITAL_GAIN",0x4000},	// playback volume set lower
{26,13,"RECV_DIGITAL_GAIN",0x2000},	// playback volume set lower
{27,14,"XMIT_DIGITAL_GAIN",0x4000},
//{27,14,"XMIT_DIGITAL_GAIN",0x2000},
{28,15,"LOOP_CLOSE_TRES",0x1000},
{29,16,"RING_TRIP_TRES",0x3600},
{30,17,"COMMON_MIN_TRES",0x1000},
{31,18,"COMMON_MAX_TRES",0x0200},
{32,19,"PWR_ALARM_Q1Q2",0x07C0},
{33,20,"PWR_ALARM_Q3Q4",0x2600},
{34,21,"PWR_ALARM_Q5Q6",0x1B80},
{35,22,"LOOP_CLOSURE_FILTER",0x8000},
{36,23,"RING_TRIP_FILTER",0x0320},
{37,24,"TERM_LP_POLE_Q1Q2",0x008C},
{38,25,"TERM_LP_POLE_Q3Q4",0x0100},
{39,26,"TERM_LP_POLE_Q5Q6",0x0010},
{40,27,"CM_BIAS_RINGING",0x0C00},
{41,64,"DCDC_MIN_V",0x0C00},
{42,255,"DCDC_XTRA",0x1000},
{43,66,"LOOP_CLOSE_TRES_LOW",0x1000},
};

#include <dahdi/kernel.h>
#include <dahdi/wctdm_user.h>

#include "fxo_modes.h"

#define DAWN_WR

#define NUM_FXO_REGS 60

#define WC_MAX_IFACES 128

#define DAWN_VERSION 0x9000
#define DAWN_DATE 0x9008
#define DAWN_TEST 0x9010

#define DAWN_SPI_RST 0x9018

#define DAWN_SPI0_SDTA 0x4000
#define DAWN_SPI0_RDTA 0x4008
#define DAWN_SPI0_BUSY 0x4010
#define DAWN_SPI0_WAIT 0x4018
#define DAWN_SPI0_TTLCLK 0x4020
#define DAWN_SPI0_RATECLK 0x4028
#define DAWN_SPI0_CS 0x4030

#define DAWN_SPI1_SDTA 0x5000
#define DAWN_SPI1_RDTA 0x5008
#define DAWN_SPI1_BUSY 0x5010
#define DAWN_SPI1_WAIT 0x5018
#define DAWN_SPI1_TTLCLK 0x5020
#define DAWN_SPI1_RATECLK 0x5028
#define DAWN_SPI1_CS 0x5030

#define DAWN_INTSTAT 	0x9108
#define DAWN_DMACNT 		0x9110
#define DAWN_MASK0   	0x9100

//TDM BUS 0
#define DAWN_DMAMOD 	0x0000
#define DAWN_DMAWS	0x0008
#define DAWN_DMAWI	0x0010
#define DAWN_DMAWE	0x0018
#define DAWN_DMAWC	0x0020
#define DAWN_DMARS	0x0028
#define DAWN_DMARI	0x0030
#define DAWN_DMARE	0x0038
#define DAWN_DMARC	0x0040
#define DAWN_PCMCTL 	0x0048
#define DAWN_MODE     0x0080

//TDM BUS 1
#define DAWN_DMAMOD1 	0x800000
#define DAWN_DMAWS1	0x800008
#define DAWN_DMAWI1	0x800010
#define DAWN_DMAWE1	0x800018
#define DAWN_DMAWC1	0x800020
#define DAWN_DMARS1	0x800028
#define DAWN_DMARI1	0x800030
#define DAWN_DMARE1	0x800038
#define DAWN_DMARC1	0x800040
#define DAWN_PCMCTL1 	0x800048
#define DAWN_MODE1     0x800080


#define FLAG_EMPTY	0
#define FLAG_WRITE	1
#define FLAG_READ	2

#define DEFAULT_RING_DEBOUNCE	64		/* Ringer Debounce (64 ms) */

#define POLARITY_DEBOUNCE 	64		/* Polarity debounce (64 ms) */

#define OHT_TIMER		6000	/* How long after RING to retain OHT */

/* NEON MWI pulse width - Make larger for longer period time
 * For more information on NEON MWI generation using the proslic
 * refer to Silicon Labs App Note "AN33-SI321X NEON FLASHING"
 * RNGY = RNGY 1/2 * Period * 8000
 */
#define NEON_MWI_RNGY_PULSEWIDTH	0x3e8	/*=> period of 250 mS */

#define FLAG_3215	(1 << 0)

#define NUM_CARDS 4
#define TDMBUS 2

#define MAX_ALARMS 10

#define MOD_TYPE_FXS	0
#define MOD_TYPE_FXO	1

#define MINPEGTIME	10 * 8		/* 30 ms peak to peak gets us no more than 100 Hz */
#define PEGTIME		50 * 8		/* 50ms peak to peak gets us rings of 10 Hz or more */
#define PEGCOUNT	5		/* 5 cycles of pegging means RING */

#define NUM_CAL_REGS 12


struct calregs {
	unsigned char vals[NUM_CAL_REGS];
};

enum proslic_power_warn {
	PROSLIC_POWER_UNKNOWN = 0,
	PROSLIC_POWER_ON,
	PROSLIC_POWER_WARNED,
};

enum battery_state {
	BATTERY_UNKNOWN = 0,
	BATTERY_PRESENT,
	BATTERY_LOST,
};

struct wctdm {
	struct dawn_base *bdev;//point back to dawn_base
	int busidx;
	struct pci_dev *dev;
	char *variety;
	const char *name;
	struct dahdi_span span;
	struct dahdi_device *ddev;
	struct dahdi_echocan_state ec[4];
	unsigned char ios;
	int usecount;
	unsigned int intcount;
	int dead;
	int pos;
	int flags[NUM_CARDS];
	int freeregion;
	int alt;
	int curcard;
	int cardflag;		/* Bit-map of present cards */
	enum proslic_power_warn proslic_power;
	spinlock_t lock;
	int irq;

	union {
		struct fxo {
#ifdef AUDIO_RINGCHECK
			unsigned int pegtimer;
			int pegcount;
			int peg;
			int ring;
#else
			int wasringing;
			int lastrdtx;
#endif
			int ringdebounce;
			int offhook;
			unsigned int battdebounce;
			unsigned int battalarm;
			enum battery_state battery;
		        int lastpol;
		        int polarity;
		        int polaritydebounce;
		} fxo;
		struct fxs {
			int oldrxhook;
			int debouncehook;
			int lastrxhook;
			int debounce;
			int ohttimer;
			int idletxhookstate;		/* IDLE changing hook state */
			int lasttxhook;
			int palarms;
			int reversepolarity;		/* Reverse Line */
			int mwisendtype;
			struct dahdi_vmwi_info vmwisetting;
			int vmwi_active_messages;
			u32 vmwi_lrev:1; /*MWI Line Reversal*/
			u32 vmwi_hvdc:1; /*MWI High Voltage DC Idle line*/
			u32 vmwi_hvac:1; /*MWI Neon High Voltage AC Idle line*/
			u32 neonringing:1; /*Ring Generator is set for NEON*/
			struct calregs calregs;
		} fxs;
	} mod[NUM_CARDS];

	/* Receive hook state and debouncing */
	int modtype[NUM_CARDS];
	unsigned char reg0shadow[NUM_CARDS];
	unsigned char reg1shadow[NUM_CARDS];

	void __iomem *ioaddr;
	dma_addr_t 	readdma;
	dma_addr_t	writedma;
	volatile unsigned char *writechunk;				/* Double-word aligned write memory */
	volatile unsigned char *readchunk;				/* Double-word aligned read memory */
	struct dahdi_chan *chans[NUM_CARDS];
};

struct dawn_base {
	struct pci_dev *dev;
	spinlock_t lock;
	int irq;
	int pos;
	int curcard;
	int cardflag;		/* Bit-map of present cards */
	struct dahdi_span span;
	struct dahdi_device *ddev;

	struct wctdm *wc;
	void __iomem *ioaddr;
};

struct wctdm_desc {
	char *name;
	int flags;
};

//one Dawn card has two tdm buses
static struct dawn_base * dawn_card;


//static struct wctdm_desc wctdm = { "SwitchPi Dawn", 0 };
static int acim2tiss[16] = { 0x0, 0x1, 0x4, 0x5, 0x7, 0x0, 0x0, 0x6, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3 };

static struct dawn_base *ifaces[WC_MAX_IFACES];

static void wctdm_release(struct wctdm *wc);

static unsigned int fxovoltage;
static unsigned int battdebounce;
static unsigned int battalarm;
static unsigned int battthresh;
static int ringdebounce = DEFAULT_RING_DEBOUNCE;
/* times 4, because must be a multiple of 4ms: */
static int dialdebounce = 8 * 8;
static int fwringdetect = 0;
static int debug = 0;
static int robust = 0;
static int timingonly = 0;
static int lowpower = 0;
static int boostringer = 0;
static int fastringer = 0;
static int _opermode = 0;
static char *opermode = "FCC";
static int fxshonormode = 0;
static int alawoverride = 0;
static int fastpickup = 0;
static int fxotxgain = 0;
static int fxorxgain = 0;
static int fxstxgain = 0;
static int fxsrxgain = 0;


static int wctdm_init_proslic(struct wctdm *wc, int card, int fast , int manual, int sane);
static int wctdm_init_ring_generator_mode(struct wctdm *wc, int card);
static int wctdm_set_ring_generator_mode(struct wctdm *wc, int card, int mode);
DAHDI_IRQ_HANDLER(wctdm_interrupt);

static int chanmap_ana[] = { 0,1,2,3,4,5,6,7,8,9,10,11,
  12,13,14,15,16,17,18,19,20,
  21,22,23,24,25,26,27,28,29,30,31};

static int codechan_map[] = { 3,2,1,0};

int wcnt, wcnt1, wcnt00, wcnt01, wcnt10, wcnt11;

static inline void wctdm_transmitprep(struct wctdm *wc, unsigned char ints)
{
	volatile unsigned char *writechunk;
	int x,y;
	int pos;
	if (wc->busidx == 0) {
		if (ints & 0x01){
			/* Write is at interrupt address.  Start writing from normal offset */
			writechunk = wc->writechunk;
			wcnt00++;
		}
		else{
			writechunk = wc->writechunk + (DAHDI_CHUNKSIZE * 32);
			wcnt01++;
		}
		/* Calculate Transmission */
		dahdi_transmit(&wc->span);

		for (y=0;y<DAHDI_CHUNKSIZE;y++) {
			for (x=0;x<32;x++) {
				pos = y * 32 + chanmap_ana[x];
				if (x <4) writechunk[pos] = wc->chans[x]->writechunk[y];
				else writechunk[pos] = 0xff;
			}
		}
	}else {
		if (ints & 0x10){
			/* Write is at interrupt address.  Start writing from normal offset */
			writechunk = wc->writechunk;
			wcnt10++;
		}
		else{
			writechunk = wc->writechunk + (DAHDI_CHUNKSIZE * 32);
			wcnt11++;
		}
		/* Calculate Transmission */
		dahdi_transmit(&wc->span);

		for (y=0;y<DAHDI_CHUNKSIZE;y++) {
			for (x=0;x<32;x++) {
				pos = y * 32 + chanmap_ana[x];
				if (x <4) writechunk[pos] = wc->chans[x]->writechunk[y];
				else writechunk[pos] = 0xff;
				//writechunk[pos] = 0xcd;
				//mycnt = wc->chans[x]->writechunk[y];
			}
		}
	}
}

#ifdef AUDIO_RINGCHECK
static inline void ring_check(struct wctdm *wc, int card)
{
	int x;
	short sample;
	if (wc->modtype[card] != MOD_TYPE_FXO)
		return;
	wc->mod[card].fxo.pegtimer += DAHDI_CHUNKSIZE;
	for (x=0;x<DAHDI_CHUNKSIZE;x++) {
		/* Look for pegging to indicate ringing */
		sample = DAHDI_XLAW(wc->chans[card].readchunk[x], (&(wc->chans[card])));
		if ((sample > 10000) && (wc->mod[card].fxo.peg != 1)) {
			if (debug > 1) printk(KERN_DEBUG "High peg!\n");
			if ((wc->mod[card].fxo.pegtimer < PEGTIME) && (wc->mod[card].fxo.pegtimer > MINPEGTIME))
				wc->mod[card].fxo.pegcount++;
			wc->mod[card].fxo.pegtimer = 0;
			wc->mod[card].fxo.peg = 1;
		} else if ((sample < -10000) && (wc->mod[card].fxo.peg != -1)) {
			if (debug > 1) printk(KERN_DEBUG "Low peg!\n");
			if ((wc->mod[card].fxo.pegtimer < (PEGTIME >> 2)) && (wc->mod[card].fxo.pegtimer > (MINPEGTIME >> 2)))
				wc->mod[card].fxo.pegcount++;
			wc->mod[card].fxo.pegtimer = 0;
			wc->mod[card].fxo.peg = -1;
		}
	}
	if (wc->mod[card].fxo.pegtimer > PEGTIME) {
		/* Reset pegcount if our timer expires */
		wc->mod[card].fxo.pegcount = 0;
	}
	/* Decrement debouncer if appropriate */
	if (wc->mod[card].fxo.ringdebounce)
		wc->mod[card].fxo.ringdebounce--;
	if (!wc->mod[card].fxo.offhook && !wc->mod[card].fxo.ringdebounce) {
		if (!wc->mod[card].fxo.ring && (wc->mod[card].fxo.pegcount > PEGCOUNT)) {
			/* It's ringing */
			if (debug)
				printk(KERN_DEBUG "RING on %d/%d!\n", wc->span.spanno, card + 1);
			if (!wc->mod[card].fxo.offhook)
				dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_RING);
			wc->mod[card].fxo.ring = 1;
		}
		if (wc->mod[card].fxo.ring && !wc->mod[card].fxo.pegcount) {
			/* No more ring */
			if (debug)
				printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->span.spanno, card + 1);
			dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
			wc->mod[card].fxo.ring = 0;
		}
	}
}
#endif
static inline void wctdm_receiveprep(struct wctdm *wc, unsigned char ints)
{
	volatile unsigned char *readchunk;
	int x,y;
	int pos;

	if (wc->busidx == 0) {
		if (ints & 0x01) {
			//readchunk = wc->readchunk + (DAHDI_CHUNKSIZE*4);
			readchunk = wc->readchunk;
		}
		else{
			/* Read is at interrupt address.  Valid data is available at normal offset */
			readchunk = wc->readchunk + (DAHDI_CHUNKSIZE*32);
			//readchunk = wc->readchunk;
		}
		for (y=0;y<DAHDI_CHUNKSIZE;y++) {
			for (x=0;x<32;x++) {
				pos = y * 32 + chanmap_ana[x];
				if (x <4) wc->chans[x]->readchunk[y] = readchunk[pos];
			}
		}
	#ifdef AUDIO_RINGCHECK
		for (x=0;x<wc->cards;x++)
			ring_check(wc, x);
	#endif
		/* XXX We're wasting 8 taps.  We should get closer :( */
		for (x = 0; x < NUM_CARDS; x++) {
			if (wc->cardflag & (1 << x))
				dahdi_ec_chunk(wc->chans[x], wc->chans[x]->readchunk, wc->chans[x]->writechunk);
		}
		dahdi_receive(&wc->span);
	}else {
		if (ints & 0x10) {
			//readchunk = wc->readchunk + (DAHDI_CHUNKSIZE*4);
			readchunk = wc->readchunk;
		}
		else{
			/* Read is at interrupt address.  Valid data is available at normal offset */
			readchunk = wc->readchunk + (DAHDI_CHUNKSIZE*32);
		}
		for (y=0;y<DAHDI_CHUNKSIZE;y++) {
			for (x=0;x<32;x++) {
				pos = y * 32 + chanmap_ana[x];
				if (x <4) wc->chans[x]->readchunk[y] = readchunk[pos];
			}
		}
	#ifdef AUDIO_RINGCHECK
		for (x=0;x<wc->cards;x++)
			ring_check(wc, x);
	#endif
		/* XXX We're wasting 8 taps.  We should get closer :( */
		for (x = 0; x < NUM_CARDS; x++) {
			if (wc->cardflag & (1 << x))
				dahdi_ec_chunk(wc->chans[x], wc->chans[x]->readchunk, wc->chans[x]->writechunk);
		}
		dahdi_receive(&wc->span);
	}
}

static void wctdm_stop_dma(struct wctdm *wc);
static void wctdm_reset_tdm(struct wctdm *wc);
static void wctdm_restart_dma(struct wctdm *wc);

unsigned int dawn_read(struct wctdm *wc, int reg)
{
	int val=0;
	unsigned long flags=0;
	spin_lock_irqsave(&wc->bdev->lock, flags);
	val = readl(wc->bdev->ioaddr + reg);
	//printk("Read out %x from %x\n", val, reg);
	spin_unlock_irqrestore(&wc->bdev->lock, flags);
	return val;
}

void dawn_write(struct wctdm *wc, int reg, unsigned value)
{
	unsigned long flags=0;
	spin_lock_irqsave(&wc->bdev->lock, flags);
	writel(value, wc->bdev->ioaddr + reg);
	spin_unlock_irqrestore(&wc->bdev->lock, flags);
}

void spi_reset(struct wctdm *wc){
		dawn_write(wc, DAWN_SPI_RST,0xf);//set modle_rst
		udelay (200);
		dawn_write(wc, DAWN_SPI_RST,0x0);//clear module_rst
		udelay (500);
		dawn_write(wc, DAWN_SPI_RST,0xf);//set modle_rst
		udelay(1000);
}

void spi_initial(struct wctdm *wc){
	if (wc->busidx == 0) {
		dawn_write(wc, DAWN_SPI0_RATECLK, 0x4);
	}
	if (wc->busidx == 1) {
		dawn_write(wc, DAWN_SPI1_RATECLK, 0x4);
	}
}

void dma_initial(struct wctdm *wc) {
	if (wc->busidx == 0) {
		dawn_write(wc, DAWN_DMAMOD, 0x00);
		dawn_write(wc, DAWN_PCMCTL, 0xC100);
		dawn_write(wc, DAWN_MODE, 0x1);
		dawn_write(wc, 0x9030, 0x0);

		/* Setup DMA Addresses */
		dawn_write(wc, DAWN_DMARS, wc->writedma); /* Write start */
		dawn_write(wc, DAWN_DMARI, wc->writedma + 8 * 32 -4); /* Middle (interrupt) */
		dawn_write(wc, DAWN_DMARE, wc->writedma + 8 * 32 * 2 -4); /* End */

		dawn_write(wc, DAWN_DMAWS, wc->readdma);	/* Read start */
		dawn_write(wc, DAWN_DMAWI, wc->readdma + 8 * 32 -4);	/* Middle (interrupt) */
		dawn_write(wc, DAWN_DMAWE, wc->readdma + 8 * 32 * 2 -4);	/* End */
	}
	if (wc->busidx == 1) {
		dawn_write(wc, DAWN_DMAMOD1, 0x00);
		dawn_write(wc, DAWN_PCMCTL1, 0xC100);
		dawn_write(wc, DAWN_MODE1, 0x1);
		dawn_write(wc, 0x9030, 0x0);

		/* Setup DMA Addresses */
		dawn_write(wc, DAWN_DMARS1, wc->writedma); /* Write start */
		dawn_write(wc, DAWN_DMARI1, wc->writedma + 8 * 32 -4); /* Middle (interrupt) */
		dawn_write(wc, DAWN_DMARE1, wc->writedma + 8 * 32 * 2 -4); /* End */

		dawn_write(wc, DAWN_DMAWS1, wc->readdma);	/* Read start */
		dawn_write(wc, DAWN_DMAWI1, wc->readdma + 8 * 32 -4);	/* Middle (interrupt) */
		dawn_write(wc, DAWN_DMAWE1, wc->readdma + 8 * 32 * 2 -4);	/* End */
	}
	dawn_write(wc, DAWN_INTSTAT, 0xff);//or ff, need try it
}

static inline void __write_8bits(struct wctdm *wc, unsigned char bits)
{
	if (wc->busidx == 0) {
		dawn_write(wc, DAWN_SPI0_SDTA, bits);
	}
	if (wc->busidx == 1)  {
		dawn_write(wc, DAWN_SPI1_SDTA, bits);
	}
	udelay(5);
}

static inline unsigned char __read_8bits(struct wctdm *wc)
{
	unsigned char ret;
	if (wc->busidx == 0) {
		dawn_write(wc, DAWN_SPI0_SDTA, 0xff);
		udelay(5);
		ret = dawn_read(wc, DAWN_SPI0_RDTA);
	}
	if (wc->busidx == 1)  {
		dawn_write(wc, DAWN_SPI1_SDTA, 0xff);
		udelay(5);
		ret = dawn_read(wc, DAWN_SPI1_RDTA);
	}
	return ret;
}


static inline void __wctdm_setcard(struct wctdm *wc, int card)
{
	if (wc->curcard != card) {
		if (wc->busidx == 0) {
			dawn_write(wc, DAWN_SPI0_CS, ~(1 << card));
		}
		if (wc->busidx == 1)  {
			dawn_write(wc, DAWN_SPI1_CS, ~(1 << card));
		}
		wc->curcard = card;
	}
}

static void __wctdm_setreg(struct wctdm *wc, int card, unsigned char reg, unsigned char value)
{
	__wctdm_setcard(wc, card);
	if (wc->modtype[card] == MOD_TYPE_FXO) {
		__write_8bits(wc, 0x20);
		__write_8bits(wc, (reg & 0x7f));
	} else {
		__write_8bits(wc, (reg & 0x7f));
	}
	__write_8bits(wc, value);
}

static void wctdm_setreg(struct wctdm *wc, int card, unsigned char reg, unsigned char value)
{
	//unsigned long flags;
	//spin_lock_irqsave(&wc->lock, flags);
	__wctdm_setreg(wc, card, reg, value);
	//spin_unlock_irqrestore(&wc->lock, flags);
}

static unsigned char __wctdm_getreg(struct wctdm *wc, int card, unsigned char reg)
{
	__wctdm_setcard(wc, card);
	if (wc->modtype[card] == MOD_TYPE_FXO) {
		__write_8bits(wc, 0x60);
		__write_8bits(wc, reg & 0x7f);
	} else {
		__write_8bits(wc, reg | 0x80);
	}
	return __read_8bits(wc);
}


static unsigned char wctdm_getreg(struct wctdm *wc, int card, unsigned char reg)
{
	//unsigned long flags;
	unsigned char res;
	//spin_lock_irqsave(&wc->lock, flags);
	res = __wctdm_getreg(wc, card, reg);
	//spin_unlock_irqrestore(&wc->lock, flags);
	return res;
}


static struct proc_dir_entry *proc_frame;

static int frame_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "-----TDM BUS0 CNT %d\n", wcnt);
  	seq_printf(m, "-----TDM BUS1 CNT %d\n", wcnt1);
    seq_printf(m, "-----TDM BUS0 WCNT00 %d\n", wcnt00);
  	seq_printf(m, "-----TDM BUS1 WCNT01 %d\n", wcnt01);
    seq_printf(m, "-----TDM BUS0 WCNT10 %d\n", wcnt10);
  	seq_printf(m, "-----TDM BUS1 WCNT11 %d\n", wcnt11);
	return 0;
}

static int frame_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, frame_proc_show, NULL);
}

static const struct file_operations frame_proc_ops = {
	.open		= frame_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void dma_proc_create(void)
{
	proc_frame = proc_create("frame", 0, NULL, &frame_proc_ops);
}

static int __wait_access(struct wctdm *wc, int card)
{
    unsigned char data = 0;
    int count = 0;

    #define MAX 6000 /* attempts */


    /* Wait for indirect access */
    while (count++ < MAX)
	 {
		data = __wctdm_getreg(wc, card, I_STATUS);

		if (!data)
			return 0;

	 }

    if(count > (MAX-1)) printk(KERN_NOTICE " ##### Loop error (%02x) #####\n", data);

	return 0;
}

static unsigned char translate_3215(unsigned char address)
{
	int x;
	for (x=0;x<sizeof(indirect_regs)/sizeof(indirect_regs[0]);x++) {
		if (indirect_regs[x].address == address) {
			address = indirect_regs[x].altaddr;
			break;
		}
	}
	return address;
}

static int wctdm_proslic_setreg_indirect(struct wctdm *wc, int card, unsigned char address, unsigned short data)
{
	unsigned long flags;
	int res = -1;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if(!__wait_access(wc, card)) {
		__wctdm_setreg(wc, card, IDA_LO,(unsigned char)(data & 0xFF));
		__wctdm_setreg(wc, card, IDA_HI,(unsigned char)((data & 0xFF00)>>8));
		__wctdm_setreg(wc, card, IAA,address);
		res = 0;
	};
	spin_unlock_irqrestore(&wc->lock, flags);
	return res;
}

static int wctdm_proslic_getreg_indirect(struct wctdm *wc, int card, unsigned char address)
{
	unsigned long flags;
	int res = -1;
	char *p=NULL;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if (!__wait_access(wc, card)) {
		__wctdm_setreg(wc, card, IAA, address);
		if (!__wait_access(wc, card)) {
			unsigned char data1, data2;
			data1 = __wctdm_getreg(wc, card, IDA_LO);
			data2 = __wctdm_getreg(wc, card, IDA_HI);
			res = data1 | (data2 << 8);
		} else
			p = "Failed to wait inside\n";
	} else
		p = "failed to wait\n";
	spin_unlock_irqrestore(&wc->lock, flags);
	if (p)
		printk(KERN_NOTICE "%s", p);
	return res;
}

static int wctdm_proslic_init_indirect_regs(struct wctdm *wc, int card)
{
	unsigned char i;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++)
	{
		if(wctdm_proslic_setreg_indirect(wc, card, indirect_regs[i].address,indirect_regs[i].initial))
			return -1;
	}

	return 0;
}

static int wctdm_proslic_verify_indirect_regs(struct wctdm *wc, int card)
{
	int passed = 1;
	unsigned short i, initial;
	int j;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++)
	{
		if((j = wctdm_proslic_getreg_indirect(wc, card, (unsigned char) indirect_regs[i].address)) < 0) {
			printk(KERN_NOTICE "Failed to read indirect register %d\n", i);
			return -1;
		}
		initial= indirect_regs[i].initial;

		if ( j != initial && (!(wc->flags[card] & FLAG_3215) || (indirect_regs[i].altaddr != 255)))
		{
			 printk(KERN_NOTICE "!!!!!!! %s  iREG %X = %X  should be %X\n",
				indirect_regs[i].name,indirect_regs[i].address,j,initial );
			 passed = 0;
		}
	}

    if (passed) {
		if (debug)
			printk(KERN_DEBUG "Init Indirect Registers completed successfully.\n");
    } else {
		printk(KERN_NOTICE " !!!!! Init Indirect Registers UNSUCCESSFULLY.\n");
		return -1;
    }
    return 0;
}

static inline void wctdm_proslic_recheck_sanity(struct wctdm *wc, int card)
{
	struct fxs *const fxs = &wc->mod[card].fxs;
	int res;
	/* Check loopback */
	res = wc->reg1shadow[card];
	if (!res && (res != fxs->lasttxhook)) {
		res = wctdm_getreg(wc, card, 8);
		if (res) {
			printk(KERN_NOTICE "Ouch, part reset, quickly restoring reality (%d)\n", card);
			wctdm_init_proslic(wc, card, 1, 0, 1);
		} else {
			if (fxs->palarms++ < MAX_ALARMS) {
				printk(KERN_NOTICE "Power alarm on module %d, resetting!\n", card + 1);
				if (fxs->lasttxhook == SLIC_LF_RINGING)
					fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
				wctdm_setreg(wc, card, 64, fxs->lasttxhook);
			} else {
				if (fxs->palarms == MAX_ALARMS)
					printk(KERN_NOTICE "Too many power alarms on card %d, NOT resetting!\n", card + 1);
			}
		}
	}
}

static inline void wctdm_voicedaa_check_hook(struct wctdm *wc, int card)
{
#define MS_PER_CHECK_HOOK 16

#ifndef AUDIO_RINGCHECK
	unsigned char res;
#endif
	signed char b;
	int errors = 0;
	struct fxo *fxo = &wc->mod[card].fxo;

	/* Try to track issues that plague slot one FXO's */
	b = wc->reg0shadow[card];
	if ((b & 0x2) || !(b & 0x8)) {
		/* Not good -- don't look at anything else */
		if (debug)
			printk(KERN_DEBUG "Error (%02x) on card %d!\n", b, card + 1);
		errors++;
	}
	b &= 0x9b;
	if (fxo->offhook) {
		if (b != 0x9)
			wctdm_setreg(wc, card, 5, 0x9);
	} else {
		if (b != 0x8)
			wctdm_setreg(wc, card, 5, 0x8);
	}
	if (errors)
		return;
	if (!fxo->offhook) {
		if (fwringdetect) {
			res = wc->reg0shadow[card] & 0x60;
			if (fxo->ringdebounce) {
				--fxo->ringdebounce;
				if (res && (res != fxo->lastrdtx) &&
				    (fxo->battery == BATTERY_PRESENT)) {
					if (!fxo->wasringing) {
						fxo->wasringing = 1;
						if (debug)
							printk(KERN_DEBUG "RING on %d/%d!\n", wc->span.spanno, card + 1);
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_RING);
					}
					fxo->lastrdtx = res;
					fxo->ringdebounce = 10;
				} else if (!res) {
					if ((fxo->ringdebounce == 0) && fxo->wasringing) {
						fxo->wasringing = 0;
						if (debug)
							printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->span.spanno, card + 1);
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
					}
				}
			} else if (res && (fxo->battery == BATTERY_PRESENT)) {
				fxo->lastrdtx = res;
				fxo->ringdebounce = 10;
			}
		} else {
			res = wc->reg0shadow[card];
			if ((res & 0x60) && (fxo->battery == BATTERY_PRESENT)) {
				fxo->ringdebounce += (DAHDI_CHUNKSIZE * 16);
				if (fxo->ringdebounce >= DAHDI_CHUNKSIZE * ringdebounce) {
					if (!fxo->wasringing) {
						fxo->wasringing = 1;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_RING);
						if (debug)
							printk(KERN_DEBUG "RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = DAHDI_CHUNKSIZE * ringdebounce;
				}
			} else {
				fxo->ringdebounce -= DAHDI_CHUNKSIZE * 4;
				if (fxo->ringdebounce <= 0) {
					if (fxo->wasringing) {
						fxo->wasringing = 0;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
						if (debug)
							printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = 0;
				}
			}
		}
	}

	b = wc->reg1shadow[card];

	if (fxovoltage) {
		static int count = 0;
		if (!(count++ % 100)) {
			printk(KERN_DEBUG "Card %d: Voltage: %d Debounce %d\n", card + 1, b, fxo->battdebounce);
		}
	}

	if (unlikely(DAHDI_RXSIG_INITIAL == wc->chans[card]->rxhooksig)) {
		/*
		 * dahdi-base will set DAHDI_RXSIG_INITIAL after a
		 * DAHDI_STARTUP or DAHDI_CHANCONFIG ioctl so that new events
		 * will be queued on the channel with the current received
		 * hook state.  Channels that use robbed-bit signalling always
		 * report the current received state via the dahdi_rbsbits
		 * call. Since we only call dahdi_hooksig when we've detected
		 * a change to report, let's forget our current state in order
		 * to force us to report it again via dahdi_hooksig.
		 *
		 */
		fxo->battery = BATTERY_UNKNOWN;
	}

	if (abs(b) < battthresh) {
		/* possible existing states:
		   battery lost, no debounce timer
		   battery lost, debounce timer (going to battery present)
		   battery present or unknown, no debounce timer
		   battery present or unknown, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_LOST) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_PRESENT, but battery was lost again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_LOST, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_LOST;
					if (debug)
						printk(KERN_DEBUG "NO BATTERY on %d/%d!\n", wc->span.spanno, card + 1);
#ifdef	JAPAN
					if (!wc->ohdebounce && wc->offhook) {
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
						if (debug)
							printk(KERN_DEBUG "Signalled On Hook\n");
#ifdef	ZERO_BATT_RING
						wc->onhook++;
#endif
					}
#else
					dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
#endif
				}
			} else {
				/* start the debounce timer to verify that battery has been lost */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	} else {
		/* possible existing states:
		   battery lost or unknown, no debounce timer
		   battery lost or unknown, debounce timer (going to battery present)
		   battery present, no debounce timer
		   battery present, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_PRESENT) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_LOST, but battery appeared again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_PRESENT, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_PRESENT;
					if (debug)
						printk(KERN_DEBUG "BATTERY on %d/%d (%s)!\n", wc->span.spanno, card + 1,
						       (b < 0) ? "-" : "+");
#ifdef	ZERO_BATT_RING
					if (wc->onhook) {
						wc->onhook = 0;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
						if (debug)
							printk(KERN_DEBUG "Signalled Off Hook\n");
					}
#else
					dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
#endif
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
				}
			} else {
				/* start the debounce timer to verify that battery has appeared */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	}

	if (fxo->lastpol >= 0) {
		if (b < 0) {
			fxo->lastpol = -1;
			fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
		}
	}
	if (fxo->lastpol <= 0) {
		if (b > 0) {
			fxo->lastpol = 1;
			fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
		}
	}

	if (fxo->battalarm) {
		if (--fxo->battalarm == 0) {
			/* the alarm timer has expired, so update the battery alarm state
			   for this channel */
			dahdi_alarm_channel(wc->chans[card], fxo->battery == BATTERY_LOST ? DAHDI_ALARM_RED : DAHDI_ALARM_NONE);
		}
	}

	if (fxo->polaritydebounce) {
		if (--fxo->polaritydebounce == 0) {
		    if (fxo->lastpol != fxo->polarity) {
				if (debug)
					printk(KERN_DEBUG "%lu Polarity reversed (%d -> %d)\n", jiffies,
				       fxo->polarity,
				       fxo->lastpol);
				if (fxo->polarity)
					dahdi_qevent_lock(wc->chans[card], DAHDI_EVENT_POLARITY);
				fxo->polarity = fxo->lastpol;
		    }
		}
	}
#undef MS_PER_CHECK_HOOK
}

static void wctdm_fxs_hooksig(struct wctdm *wc, const int card, enum dahdi_txsig txsig)
{
	struct fxs *const fxs = &wc->mod[card].fxs;
	switch (txsig) {
	case DAHDI_TXSIG_ONHOOK:
		switch (wc->span.chans[card]->sig) {
		case DAHDI_SIG_FXOKS:
		case DAHDI_SIG_FXOLS:
			/* Can't change Ring Generator during OHT */
			if (!fxs->ohttimer) {
				wctdm_set_ring_generator_mode(wc,
					    card, fxs->vmwi_hvac);
				fxs->lasttxhook = fxs->vmwi_hvac ?
						SLIC_LF_RINGING :
						fxs->idletxhookstate;
			} else {
				fxs->lasttxhook = fxs->idletxhookstate;
			}
			break;
		case DAHDI_SIG_EM:
			fxs->lasttxhook = fxs->idletxhookstate;
			break;
		case DAHDI_SIG_FXOGS:
			fxs->lasttxhook = SLIC_LF_TIP_OPEN;
			break;
		}
		break;
	case DAHDI_TXSIG_OFFHOOK:
		switch (wc->span.chans[card]->sig) {
		case DAHDI_SIG_EM:
			fxs->lasttxhook = SLIC_LF_ACTIVE_REV;
			break;
		default:
			fxs->lasttxhook = fxs->idletxhookstate;
			break;
		}
		break;
	case DAHDI_TXSIG_START:
		/* Set ringer mode */
		wctdm_set_ring_generator_mode(wc, card, 0);
		fxs->lasttxhook = SLIC_LF_RINGING;
		break;
	case DAHDI_TXSIG_KEWL:
		fxs->lasttxhook = SLIC_LF_OPEN;
		break;
	default:
		printk(KERN_NOTICE "wctdm: Can't set tx state to %d\n", txsig);
		return;
	}
	if (debug) {
		printk(KERN_DEBUG
		       "Setting FXS hook state to %d (%02x)\n",
		       txsig, fxs->lasttxhook);
	}
	wctdm_setreg(wc, card, LINE_STATE, fxs->lasttxhook);
}

static inline void wctdm_proslic_check_hook(struct wctdm *wc, int card)
{
	struct fxs *const fxs = &wc->mod[card].fxs;
	char res;
	int hook;

	/* For some reason we have to debounce the
	   hook detector.  */

	res = wc->reg0shadow[card];
	hook = (res & 1);
	if (hook != fxs->lastrxhook) {
		/* Reset the debounce (must be multiple of 4ms) */
		fxs->debounce = dialdebounce * 4;
#if 0
		printk(KERN_DEBUG "Resetting debounce card %d hook %d, %d\n",
		       card, hook, fxs->debounce);
#endif
	} else {
		if (fxs->debounce > 0) {
			fxs->debounce -= 16 * DAHDI_CHUNKSIZE;
#if 0
			printk(KERN_DEBUG "Sustaining hook %d, %d\n",
			       hook, fxs->debounce);
#endif
			if (!fxs->debounce) {
#if 0
				printk(KERN_DEBUG "Counted down debounce, newhook: %d...\n", hook);
#endif
				fxs->debouncehook = hook;
			}
			if (!fxs->oldrxhook && fxs->debouncehook) {
				/* Off hook */
#if 1
				if (debug)
#endif
					printk(KERN_DEBUG "wctdm: Card %d Going off hook\n", card);

				switch (fxs->lasttxhook) {
				case SLIC_LF_RINGING:
				case SLIC_LF_OHTRAN_FWD:
				case SLIC_LF_OHTRAN_REV:
					/* just detected OffHook, during
					 * Ringing or OnHookTransfer */
					fxs->idletxhookstate =
						POLARITY_XOR ?
							SLIC_LF_ACTIVE_REV :
							SLIC_LF_ACTIVE_FWD;
					break;
				}

				wctdm_fxs_hooksig(wc, card, DAHDI_TXSIG_OFFHOOK);
				dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
				if (robust)
					wctdm_init_proslic(wc, card, 1, 0, 1);
				fxs->oldrxhook = 1;

			} else if (fxs->oldrxhook && !fxs->debouncehook) {
				/* On hook */
#if 1
				if (debug)
#endif
					printk(KERN_DEBUG "wctdm: Card %d Going on hook\n", card);
				wctdm_fxs_hooksig(wc, card, DAHDI_TXSIG_ONHOOK);
				dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
				fxs->oldrxhook = 0;
			}
		}
	}
	fxs->lastrxhook = hook;
}

void check_hook_status (struct wctdm *wc, unsigned char ints) {
	int x, mode;

	wc->intcount++;
	x = wc->intcount & 0x3;
	mode = wc->intcount & 0xc;
	if (wc->cardflag & (1 << x)) {
		switch(mode) {
		case 0:
			/* Rest */
			break;
		case 4:
			/* Read first shadow reg */
			if (wc->modtype[x] == MOD_TYPE_FXS)
				wc->reg0shadow[x] = wctdm_getreg(wc, x, 68);
			else if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->reg0shadow[x] = wctdm_getreg(wc, x, 5);
			break;
		case 8:
			/* Read second shadow reg */
			if (wc->modtype[x] == MOD_TYPE_FXS)
				wc->reg1shadow[x] = wctdm_getreg(wc, x, LINE_STATE);
			else if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->reg1shadow[x] = wctdm_getreg(wc, x, 29);
			break;
		case 12:
			/* Perform processing */
			if (wc->modtype[x] == MOD_TYPE_FXS) {
				wctdm_proslic_check_hook(wc, x);
				if (!(wc->intcount & 0xf0)) {
					wctdm_proslic_recheck_sanity(wc, x);
				}
			} else if (wc->modtype[x] == MOD_TYPE_FXO) {
				wctdm_voicedaa_check_hook(wc, x);
			}
			break;
		}
	}
	if (!(wc->intcount % 10000)) {
		/* Accept an alarm once per 10 seconds */
		for (x=0;x<4;x++)
			if (wc->modtype[x] == MOD_TYPE_FXS) {
				if (wc->mod[x].fxs.palarms)
					wc->mod[x].fxs.palarms--;
			}
	}
	wctdm_receiveprep(wc, ints);
	wctdm_transmitprep(wc, ints);
}

void check_ring_status (struct wctdm *wc) {
	int x;
	for (x=0;x<4;x++) {
		if (wc->cardflag & (1 << x) &&
		    (wc->modtype[x] == MOD_TYPE_FXS)) {
			struct fxs *const fxs = &wc->mod[x].fxs;
			if (fxs->lasttxhook == SLIC_LF_RINGING &&
						!fxs->neonringing) {
				/* RINGing, prepare for OHT */
				fxs->ohttimer = OHT_TIMER << 3;

				/* logical XOR 3 variables
				    module parameter 'reversepolarity', global reverse all FXS lines.
				    ioctl channel variable fxs 'reversepolarity', Line Reversal Alert Signal if required.
				    ioctl channel variable fxs 'vmwi_lrev', VMWI pending.
				 */

				/* OHT mode when idle */
				fxs->idletxhookstate = POLARITY_XOR ?
							SLIC_LF_OHTRAN_REV :
							SLIC_LF_OHTRAN_FWD;
			} else if (fxs->ohttimer) {
				/* check if still OnHook */
				if (!fxs->oldrxhook) {
					fxs->ohttimer -= DAHDI_CHUNKSIZE;
					if (!fxs->ohttimer) {
						fxs->idletxhookstate = POLARITY_XOR ? SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD; /* Switch to Active, Rev or Fwd */
						/* if currently OHT */
						if ((fxs->lasttxhook == SLIC_LF_OHTRAN_FWD) || (fxs->lasttxhook == SLIC_LF_OHTRAN_REV)) {
							if (fxs->vmwi_hvac) {
								/* force idle polarity Forward if ringing */
								fxs->idletxhookstate = SLIC_LF_ACTIVE_FWD;
								/* Set ring generator for neon */
								wctdm_set_ring_generator_mode(wc, x, 1);
								fxs->lasttxhook = SLIC_LF_RINGING;
							} else {
								fxs->lasttxhook = fxs->idletxhookstate;
							}
							/* Apply the change as appropriate */
							wctdm_setreg(wc, x, LINE_STATE, fxs->lasttxhook);
						}
					}
				} else {
					fxs->ohttimer = 0;
					/* Switch to Active, Rev or Fwd */
					fxs->idletxhookstate = POLARITY_XOR ? SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD;
				}
			}
		}
	}
}

DAHDI_IRQ_HANDLER(wctdm_interrupt)
{
	struct dawn_base *base = dev_id;
	struct wctdm *wc = NULL;
	unsigned char ints;

	ints = readl(base->ioaddr + DAWN_INTSTAT);

	if (!ints) {
		return IRQ_NONE;
	}

	writel(ints, base->ioaddr + DAWN_INTSTAT);

	if (ints & 0x03) {
		wcnt ++;
		//here we got tdm bus 0
		wc = &base->wc[0];
		check_ring_status(wc);
		check_hook_status(wc, ints);
	}
	if (ints & 0x30) {
		wcnt1 ++;
		//here we got tdm bus 1
		wc = &base->wc[1];
		check_ring_status(wc);
		check_hook_status(wc, ints);
	}

	return IRQ_RETVAL(1);
}

static int wctdm_voicedaa_insane(struct wctdm *wc, int card)
{
	int blah;
	blah = wctdm_getreg(wc, card, 2);
	if (blah != 0x3)
		return -2;
	blah = wctdm_getreg(wc, card, 11);
	if (debug)
		printk(KERN_DEBUG "VoiceDAA System: %02x\n", blah & 0xf);
	return 0;
}

static int wctdm_proslic_insane(struct wctdm *wc, int card)
{
	int blah,insane_report;
	insane_report=0;

	blah = wctdm_getreg(wc, card, 0);
	if (debug)
		printk(KERN_DEBUG "ProSLIC on module %d, product %d, version %d\n", card, (blah & 0x30) >> 4, (blah & 0xf));

#if 0
	if ((blah & 0x30) >> 4) {
		printk(KERN_DEBUG "ProSLIC on module %d is not a 3210.\n", card);
		return -1;
	}
#endif
	if (((blah & 0xf) == 0) || ((blah & 0xf) == 0xf)) {
		/* SLIC not loaded */
		return -1;
	}
	if ((blah & 0xf) < 2) {
		printk(KERN_NOTICE "ProSLIC 3210 version %d is too old\n", blah & 0xf);
		return -1;
	}
	if (wctdm_getreg(wc, card, 1) & 0x80)
		/* ProSLIC 3215, not a 3210 */
		wc->flags[card] |= FLAG_3215;

	blah = wctdm_getreg(wc, card, 8);
	if (blah != 0x2) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (1) %d should be 2\n", card, blah);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 8 Reads %d Expected is 0x2\n",card,blah);

	blah = wctdm_getreg(wc, card, 64);
	if (blah != 0x0) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (2)\n", card);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 64 Reads %d Expected is 0x0\n",card,blah);

	blah = wctdm_getreg(wc, card, 11);
	if (blah != 0x33) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (3)\n", card);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 11 Reads %d Expected is 0x33\n",card,blah);

	/* Just be sure it's setup right. */
	wctdm_setreg(wc, card, 30, 0);

	if (debug)
		printk(KERN_DEBUG "ProSLIC on module %d seems sane.\n", card);
	return 0;
}

static int wctdm_proslic_powerleak_test(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char vbat;

	/* Turn off linefeed */
	wctdm_setreg(wc, card, 64, 0);

	/* Power down */
	wctdm_setreg(wc, card, 14, 0x10);

	/* Wait for one second */
	origjiffies = jiffies;

	while((vbat = wctdm_getreg(wc, card, 82)) > 0x6) {
		if ((jiffies - origjiffies) >= (HZ/2))
			break;;
	}

	if (vbat < 0x06) {
		printk(KERN_NOTICE "Excessive leakage detected on module %d: %d volts (%02x) after %d ms\n", card,
		       376 * vbat / 1000, vbat, (int)((jiffies - origjiffies) * 1000 / HZ));
		return -1;
	} else if (debug) {
		printk(KERN_NOTICE "Post-leakage voltage: %d volts\n", 376 * vbat / 1000);
	}
	return 0;
}

static int wctdm_powerup_proslic(struct wctdm *wc, int card, int fast)
{
	unsigned char vbat;
	unsigned long origjiffies;
	int lim;

	/* Set period of DC-DC converter to 1/64 khz */
	wctdm_setreg(wc, card, 92, 0xff /* was 0xff */);

	/* Wait for VBat to powerup */
	origjiffies = jiffies;

	/* Disable powerdown */
	wctdm_setreg(wc, card, 14, 0);

	/* If fast, don't bother checking anymore */
	if (fast)
		return 0;

	while((vbat = wctdm_getreg(wc, card, 82)) < 0xc0) {
		/* Wait no more than 500ms */
		if ((jiffies - origjiffies) > HZ/2) {
			break;
		}
	}

	if (vbat < 0xc0) {
		if (wc->proslic_power == PROSLIC_POWER_UNKNOWN)
				 printk(KERN_NOTICE "ProSLIC on module %d failed to powerup within %d ms (%d mV only)\n\n -- DID YOU REMEMBER TO PLUG IN THE HD POWER CABLE TO THE TDM400P??\n",
					card, (int)(((jiffies - origjiffies) * 1000 / HZ)),
					vbat * 375);
		wc->proslic_power = PROSLIC_POWER_WARNED;
		return -1;
	} else if (debug) {
		printk(KERN_DEBUG "ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
	}
	wc->proslic_power = PROSLIC_POWER_ON;

        /* Proslic max allowed loop current, reg 71 LOOP_I_LIMIT */
        /* If out of range, just set it to the default value     */
        lim = (loopcurrent - 20) / 3;
        if ( loopcurrent > 41 ) {
                lim = 0;
                if (debug)
                        printk(KERN_DEBUG "Loop current out of range! Setting to default 20mA!\n");
        }
        else if (debug)
                        printk(KERN_DEBUG "Loop current set to %dmA!\n",(lim*3)+20);
        wctdm_setreg(wc,card,LOOP_I_LIMIT,lim);

	/* Engage DC-DC converter */
	wctdm_setreg(wc, card, 93, 0x19 /* was 0x19 */);
#if 0
	origjiffies = jiffies;
	while(0x80 & wctdm_getreg(wc, card, 93)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk(KERN_DEBUG "Timeout waiting for DC-DC calibration on module %d\n", card);
			return -1;
		}
	}

#if 0
	/* Wait a full two seconds */
	while((jiffies - origjiffies) < 2 * HZ);

	/* Just check to be sure */
	vbat = wctdm_getreg(wc, card, 82);
	printk(KERN_DEBUG "ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
#endif
#endif
	return 0;

}

static int wctdm_proslic_manual_calibrate(struct wctdm *wc, int card){
	unsigned long origjiffies;
	unsigned char i;

	wctdm_setreg(wc, card, 21, 0);//(0)  Disable all interupts in DR21
	wctdm_setreg(wc, card, 22, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 23, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 64, 0);//(0)

	wctdm_setreg(wc, card, 97, 0x18); //(0x18)Calibrations without the ADC and DAC offset and without common mode calibration.
	wctdm_setreg(wc, card, 96, 0x47); //(0x47)	Calibrate common mode and differential DAC mode DAC + ILIM

	origjiffies=jiffies;
	while( wctdm_getreg(wc,card,96)!=0 ){
		if((jiffies-origjiffies)>80)
			return -1;
	}
//Initialized DR 98 and 99 to get consistant results.
// 98 and 99 are the results registers and the search should have same intial conditions.

/*******************************The following is the manual gain mismatch calibration****************************/
/*******************************This is also available as a function *******************************************/
	// Delay 10ms
	origjiffies=jiffies;
	while((jiffies-origjiffies)<1);
	wctdm_proslic_setreg_indirect(wc, card, 88, 0);
	wctdm_proslic_setreg_indirect(wc, card, 89, 0);
	wctdm_proslic_setreg_indirect(wc, card, 90, 0);
	wctdm_proslic_setreg_indirect(wc, card, 91, 0);
	wctdm_proslic_setreg_indirect(wc, card, 92, 0);
	wctdm_proslic_setreg_indirect(wc, card, 93, 0);

	wctdm_setreg(wc, card, 98, 0x10); // This is necessary if the calibration occurs other than at reset time
	wctdm_setreg(wc, card, 99, 0x10);

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 98, i);
		origjiffies=jiffies;
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc, card, 88)) == 0)
			break;
	} // for

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 99, i);
		origjiffies=jiffies;
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc, card, 89)) == 0)
			break;
	}//for

/*******************************The preceding is the manual gain mismatch calibration****************************/
/**********************************The following is the longitudinal Balance Cal***********************************/
	wctdm_setreg(wc,card,64,1);
	while((jiffies-origjiffies)<10); // Sleep 100?

	wctdm_setreg(wc, card, 64, 0);
	wctdm_setreg(wc, card, 23, 0x4);  // enable interrupt for the balance Cal
	wctdm_setreg(wc, card, 97, 0x1); // this is a singular calibration bit for longitudinal calibration
	wctdm_setreg(wc, card, 96, 0x40);

	wctdm_getreg(wc, card, 96); /* Read Reg 96 just cause */

	wctdm_setreg(wc, card, 21, 0xFF);
	wctdm_setreg(wc, card, 22, 0xFF);
	wctdm_setreg(wc, card, 23, 0xFF);

	/**The preceding is the longitudinal Balance Cal***/
	return(0);

}
#if 1
static int wctdm_proslic_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	int x;
	/* Perform all calibrations */
	wctdm_setreg(wc, card, 97, 0x1f);

	/* Begin, no speedup */
	wctdm_setreg(wc, card, 96, 0x5f);

	/* Wait for it to finish */
	origjiffies = jiffies;
	while(wctdm_getreg(wc, card, 96)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk(KERN_NOTICE "Timeout waiting for calibration of module %d\n", card);
			return -1;
		}
	}

	if (debug) {
		/* Print calibration parameters */
		printk(KERN_DEBUG "Calibration Vector Regs 98 - 107: \n");
		for (x=98;x<108;x++) {
			printk(KERN_DEBUG "%d: %02x\n", x, wctdm_getreg(wc, card, x));
		}
	}
	return 0;
}
#endif

static void wait_just_a_bit(int foo)
{
	long newjiffies;
	newjiffies = jiffies + foo;
	while(jiffies < newjiffies);
}

/*********************************************************************
 * Set the hwgain on the analog modules
 *
 * card = the card position for this module (0-23)
 * gain = gain in dB x10 (e.g. -3.5dB  would be gain=-35)
 * tx = (0 for rx; 1 for tx)
 *
 *******************************************************************/
static int wctdm_set_hwgain(struct wctdm *wc, int card, __s32 gain, __u32 tx)
{
	if (!(wc->modtype[card] == MOD_TYPE_FXO)) {
		printk(KERN_NOTICE "Cannot adjust gain.  Unsupported module type!\n");
		return -1;
	}
	if (tx) {
		if (debug)
			printk(KERN_DEBUG "setting FXO tx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 38, 16 + (gain/-10));
			wctdm_setreg(wc, card, 40, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 38, gain/10);
			wctdm_setreg(wc, card, 40, (gain%10));
		} else {
			printk(KERN_INFO "FXO tx gain is out of range (%d)\n", gain);
			return -1;
		}
	} else { /* rx */
		if (debug)
			printk(KERN_DEBUG "setting FXO rx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 39, 16+ (gain/-10));
			wctdm_setreg(wc, card, 41, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 39, gain/10);
			wctdm_setreg(wc, card, 41, (gain%10));
		} else {
			printk(KERN_INFO "FXO rx gain is out of range (%d)\n", gain);
			return -1;
		}
	}

	return 0;
}

static int set_vmwi(struct wctdm * wc, int chan_idx)
{
	struct fxs *const fxs = &wc->mod[chan_idx].fxs;
	if (fxs->vmwi_active_messages) {
		fxs->vmwi_lrev =
		    (fxs->vmwisetting.vmwi_type & DAHDI_VMWI_LREV) ? 1 : 0;
		fxs->vmwi_hvdc =
		    (fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVDC) ? 1 : 0;
		fxs->vmwi_hvac =
		    (fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVAC) ? 1 : 0;
	} else {
		fxs->vmwi_lrev = 0;
		fxs->vmwi_hvdc = 0;
		fxs->vmwi_hvac = 0;
	}

	if (debug) {
		printk(KERN_DEBUG "Setting VMWI on channel %d, messages=%d, "
				"lrev=%d, hvdc=%d, hvac=%d\n",
				chan_idx,
				fxs->vmwi_active_messages,
				fxs->vmwi_lrev,
				fxs->vmwi_hvdc,
				fxs->vmwi_hvac
			  );
	}
	if (fxs->vmwi_hvac) {
		/* Can't change ring generator while in On Hook Transfer mode*/
		if (!fxs->ohttimer) {
			if (POLARITY_XOR)
				fxs->idletxhookstate |= SLIC_LF_REVMASK;
			else
				fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			/* Set ring generator for neon */
			wctdm_set_ring_generator_mode(wc, chan_idx, 1);
			/* Activate ring to send neon pulses */
			fxs->lasttxhook = SLIC_LF_RINGING;
			wctdm_setreg(wc, chan_idx, LINE_STATE, fxs->lasttxhook);
		}
	} else {
		if (fxs->neonringing) {
			/* Set ring generator for normal ringer */
			wctdm_set_ring_generator_mode(wc, chan_idx, 0);
			/* ACTIVE, polarity determined later */
			fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
		} else if ((fxs->lasttxhook == SLIC_LF_RINGING) ||
					(fxs->lasttxhook == SLIC_LF_OPEN)) {
			/* Can't change polarity while ringing or when open,
				set idlehookstate instead */
			if (POLARITY_XOR)
				fxs->idletxhookstate |= SLIC_LF_REVMASK;
			else
				fxs->idletxhookstate &= ~SLIC_LF_REVMASK;

			printk(KERN_DEBUG "Unable to change polarity on channel"
					    "%d, lasttxhook=0x%X\n",
				chan_idx,
				fxs->lasttxhook
			);
			return 0;
		}
		if (POLARITY_XOR) {
			fxs->idletxhookstate |= SLIC_LF_REVMASK;
			fxs->lasttxhook |= SLIC_LF_REVMASK;
		} else {
			fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			fxs->lasttxhook &= ~SLIC_LF_REVMASK;
		}
		wctdm_setreg(wc, chan_idx, LINE_STATE, fxs->lasttxhook);
	}
	return 0;
}

static int wctdm_init_voicedaa(struct wctdm *wc, int card, int fast, int manual, int sane)
{
	unsigned char reg16=0, reg26=0, reg30=0, reg31=0;
	long newjiffies;
	wc->modtype[card] = MOD_TYPE_FXO;
	/* San/ty check the ProSLIC */
	if (!sane && wctdm_voicedaa_insane(wc, card))
		return -2;

	/* Software reset */
	wctdm_setreg(wc, card, 1, 0x80);

	/* Wait just a bit */
	wait_just_a_bit(HZ/10);

	/* Enable PCM, ulaw */
	if (alawoverride){
		wctdm_setreg(wc, card, 33, 0x20);
	} else {
		wctdm_setreg(wc, card, 33, 0x28);
	}

	/* Set On-hook speed, Ringer impedence, and ringer threshold */
	reg16 |= (fxo_modes[_opermode].ohs << 6);
	reg16 |= (fxo_modes[_opermode].rz << 1);
	reg16 |= (fxo_modes[_opermode].rt);
	wctdm_setreg(wc, card, 16, reg16);

	if(fwringdetect) {
		/* Enable ring detector full-wave rectifier mode */
		wctdm_setreg(wc, card, 18, 2);
		wctdm_setreg(wc, card, 24, 0);
	} else {
		/* Set to the device defaults */
		wctdm_setreg(wc, card, 18, 0);
		wctdm_setreg(wc, card, 24, 0x19);
	}

	/* Set DC Termination:
	   Tip/Ring voltage adjust, minimum operational current, current limitation */
	reg26 |= (fxo_modes[_opermode].dcv << 6);
	reg26 |= (fxo_modes[_opermode].mini << 4);
	reg26 |= (fxo_modes[_opermode].ilim << 1);
	wctdm_setreg(wc, card, 26, reg26);

	/* Set AC Impedence */
	reg30 = (fxo_modes[_opermode].acim);
	wctdm_setreg(wc, card, 30, reg30);

	/* Misc. DAA parameters */
	if (fastpickup)
		reg31 = 0xe3;
	else
		reg31 = 0xa3;

	reg31 |= (fxo_modes[_opermode].ohs2 << 3);
	wctdm_setreg(wc, card, 31, reg31);

	/* Set Transmit/Receive timeslot */
	wctdm_setreg(wc, card, 34, (codechan_map[card] * 8) );
	wctdm_setreg(wc, card, 35, 0x00);
	wctdm_setreg(wc, card, 36, (codechan_map[card] * 8) );
	wctdm_setreg(wc, card, 37, 0x00);

	/* Enable ISO-Cap */
	wctdm_setreg(wc, card, 6, 0x00);

	if (fastpickup)
		wctdm_setreg(wc, card, 17, wctdm_getreg(wc, card, 17) | 0x20);

	/* Wait 1000ms for ISO-cap to come up */
	newjiffies = jiffies;
	newjiffies += 2 * HZ;
	while((jiffies < newjiffies) && !(wctdm_getreg(wc, card, 11) & 0xf0))
		wait_just_a_bit(HZ/10);

	if (!(wctdm_getreg(wc, card, 11) & 0xf0)) {
		printk(KERN_NOTICE "VoiceDAA did not bring up ISO link properly!\n");
		return -1;
	}
	if (debug)
		printk(KERN_DEBUG "ISO-Cap is now up, line side: %02x rev %02x\n",
		       wctdm_getreg(wc, card, 11) >> 4,
		       (wctdm_getreg(wc, card, 13) >> 2) & 0xf);
	/* Enable on-hook line monitor */
	wctdm_setreg(wc, card, 5, 0x08);

	/* Take values for fxotxgain and fxorxgain and apply them to module */
	wctdm_set_hwgain(wc, card, fxotxgain, 1);
	wctdm_set_hwgain(wc, card, fxorxgain, 0);

	/* NZ -- crank the tx gain up by 7 dB */
	if (!strcmp(fxo_modes[_opermode].name, "NEWZEALAND")) {
		printk(KERN_INFO "Adjusting gain\n");
		wctdm_set_hwgain(wc, card, 7, 1);
	}

	if(debug)
		printk(KERN_DEBUG "DEBUG fxotxgain:%i.%i fxorxgain:%i.%i\n", (wctdm_getreg(wc, card, 38)/16)?-(wctdm_getreg(wc, card, 38) - 16) : wctdm_getreg(wc, card, 38), (wctdm_getreg(wc, card, 40)/16)? -(wctdm_getreg(wc, card, 40) - 16):wctdm_getreg(wc, card, 40), (wctdm_getreg(wc, card, 39)/16)? -(wctdm_getreg(wc, card, 39) - 16) : wctdm_getreg(wc, card, 39),(wctdm_getreg(wc, card, 41)/16)?-(wctdm_getreg(wc, card, 41) - 16):wctdm_getreg(wc, card, 41));

	return 0;

}

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast, int manual, int sane)
{

	unsigned short tmp[5];
	unsigned char r19,r9;
	int x;
	int fxsmode=0;
	struct fxs *const fxs = &wc->mod[card].fxs;
	/* Sanity check the ProSLIC */
	if (!sane && wctdm_proslic_insane(wc, card))
		return -2;

	/* default messages to none and method to FSK */
	memset(&fxs->vmwisetting, 0, sizeof(fxs->vmwisetting));
	fxs->vmwi_lrev = 0;
	fxs->vmwi_hvdc = 0;
	fxs->vmwi_hvac = 0;


	/* By default, don't send on hook */
	if (!reversepolarity != !fxs->reversepolarity)
		fxs->idletxhookstate = SLIC_LF_ACTIVE_REV;
	else
		fxs->idletxhookstate = SLIC_LF_ACTIVE_FWD;

	if (sane) {
		/* Make sure we turn off the DC->DC converter to prevent anything from blowing up */
		wctdm_setreg(wc, card, 14, 0x10);
	}

	if (wctdm_proslic_init_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed to initialize on module %d.\n", card);
		return -1;
	}

	/* Clear scratch pad area */
	wctdm_proslic_setreg_indirect(wc, card, 97,0);

	/* Clear digital loopback */
	wctdm_setreg(wc, card, 8, 0);

	/* Revision C optimization */
	wctdm_setreg(wc, card, 108, 0xeb);

	/* Disable automatic VBat switching for safety to prevent
	   Q7 from accidently turning on and burning out. */
	wctdm_setreg(wc, card, 67, 0x07);  /* Note, if pulse dialing has problems at high REN loads
					      change this to 0x17 */

	/* Turn off Q7 */
	wctdm_setreg(wc, card, 66, 1);

	/* Flush ProSLIC digital filters by setting to clear, while
	   saving old values */
	for (x=0;x<5;x++) {
		tmp[x] = wctdm_proslic_getreg_indirect(wc, card, x + 35);
		wctdm_proslic_setreg_indirect(wc, card, x + 35, 0x8000);
	}

	/* Power up the DC-DC converter */
	if (wctdm_powerup_proslic(wc, card, fast)) {
		printk(KERN_NOTICE "Unable to do INITIAL ProSLIC powerup on module %d\n", card);
		return -1;
	}

	if (!fast) {

		/* Check for power leaks */
		if (wctdm_proslic_powerleak_test(wc, card)) {
			printk(KERN_NOTICE "ProSLIC module %d failed leakage test.  Check for short circuit\n", card);
		}
		/* Power up again */
		if (wctdm_powerup_proslic(wc, card, fast)) {
			printk(KERN_NOTICE "Unable to do FINAL ProSLIC powerup on module %d\n", card);
			return -1;
		}
#ifndef NO_CALIBRATION
		/* Perform calibration */
		if(manual) {
			if (wctdm_proslic_manual_calibrate(wc, card)) {
				//printk(KERN_NOTICE "Proslic failed on Manual Calibration\n");
				if (wctdm_proslic_manual_calibrate(wc, card)) {
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Calibrate Manually. (Try -DNO_CALIBRATION in Makefile)\n");
					return -1;
				}
				printk(KERN_NOTICE "Proslic Passed Manual Calibration on Second Attempt\n");
			}
		}
		else {
			if(wctdm_proslic_calibrate(wc, card))  {
				//printk(KERN_NOTICE "ProSlic died on Auto Calibration.\n");
				if (wctdm_proslic_calibrate(wc, card)) {
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Auto Calibrate\n");
					return -1;
				}
				printk(KERN_NOTICE "Proslic Passed Auto Calibration on Second Attempt\n");
			}
		}
		/* Perform DC-DC calibration */
		wctdm_setreg(wc, card, 93, 0x99);
		r19 = wctdm_getreg(wc, card, 107);
		if ((r19 < 0x2) || (r19 > 0xd)) {
			printk(KERN_NOTICE "DC-DC cal has a surprising direct 107 of 0x%02x!\n", r19);
			wctdm_setreg(wc, card, 107, 0x8);
		}

		/* Save calibration vectors */
		for (x=0;x<NUM_CAL_REGS;x++)
			fxs->calregs.vals[x] = wctdm_getreg(wc, card, 96 + x);
#endif

	} else {
		/* Restore calibration registers */
		for (x=0;x<NUM_CAL_REGS;x++)
			wctdm_setreg(wc, card, 96 + x, fxs->calregs.vals[x]);
	}
	/* Calibration complete, restore original values */
	for (x=0;x<5;x++) {
		wctdm_proslic_setreg_indirect(wc, card, x + 35, tmp[x]);
	}

	if (wctdm_proslic_verify_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed verification.\n");
		return -1;
	}


#if 0
    /* Disable Auto Power Alarm Detect and other "features" */
    wctdm_setreg(wc, card, 67, 0x0e);
    blah = wctdm_getreg(wc, card, 67);
#endif

#if 0
    if (wctdm_proslic_setreg_indirect(wc, card, 97, 0x0)) { // Stanley: for the bad recording fix
		 printk(KERN_INFO "ProSlic IndirectReg Died.\n");
		 return -1;
	}
#endif

    if (alawoverride)
    	wctdm_setreg(wc, card, 1, 0x20);
    else
    	wctdm_setreg(wc, card, 1, 0x28);
 	// U-Law 8-bit interface
    wctdm_setreg(wc, card, 2, (codechan_map[card] * 8) );    // Tx Start count low byte  0
    wctdm_setreg(wc, card, 3, 0);    // Tx Start count high byte 0
    wctdm_setreg(wc, card, 4, (codechan_map[card] * 8) );    // Rx Start count low byte  0
    wctdm_setreg(wc, card, 5, 0);    // Rx Start count high byte 0
    wctdm_setreg(wc, card, 18, 0xff);     // clear all interrupt
    wctdm_setreg(wc, card, 19, 0xff);
    wctdm_setreg(wc, card, 20, 0xff);
    wctdm_setreg(wc, card, 73, 0x04);
	if (fxshonormode) {
		fxsmode = acim2tiss[fxo_modes[_opermode].acim];
		wctdm_setreg(wc, card, 10, 0x08 | fxsmode);
	}
    if (lowpower)
    	wctdm_setreg(wc, card, 72, 0x10);

#if 0
    wctdm_setreg(wc, card, 21, 0x00); 	// enable interrupt
    wctdm_setreg(wc, card, 22, 0x02); 	// Loop detection interrupt
    wctdm_setreg(wc, card, 23, 0x01); 	// DTMF detection interrupt
#endif

#if 0
    /* Enable loopback */
    wctdm_setreg(wc, card, 8, 0x2);
    wctdm_setreg(wc, card, 14, 0x0);
    wctdm_setreg(wc, card, 64, 0x0);
    wctdm_setreg(wc, card, 1, 0x08);
#endif
	if (wctdm_init_ring_generator_mode(wc, card)) {
		return -1;
	}

	if(fxstxgain || fxsrxgain) {
		r9 = wctdm_getreg(wc, card, 9);
		switch (fxstxgain) {
			case 35:
				r9+=8;
				break;
			case -35:
				r9+=4;
				break;
			case 0:
				break;
		}

		switch (fxsrxgain) {
			case 35:
				r9+=2;
				break;
			case -35:
				r9+=1;
				break;
			case 0:
				break;
		}
		wctdm_setreg(wc,card,9,r9);
	}

	if(debug)
			printk(KERN_DEBUG "DEBUG: fxstxgain:%s fxsrxgain:%s\n",((wctdm_getreg(wc, card, 9)/8) == 1)?"3.5":(((wctdm_getreg(wc,card,9)/4) == 1)?"-3.5":"0.0"),((wctdm_getreg(wc, card, 9)/2) == 1)?"3.5":((wctdm_getreg(wc,card,9)%2)?"-3.5":"0.0"));

	fxs->lasttxhook = fxs->idletxhookstate;
	wctdm_setreg(wc, card, LINE_STATE, fxs->lasttxhook);
	return 0;
}

static int wctdm_ioctl(struct dahdi_chan *chan, unsigned int cmd, unsigned long data)
{
	struct wctdm_stats stats;
	struct wctdm_regs regs;
	struct wctdm_regop regop;
	struct wctdm_echo_coefs echoregs;
	struct dahdi_hwgain hwgain;
	struct wctdm *wc = chan->pvt;
	struct fxs *const fxs = &wc->mod[chan->chanpos - 1].fxs;
	int x;
	switch (cmd) {
	case DAHDI_ONHOOKTRANSFER:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		fxs->ohttimer = x << 3;

		/* Active mode when idle */
		fxs->idletxhookstate = POLARITY_XOR ?
				SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD;
		if (fxs->neonringing) {
			/* keep same Forward polarity */
			fxs->lasttxhook = SLIC_LF_OHTRAN_FWD;
			printk(KERN_INFO "ioctl: Start OnHookTrans, card %d\n",
					chan->chanpos - 1);
			wctdm_setreg(wc, chan->chanpos - 1,
					LINE_STATE, fxs->lasttxhook);
		} else if (fxs->lasttxhook == SLIC_LF_ACTIVE_FWD ||
			    fxs->lasttxhook == SLIC_LF_ACTIVE_REV) {
			/* Apply the change if appropriate */
			fxs->lasttxhook = POLARITY_XOR ?
				SLIC_LF_OHTRAN_REV : SLIC_LF_OHTRAN_FWD;
			printk(KERN_INFO "ioctl: Start OnHookTrans, card %d\n",
					chan->chanpos - 1);
			wctdm_setreg(wc, chan->chanpos - 1,
				      LINE_STATE, fxs->lasttxhook);
		}
		break;
	case DAHDI_SETPOLARITY:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		/* Can't change polarity while ringing or when open */
		if ((fxs->lasttxhook == SLIC_LF_RINGING) ||
		    (fxs->lasttxhook == SLIC_LF_OPEN))
			return -EINVAL;
		fxs->reversepolarity = x;
		if (POLARITY_XOR) {
			fxs->lasttxhook |= SLIC_LF_REVMASK;
			printk(KERN_INFO "ioctl: Reverse Polarity, card %d\n",
					chan->chanpos - 1);
		} else {
			fxs->lasttxhook &= ~SLIC_LF_REVMASK;
			printk(KERN_INFO "ioctl: Normal Polarity, card %d\n",
					chan->chanpos - 1);
		}
		wctdm_setreg(wc, chan->chanpos - 1,
					LINE_STATE, fxs->lasttxhook);
		break;
	case DAHDI_VMWI_CONFIG:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (copy_from_user(&(fxs->vmwisetting), (__user void *) data,
						sizeof(fxs->vmwisetting)))
			return -EFAULT;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case DAHDI_VMWI:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		if (0 > x)
			return -EFAULT;
		fxs->vmwi_active_messages = x;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case WCTDM_GET_STATS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			stats.tipvolt = wctdm_getreg(wc, chan->chanpos - 1, 80) * -376;
			stats.ringvolt = wctdm_getreg(wc, chan->chanpos - 1, 81) * -376;
			stats.batvolt = wctdm_getreg(wc, chan->chanpos - 1, 82) * -376;
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			stats.tipvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.ringvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.batvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
		} else
			return -EINVAL;
		if (copy_to_user((__user void *)data, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	case WCTDM_GET_REGS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			for (x=0;x<NUM_INDIRECT_REGS;x++)
				regs.indirect[x] = wctdm_proslic_getreg_indirect(wc, chan->chanpos -1, x);
			for (x=0;x<NUM_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<NUM_FXO_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		}
		if (copy_to_user((__user void *)data, &regs, sizeof(regs)))
			return -EFAULT;
		break;
	case WCTDM_SET_REG:
		if (copy_from_user(&regop, (__user void *) data, sizeof(regop)))
			return -EFAULT;
		if (regop.indirect) {
			if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
				return -EINVAL;
			printk(KERN_INFO "Setting indirect %d to 0x%04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_proslic_setreg_indirect(wc, chan->chanpos - 1, regop.reg, regop.val);
		} else {
			regop.val &= 0xff;
			printk(KERN_INFO "Setting direct %d to %04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_setreg(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		break;
	case WCTDM_SET_ECHOTUNE:
		printk(KERN_INFO "-- Setting echo registers: \n");
		if (copy_from_user(&echoregs, (__user void *)data, sizeof(echoregs)))
			return -EFAULT;

		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			/* Set the ACIM register */
			wctdm_setreg(wc, chan->chanpos - 1, 30, echoregs.acim);

			/* Set the digital echo canceller registers */
			wctdm_setreg(wc, chan->chanpos - 1, 45, echoregs.coef1);
			wctdm_setreg(wc, chan->chanpos - 1, 46, echoregs.coef2);
			wctdm_setreg(wc, chan->chanpos - 1, 47, echoregs.coef3);
			wctdm_setreg(wc, chan->chanpos - 1, 48, echoregs.coef4);
			wctdm_setreg(wc, chan->chanpos - 1, 49, echoregs.coef5);
			wctdm_setreg(wc, chan->chanpos - 1, 50, echoregs.coef6);
			wctdm_setreg(wc, chan->chanpos - 1, 51, echoregs.coef7);
			wctdm_setreg(wc, chan->chanpos - 1, 52, echoregs.coef8);

			printk(KERN_INFO "-- Set echo registers successfully\n");

			break;
		} else {
			return -EINVAL;

		}
		break;
	case DAHDI_SET_HWGAIN:
		if (copy_from_user(&hwgain, (__user void *) data, sizeof(hwgain)))
			return -EFAULT;

		wctdm_set_hwgain(wc, chan->chanpos-1, hwgain.newgain, hwgain.tx);

		if (debug)
			printk(KERN_DEBUG "Setting hwgain on channel %d to %d for %s direction\n",
				chan->chanpos-1, hwgain.newgain, hwgain.tx ? "tx" : "rx");
		break;
	default:
		return -ENOTTY;
	}
	return 0;

}

static int wctdm_open(struct dahdi_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	if (!(wc->cardflag & (1 << (chan->chanpos - 1))))
		return -ENODEV;
	if (wc->dead)
		return -ENODEV;
	wc->usecount++;
	return 0;
}

static inline struct wctdm *wctdm_from_span(struct dahdi_span *span)
{
	return container_of(span, struct wctdm, span);
}

static int wctdm_watchdog(struct dahdi_span *span, int event)
{
	printk(KERN_INFO "TDM: Restarting DMA\n");
	wctdm_restart_dma(wctdm_from_span(span));
	return 0;
}

static int wctdm_close(struct dahdi_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	struct fxs *const fxs = &wc->mod[chan->chanpos - 1].fxs;
	wc->usecount--;
	if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
		int idlehookstate;
		idlehookstate = POLARITY_XOR ?
						SLIC_LF_ACTIVE_REV :
						SLIC_LF_ACTIVE_FWD;
		fxs->idletxhookstate = idlehookstate;
	}
	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead)
		wctdm_release(wc);
	return 0;
}

static int wctdm_init_ring_generator_mode(struct wctdm *wc, int card)
{
	wctdm_setreg(wc, card, 34, 0x00);	/* Ringing Osc. Control */

						/* neon trapezoid timers */
	wctdm_setreg(wc, card, 48, 0xe0);	/* Active Timer low byte */
	wctdm_setreg(wc, card, 49, 0x01);	/* Active Timer high byte */
	wctdm_setreg(wc, card, 50, 0xF0);	/* Inactive Timer low byte */
	wctdm_setreg(wc, card, 51, 0x05);	/* Inactive Timer high byte */

	wctdm_set_ring_generator_mode(wc, card, 0);

	return 0;
}

static int wctdm_set_ring_generator_mode(struct wctdm *wc, int card, int mode)
{
	int reg20, reg21, reg74; /* RCO, RNGX, VBATH */
	struct fxs *const fxs = &wc->mod[card].fxs;

	fxs->neonringing = mode;	/* track ring generator mode */

	if (mode) { /* Neon */
		if (debug)
			printk(KERN_DEBUG "NEON ring on chan %d, "
			"lasttxhook was 0x%x\n", card, fxs->lasttxhook);
		/* Must be in FORWARD ACTIVE before setting ringer */
		fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
		wctdm_setreg(wc, card, LINE_STATE, fxs->lasttxhook);

		wctdm_proslic_setreg_indirect(wc, card, 22,
					       NEON_MWI_RNGY_PULSEWIDTH);
		wctdm_proslic_setreg_indirect(wc, card, 21,
					       0x7bef);	/* RNGX (91.5Vpk) */
		wctdm_proslic_setreg_indirect(wc, card, 20,
					       0x009f);	/* RCO (RNGX, t rise)*/

		wctdm_setreg(wc, card, 34, 0x19); /* Ringing Osc. Control */
		wctdm_setreg(wc, card, 74, 0x3f); /* VBATH 94.5V */
		wctdm_proslic_setreg_indirect(wc, card, 29, 0x4600); /* RPTP */
		/* A write of 0x04 to register 64 will turn on the VM led */
	} else {
		wctdm_setreg(wc, card, 34, 0x00); /* Ringing Osc. Control */
		/* RNGY Initial Phase */
		wctdm_proslic_setreg_indirect(wc, card, 22, 0x0000);
		wctdm_proslic_setreg_indirect(wc, card, 29, 0x3600); /* RPTP */
		/* A write of 0x04 to register 64 will turn on the ringer */

		if (fastringer) {
			/* Speed up Ringer */
			reg20 =  0x7e6d;
			reg74 = 0x32;	/* Default */
			/* Beef up Ringing voltage to 89V */
			if (boostringer) {
				reg74 = 0x3f;
				reg21 = 0x0247;	/* RNGX */
				if (debug)
					printk(KERN_DEBUG "Boosting fast ringer"
						" on chan %d (89V peak)\n",
						card);
			} else if (lowpower) {
				reg21 = 0x014b;	/* RNGX */
				if (debug)
					printk(KERN_DEBUG "Reducing fast ring "
					    "power on chan %d (50V peak)\n",
					    card);
			} else if (fxshonormode &&
						fxo_modes[_opermode].ring_x) {
				reg21 = fxo_modes[_opermode].ring_x;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: fast "
						"ring_x power on chan %d\n",
						card);
			} else {
				reg21 = 0x01b9;
				if (debug)
					printk(KERN_DEBUG "Speeding up ringer "
						"on chan %d (25Hz)\n",
						card);
			}
			/* VBATH */
			wctdm_setreg(wc, card, 74, reg74);
			/*RCO*/
			wctdm_proslic_setreg_indirect(wc, card, 20, reg20);
			/*RNGX*/
			wctdm_proslic_setreg_indirect(wc, card, 21, reg21);

		} else {
			/* Ringer Speed */
			if (fxshonormode && fxo_modes[_opermode].ring_osc) {
				reg20 = fxo_modes[_opermode].ring_osc;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: "
						"ring_osc speed on chan %d\n",
						card);
			} else {
				reg20 = 0x7ef0;	/* Default */
			}

			reg74 = 0x32;	/* Default */
			/* Beef up Ringing voltage to 89V */
			if (boostringer) {
				reg74 = 0x3f;
				reg21 = 0x1d1;
				if (debug)
					printk(KERN_DEBUG "Boosting ringer on "
						"chan %d (89V peak)\n",
						card);
			} else if (lowpower) {
				reg21 = 0x108;
				if (debug)
					printk(KERN_DEBUG "Reducing ring power "
						"on chan %d (50V peak)\n",
						card);
			} else if (fxshonormode &&
						fxo_modes[_opermode].ring_x) {
				reg21 = fxo_modes[_opermode].ring_x;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: ring_x"
						" power on chan %d\n",
						card);
			} else {
				reg21 = 0x160;
				if (debug)
					printk(KERN_DEBUG "Normal ring power on"
						" chan %d\n",
						card);
			}
			/* VBATH */
			wctdm_setreg(wc, card, 74, reg74);
			/* RCO */
			wctdm_proslic_setreg_indirect(wc, card, 20, reg20);
			  /* RNGX */
			wctdm_proslic_setreg_indirect(wc, card, 21, reg21);
		}
	}
	return 0;
}



static int wctdm_hooksig(struct dahdi_chan *chan, enum dahdi_txsig txsig)
{
	struct wctdm *wc = chan->pvt;
	int chan_entry = chan->chanpos - 1;
	if (wc->modtype[chan_entry] == MOD_TYPE_FXO) {
		/* XXX Enable hooksig for FXO XXX */
		switch(txsig) {
		case DAHDI_TXSIG_START:
		case DAHDI_TXSIG_OFFHOOK:
			printk("OFF HOOK on tdm bus %d\n", wc->busidx);
			wc->mod[chan_entry].fxo.offhook = 1;
			wctdm_setreg(wc, chan_entry, 5, 0x9);
			break;
		case DAHDI_TXSIG_ONHOOK:
			printk("ON HOOK on tdm bus %d\n", wc->busidx);
			wc->mod[chan_entry].fxo.offhook = 0;
			wctdm_setreg(wc, chan_entry, 5, 0x8);
			break;
		default:
			printk(KERN_NOTICE "wcfxo: Can't set tx state to %d\n", txsig);
		}
	} else {
		wctdm_fxs_hooksig(wc, chan_entry, txsig);
	}
	return 0;
}

static const struct dahdi_span_ops wctdm_span_ops = {
	.owner = THIS_MODULE,
	.hooksig = wctdm_hooksig,
	.open = wctdm_open,
	.close = wctdm_close,
	.ioctl = wctdm_ioctl,
	.watchdog = wctdm_watchdog,
};

static void wctdm_free_channels(struct wctdm * wc)
{
	int x=0;
	for (x =0; x < ARRAY_SIZE(wc->chans); x++)
	{
		if (wc->chans[x]) {
			kfree(wc->chans[x]);
			wc->chans[x] = NULL;
		}
	}
}

static int wctdm_span_create(struct wctdm *wc)
{
	int x;
	struct dahdi_chan *chans[NUM_CARDS] = {NULL,};
	//create a different name
	wc->name = kasprintf(GFP_KERNEL, "WCTDM%d", wc->busidx);

	//initial dahdi_chan first
	for ( x = 0; x < NUM_CARDS; x++) {
		chans[x] = kzalloc(sizeof(struct dahdi_chan), GFP_KERNEL);
		if (!chans[x]) {
			return -ENOMEM;
		}
	}
	wctdm_free_channels(wc);
	memcpy(wc->chans, chans, sizeof(wc->chans));
	memset(chans, 0, sizeof(chans));

	sprintf(wc->span.name, "WCTDM/%d/%d", wc->pos, wc->busidx);
	snprintf(wc->span.desc, sizeof(wc->span.desc) - 1, "%s TDM BUS %d",
		 wc->variety, wc->busidx);

	if (alawoverride) {
		printk(KERN_INFO "ALAW override parameter detected.  Device will be operating in ALAW\n");
		wc->span.deflaw = DAHDI_LAW_ALAW;
	} else {
		wc->span.deflaw = DAHDI_LAW_MULAW;
	}
	for (x = 0; x < NUM_CARDS; x++) {
		sprintf(wc->chans[x]->name, "WCTDM/%d/%d/%d", wc->pos, wc->busidx, x);
		wc->chans[x]->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		wc->chans[x]->sigcap |= DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
		wc->chans[x]->chanpos = x+1;
		wc->chans[x]->pvt = wc;
	}
	wc->span.chans = wc->chans;
	wc->span.channels = NUM_CARDS;
	wc->span.flags = DAHDI_FLAG_RBS;
	//TODO
	wc->span.ops = &wctdm_span_ops;
	wc->span.spantype = 3;

	list_add_tail(&wc->span.device_node, &wc->ddev->spans);
	printk("Created Span %s (%s)\n", wc->span.name, wc->span.desc);
	return 0;
}

static void wctdm_post_initialize(struct wctdm *wc)
{
	int x;
	/* Finalize signalling  */
	for (x = 0; x < NUM_CARDS; x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->chans[x]->sigcap = DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
			else
				wc->chans[x]->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		} else if (!(wc->chans[x]->sigcap & DAHDI_SIG_BROKEN)) {
			wc->chans[x]->sigcap = 0;
		}
	}
}

void fx_auto_detect(struct wctdm *wc) {
	int i;
	u8  reg;

	for(i=0; i<4; i++) {
		spi_reset(wc);
		spi_reset(wc);
		wc->modtype[i] = MOD_TYPE_FXO;
		__wctdm_setcard(wc, i);
		__write_8bits(wc, 0x60);
		__write_8bits(wc, (0x02 & 0x7f));
		reg = __read_8bits(wc);
    		if (reg == 0x3) {
				wc->modtype[i] = MOD_TYPE_FXO;
				//printk("found a fxo module on tdm bus %d slot %d\n", wc->busidx, i);
    		}
		else {
    			spi_reset(wc);
			wc->modtype[i] = MOD_TYPE_FXS;
    			reg = __wctdm_getreg(wc, i, 0x0);
      		if (reg == 0x5 ) {
				wc->modtype[i] = MOD_TYPE_FXS;
				//printk("found a fxs module on tdm bus %d slot %d\n", wc->busidx, i);
      		}
      		else {
    			spi_reset(wc);
    			reg = __wctdm_getreg(wc, i, 0x1);
         		if ( ( reg & 0xC0) == 0x80 ) {
					wc->modtype[i] = MOD_TYPE_FXS;
					//printk("found a fxs module on tdm bus %d slot %d\n", wc->busidx, i);
				}
			}
		}
  	}
}

static int wctdm_hardware_init(struct wctdm *wc)
{
	/* Hardware stuff */
	unsigned char x;
	/*
	for(x=0; x<4; x++) {
	  printk("port: %d port_type: %c\n", x+1, port_type[x]);
	}
	*/

	for (x=0;x<NUM_CARDS;x++) {
	  int sane=0,ret=0,readi=0;

	  if (wc->modtype[x] == MOD_TYPE_FXO) {
	    if (!(ret = wctdm_init_voicedaa(wc, x, 0, 0, sane))) {
	      wc->cardflag |= (1 << x);
	      printk("Module %d TDM BUS %d: Installed -- AUTO FXO (%s mode)\n",wc->pos, wc->busidx, fxo_modes[_opermode].name);
	    } else
	      printk("Module %d TDM BUS %d: Not installed\n", wc->pos, wc->busidx);
	  }
	  else {

	    sane=0;
	    /* Init with Automatic Calibaration */
	    if (!(ret = wctdm_init_proslic(wc, x, 0, 0, sane))) {
	      wc->cardflag |= (1 << x);
	      if (debug) {
		readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
		printk("Proslic module %d loop current is %dmA\n",x,
		       ((readi*3)+20));
	      }
	      printk("Module %d TDM BUS %d: Installed -- AUTO FXS\n",wc->pos, wc->busidx);
	    }
	    else
	    {
	          if(ret != -2)
		   {
			sane=1;
			/* Init with Manual Calibration */
			if (!wctdm_init_proslic(wc, x, 0, 1, sane))
			{
				wc->cardflag |= (1 << x);
                            if (debug)
				{
                                   readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
                                    printk("Proslic module %d loop current is %dmA\n",x,
                                    ((readi*3)+20));
                            }
				printk("Module %d TDM BUS %d: Installed -- MANUAL FXS\n",wc->pos, wc->busidx);
			}
			else
			{
				printk("Module %d TDM BUS %d: FAILED FXS (%s)\n", wc->pos, wc->busidx, fxshonormode ? fxo_modes[_opermode].name : "FCC");
			}
		  }
	    }
	  }
	}
	return 0;
}

static void wctdm_enable_interrupts(struct wctdm *wc)
{
	//TODO
	/* Enable interrupts (we care about all of them) */
	writel(0x33, wc->bdev->ioaddr + DAWN_MASK0);
}

static void wctdm_restart_dma(struct wctdm *wc)
{
	//TODO
	writel(0x100, wc->bdev->ioaddr + DAWN_DMAMOD);
	writel(0x100, wc->bdev->ioaddr + DAWN_DMAMOD1);
}

static void wctdm_start_dma(struct wctdm *wc)
{
	//TODO
	writel(0xC100, wc->bdev->ioaddr + DAWN_PCMCTL);
	writel(0xC100, wc->bdev->ioaddr + DAWN_PCMCTL1);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(1);
	writel(0x100, wc->bdev->ioaddr + DAWN_DMAMOD);
	writel(0x100, wc->bdev->ioaddr + DAWN_DMAMOD1);
}

static void wctdm_stop_dma(struct wctdm *wc)
{
	//TODO
	writel(0x00, wc->bdev->ioaddr + DAWN_DMAMOD);
	writel(0x00, wc->bdev->ioaddr + DAWN_DMAMOD1);
}

static void wctdm_reset_tdm(struct wctdm *wc)
{
	//TODO
	writel(0x100, wc->bdev->ioaddr + DAWN_PCMCTL);
	writel(0x100, wc->bdev->ioaddr + DAWN_PCMCTL1);
}

static void wctdm_disable_interrupts(struct wctdm *wc)
{
	//TODO
	writel(0x00, wc->bdev->ioaddr + DAWN_MASK0);
}

static int __devinit wctdm_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int res;
	struct wctdm *wc;
	int i,x;

	for (x=0;x<WC_MAX_IFACES;x++){
		if (!ifaces[x]) break;
	}
	if (x >= WC_MAX_IFACES) {
		printk(KERN_NOTICE "Too many interfaces\n");
		return -EIO;
	}

	if (pci_enable_device(pdev)) {
		res = -EIO;
	} else {
		dawn_card = kmalloc(sizeof(struct dawn_base), GFP_KERNEL);
		if (dawn_card) {
			int cardcount = 0;
			ifaces[x] = dawn_card;
			memset(dawn_card, 0, sizeof(struct dawn_base));
			spin_lock_init(&dawn_card->lock);
			pci_set_master(pdev);
			res = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
    			if (res) {
    				dev_err(&pdev->dev, "try set dma mask failed\n");
    				return res;
    			}
    			res = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
    			if (res) {
    				dev_err(&pdev->dev, "try set consistent dma mask failed\n");
    				return res;
    			}

    			res = pci_request_regions(pdev, KBUILD_MODNAME);
    			if (res) {
    				dev_err(&pdev->dev, "try pci request regions failed%d", res);
    				pci_disable_device(pdev);
    				return res;
    			}

    			dawn_card->curcard = -1;
    			dawn_card->dev = pdev;
    			dawn_card->ioaddr = pci_iomap(pdev, 0, 0);
    			if (dawn_card->ioaddr == 0) {
    				pci_release_regions(pdev);
    				return -EIO;
    			}
    			dawn_card->pos = x;
    			dawn_card->irq = pdev->irq;
    			//TODO
    			//it is for each tdm bus fxs card type, SI3215 or SI3210
    			//for (y=0;y<NUM_CARDS;y++)
    			//	wc->flags[y] = d->flags;

    			dawn_card->ddev = dahdi_create_device();
    			if (!dawn_card->ddev)
    				return -ENOMEM;
    			dawn_card->ddev->location = kasprintf(GFP_KERNEL,
					      "PCI Bus %02d Slot %02d",
						  dawn_card->dev->bus->number,
					      PCI_SLOT(dawn_card->dev->devfn) + 1);

    			dawn_card->wc = kzalloc(sizeof(struct wctdm) * TDMBUS, GFP_KERNEL);
    			if(!dawn_card->wc) {
    				printk(KERN_NOTICE "wctdm: Unable to intialize Dawn base tdm buses\n");
    				return -1;
    			}
    			if (!dawn_card->ddev->location) {
    				dahdi_free_device(dawn_card->ddev);
    				dawn_card->ddev = NULL;
    				return -ENOMEM;
    			}

    			dawn_card->ddev->manufacturer = "SwitchPi";
    			dawn_card->ddev->devicetype = "DAWN TDM";
    			dawn_card->ddev->hardware_id = kasprintf(GFP_KERNEL, "%d",dawn_card->pos); ;
    			if (!dawn_card->ddev->hardware_id) {
				kfree(dawn_card->ddev->location);
				dawn_card->ddev = NULL;
				return -ENOMEM;
    			}

		/* Keep track of which device we are */
		pci_set_drvdata(pdev, dawn_card);
		/* Allocate enough memory for two zt chunks, receive and transmit.  Each sample uses
		   32 bits.  Allocate an extra set just for control too */
		for (i=0; i < TDMBUS; i++ )
		{
			dawn_card->wc[i].writechunk = (unsigned char *)pci_alloc_consistent(pdev, DAHDI_MAX_CHUNKSIZE * 32 * 2 * 2, &dawn_card->wc[i].writedma);
			if (!dawn_card->wc[i].writechunk) {
				printk(KERN_NOTICE "DAWN: Unable to allocate DMA-able memory for TDM bus 0\n");
					pci_disable_device(pdev);
					return -ENOMEM;
			}
			dawn_card->wc[i].readchunk = dawn_card->wc[i].writechunk + DAHDI_CHUNKSIZE * 32 * 2;
			dawn_card->wc[i].readdma = dawn_card->wc[i].writedma + DAHDI_CHUNKSIZE * 32 * 2;
			/* Initialize Write/Buffers to all blank data */
			memset((void *)dawn_card->wc[i].writechunk, 0, DAHDI_MAX_CHUNKSIZE  * 2 * 2 * 32);
			dawn_card->wc[i].busidx = i;

			dawn_card->wc[i].dev = dawn_card->dev;
			//set a back trace to dawn_base
			dawn_card->wc[i].bdev = dawn_card;
			dawn_card->wc[i].curcard = -1;
			dawn_card->wc[i].variety = "SwitchPi DAWN";
			dawn_card->wc[i].pos = x;
			dawn_card->wc[i].ddev = dawn_card->ddev;
		}

		//now go to initial each tdm bus, such as spi, detect its card, how many channels, etc
		//check DAWN version first, we use the tdm bus0 as it is persistent
		printk("Dawn Version is 0x0%x\n",  dawn_read(&dawn_card->wc[0], DAWN_VERSION));
		dawn_write(&dawn_card->wc[0], 0x9010, 0x45454545);
		printk("Dawn Test register is 0x%x\n",  dawn_read(&dawn_card->wc[0], 0x9010));
		//now we try to initial hardware
		for (i=0; i < TDMBUS; i++ )
		{
			spi_reset(&dawn_card->wc[i]);
			spi_initial(&dawn_card->wc[i]);
			dawn_write(&dawn_card->wc[i], 0x9028, 0x1);
			dawn_write(&dawn_card->wc[i], 0x9080, 0x1);
			dawn_write(&dawn_card->wc[i], 0x9040, 0x0);
			dma_initial(&dawn_card->wc[i]);
			fx_auto_detect(&dawn_card->wc[i]);
		}

		if (request_irq(dawn_card->irq, wctdm_interrupt, IRQF_SHARED, "wctdm", dawn_card)) {
			printk(KERN_NOTICE "wctdm: Unable to request IRQ %d\n", dawn_card->irq);
			goto error_exit;
		}

		for (i = 0; i < TDMBUS; i ++) {
			if ( 0 != wctdm_span_create(&dawn_card->wc[i])) {
				printk("Failed to create Span\n");
				return -EIO;
			}
		}

		if (dahdi_register_device(dawn_card->ddev, &dawn_card->dev->dev)) {
			printk(KERN_NOTICE "Unable to register span with DAHDI\n");
			kfree(dawn_card->ddev->location);
			dahdi_free_device(dawn_card->ddev);
			dawn_card->ddev = NULL;
			return -1;
		}

		for (i = 0; i < TDMBUS; i ++) {
			wctdm_hardware_init(&dawn_card->wc[i]);
			wctdm_post_initialize(&dawn_card->wc[i]);
			for (x = 0; x < NUM_CARDS; x++) {
				if (dawn_card->wc[i].cardflag & (1 << x))
					cardcount++;
			}
		}
		printk(KERN_INFO "Found a Wildcard TDM: %s (%d modules)\n", dawn_card->wc[0].variety, cardcount);
		//create a proc
		dma_proc_create();
		//base module pointer
		wctdm_enable_interrupts(&dawn_card->wc[0]);
		/* Start DMA */
		wctdm_start_dma(&dawn_card->wc[0]);
		res = 0;
		}
		else {
			res = -ENOMEM;
		}
	}
	return 0;
	error_exit:
		pci_iounmap(pdev, dawn_card->ioaddr);
		/* Immediately free resources */
		for (i = 0; i < TDMBUS; i ++) {
			pci_free_consistent(pdev, DAHDI_MAX_CHUNKSIZE * 2 * 2 * 32 *4, (void *)dawn_card->wc[i].writechunk, &dawn_card->wc[i].writedma);
			dawn_card->wc[i].ddev = NULL;
		}
		if (dawn_card->ddev) {
			dahdi_unregister_device(dawn_card->ddev);
			if (dawn_card->ddev->location) kfree(dawn_card->ddev->location);
			if (dawn_card->ddev) dahdi_free_device(dawn_card->ddev);
			if (dawn_card->ddev->hardware_id) kfree( dawn_card->ddev->hardware_id);
		}
		if (dawn_card->irq) {
			free_irq(dawn_card->irq, wc);
		}
		dawn_card->wc = NULL;
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		if (dawn_card) kfree(dawn_card);
		return -EIO;
}

static void wctdm_release(struct wctdm *wc)
{
	printk(KERN_INFO "Freed a Wildcard But no function in here :)\n");
}

static void __devexit wctdm_remove_one(struct pci_dev *pdev)
{
	struct dawn_base *wc = pci_get_drvdata(pdev);
	if (wc) {
		int i;
		/* Stop any DMA */
		wctdm_stop_dma(&wc->wc[0]);
		wctdm_reset_tdm(&wc->wc[0]);

		if (proc_frame)
			remove_proc_entry("frame", NULL);
		/* In case hardware is still there */
		wctdm_disable_interrupts(&wc->wc[0]);
		pci_iounmap(pdev, wc->ioaddr);
		/* Immediately free resources */
		for (i = 0; i < TDMBUS; i ++) {
			pci_free_consistent(wc->dev, DAHDI_MAX_CHUNKSIZE * 2 * 2 * 32 *4, (void *)wc->wc[i].writechunk, &wc->wc[i].writedma);
			wc->wc[i].ddev = NULL;
		}
		if (wc->ddev) {
			dahdi_unregister_device(wc->ddev);
			if (wc->ddev->location) kfree(wc->ddev->location);
			if (wc->ddev) dahdi_free_device(wc->ddev);
			if (wc->ddev->hardware_id) kfree( wc->ddev->hardware_id);
		}
		if (wc->irq) {
			free_irq(wc->irq, wc);
		}
		wc->wc = NULL;
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		if (wc) kfree(wc);
	}
	printk("Released driver\n");
}

static DEFINE_PCI_DEVICE_TABLE(wctdm_pci_tbl) = {
		{ PCI_DEVICE(0x1172, 0x0008) },
		{ PCI_DEVICE(0x1188, 0x0004) },//SwitchPi ID
		{ 0, }
};


MODULE_DEVICE_TABLE(pci, wctdm_pci_tbl);

static int wctdm_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return -ENOSYS;
}

static struct pci_driver wctdm_driver = {
	.name = "wctdm",
	.probe = wctdm_init_one,
	.remove =__devexit_p(wctdm_remove_one),
	.suspend = wctdm_suspend,
	.id_table = wctdm_pci_tbl,
};

static int __init wctdm_init(void)
{
	int res;
	int x;

	for (x = 0; x < (sizeof(fxo_modes) / sizeof(fxo_modes[0])); x++) {
		if (!strcmp(fxo_modes[x].name, opermode))
			break;
	}
	if (x < sizeof(fxo_modes) / sizeof(fxo_modes[0])) {
		_opermode = x;
	} else {
		printk(KERN_NOTICE "Invalid/unknown operating mode '%s' specified.  Please choose one of:\n", opermode);
		for (x = 0; x < sizeof(fxo_modes) / sizeof(fxo_modes[0]); x++)
			printk(KERN_INFO "  %s\n", fxo_modes[x].name);
		printk(KERN_INFO "Note this option is CASE SENSITIVE!\n");
		return -ENODEV;
	}

	if (!strcmp(opermode, "AUSTRALIA")) {
		boostringer = 1;
		fxshonormode = 1;
	}

	/* for the voicedaa_check_hook defaults, if the user has not overridden
	   them by specifying them as module parameters, then get the values
	   from the selected operating mode
	*/
	if (battdebounce == 0) {
		battdebounce = fxo_modes[_opermode].battdebounce;
	}
	if (battalarm == 0) {
		battalarm = fxo_modes[_opermode].battalarm;
	}
	if (battthresh == 0) {
		battthresh = fxo_modes[_opermode].battthresh;
	}

	res = dahdi_pci_module(&wctdm_driver);
	if (res)
		return -ENODEV;
	return 0;
}

static void __exit wctdm_cleanup(void)
{
	pci_unregister_driver(&wctdm_driver);
}

module_param(debug, int, 0600);
module_param(fxovoltage, int, 0600);
module_param(loopcurrent, int, 0600);
module_param(reversepolarity, int, 0600);
module_param(robust, int, 0600);
module_param(opermode, charp, 0600);
module_param(timingonly, int, 0600);
module_param(lowpower, int, 0600);
module_param(boostringer, int, 0600);
module_param(fastringer, int, 0600);
module_param(fxshonormode, int, 0600);
module_param(battdebounce, uint, 0600);
module_param(battalarm, uint, 0600);
module_param(battthresh, uint, 0600);
module_param(ringdebounce, int, 0600);
module_param(dialdebounce, int, 0600);
module_param(fwringdetect, int, 0600);
module_param(alawoverride, int, 0600);
module_param(fastpickup, int, 0600);
module_param(fxotxgain, int, 0600);
module_param(fxorxgain, int, 0600);
module_param(fxstxgain, int, 0600);
module_param(fxsrxgain, int, 0600);

MODULE_DESCRIPTION("SwitchPi DAWN Board Driver");
MODULE_AUTHOR("Xin Li <xin.li@switchpi.com>");
MODULE_ALIAS("wcfxs");
MODULE_LICENSE("GPL v2");

module_init(wctdm_init);
module_exit(wctdm_cleanup);
