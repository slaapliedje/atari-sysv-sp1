/*
 * From Ash Nazag (github.com/kdedon/ashnazag, kernel/atari/netusbee,
 * 5ae4923), MIT licence: see LICENSE.ashnazag.
 *
 * DP8390 (RTL8019AS) registers as seen through the cartridge port.
 *
 * The port is read-only.  Reads come from the ROM4 window; chip register
 * r sits at window + (r << 9) and its byte arrives on D8-D15, so a byte
 * read at that even address returns it.  A write is a read from the ROM3
 * window with the data byte on A1-A8:
 *
 *   read  reg r      byte at 0xFFFA0000 + (r << 9)
 *   write reg r = v  read 0xFFFB0000 + (r << 9) + (v << 1)
 */
#ifndef NUREG_H
#define NUREG_H

/* page 0 */
#define NU_CR		0x00
#define NU_PSTART	0x01	/* w */
#define NU_PSTOP	0x02	/* w */
#define NU_BNRY		0x03
#define NU_TPSR		0x04	/* w */
#define NU_TSR		0x04	/* r */
#define NU_TBCR0	0x05	/* w */
#define NU_TBCR1	0x06	/* w */
#define NU_ISR		0x07
#define NU_RSAR0	0x08	/* w */
#define NU_RSAR1	0x09	/* w */
#define NU_RBCR0	0x0a	/* w */
#define NU_RBCR1	0x0b	/* w */
#define NU_ID0		0x0a	/* r, RTL8019: 'P' */
#define NU_ID1		0x0b	/* r, RTL8019: 'p' */
#define NU_RCR		0x0c	/* w */
#define NU_TCR		0x0d	/* w */
#define NU_CNTR0	0x0d	/* r, frame alignment errors */
#define NU_DCR		0x0e	/* w */
#define NU_CNTR1	0x0e	/* r, CRC errors */
#define NU_IMR		0x0f	/* w */
#define NU_CNTR2	0x0f	/* r, missed frames */
/* page 1 */
#define NU_PAR0		0x01	/* station address, 6 */
#define NU_CURR		0x07
#define NU_MAR0		0x08	/* multicast hash, 8 */
/* page 2: what page 0 was given, read back */
#define NU_P2PSTART	0x01
#define NU_P2PSTOP	0x02
#define NU_P2TPSR	0x04
#define NU_P2RCR	0x0c	/* low 6 bits */
#define NU_P2TCR	0x0d	/* low 5 bits */
#define NU_P2DCR	0x0e	/* low 7 bits */
#define NU_P2IMR	0x0f	/* low 7 bits */
/* page 3 (RTL8019AS only): the EEPROM port and the configuration */
#define NU_9346CR	0x01
#define NU_CONFIG0	0x03	/* r */
#define NU_CONFIG1	0x04
#define NU_CONFIG2	0x05
#define NU_CONFIG3	0x06
#define EEM_CONFIG	0xc0	/* 9346CR: CONFIG1-3 writable */
/* NE2000 ASIC */
#define NU_DATA		0x10
#define NU_RESET	0x1f

/* CR */
#define CR_STP		0x01
#define CR_STA		0x02
#define CR_TXP		0x04
#define CR_RREAD	0x08
#define CR_RWRITE	0x10
#define CR_NODMA	0x20
#define CR_PAGE1	0x40
#define CR_PAGE2	0x80
#define CR_PAGE3	(CR_PAGE1 | CR_PAGE2)

/* ISR */
#define ISR_PRX		0x01
#define ISR_PTX		0x02
#define ISR_RXE		0x04
#define ISR_TXE		0x08
#define ISR_OVW		0x10
#define ISR_CNT		0x20
#define ISR_RDC		0x40
#define ISR_RST		0x80

/* DCR: byte transfers, normal operation, 8-byte FIFO threshold */
#define DCR_LS		0x08
#define DCR_FT1		0x40

/* RCR */
#define RCR_AB		0x04
#define RCR_AM		0x08
#define RCR_PRO		0x10
#define RCR_MON		0x20

/* TCR */
#define TCR_LOOP	0x02

/* TSR */
#define TSR_PTX		0x01
#define TSR_COL		0x04
#define TSR_ABT		0x08
#define TSR_CRS		0x10
#define TSR_FU		0x20
#define TSR_OWC		0x80

/* receive header status (RSR) */
#define RSR_PRX		0x01

#define NU_RDBASE	0xFFFA0000
#define NU_WRBASE	0xFFFB0000
#define NU_REGSHIFT	9

#endif
