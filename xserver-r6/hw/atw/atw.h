/*
 * atw.h - X11R6.3 ddx for the ATW800/2 "Seurat" graphics card under
 * Atari System V.
 *
 * The card is a 2 MB memory-mapped framebuffer with a video timing
 * generator, a PLL and a 256-entry RGB565 LUT at the top of its window
 * (ATW800/2 Programmer's Manual, "Programming Seurat"). We map it through
 * /dev/mem, run it at 8 bits per pixel through the LUT, and let cfb draw.
 * Input is the raw IKBD byte stream from /dev/ikbd, as the stock
 * XatariServer reads it.
 */
#ifndef ATW_H
#define ATW_H

#define NEED_EVENTS
#define NEED_REPLIES
#include "X.h"
#include "Xmd.h"
#include "Xproto.h"
#include "scrnintstr.h"
#include "input.h"
#include "inputstr.h"
#include "mipointer.h"

#define ATW_PHYS	0xFEC00000UL	/* TT, ADDR jumper open */
#define ATW_SIZE	0x00200000UL	/* 2 MB card */
#define ATW_OFF_LUT	0x1FF000UL	/* 256 x RGB565, big-endian words */
#define ATW_OFF_INFO	0x1FF200UL	/* 32-byte FPGA id string */
#define ATW_OFF_VTG	0x1FF800UL	/* word registers */
#define ATW_R_CTRL	0x00
#define ATW_R_HFP	0x02
#define ATW_R_HSY	0x04
#define ATW_R_HBP	0x06
#define ATW_R_HDI	0x08
#define ATW_R_VFP	0x0A
#define ATW_R_VSY	0x0C
#define ATW_R_VBP	0x0E
#define ATW_R_VDI	0x10
#define ATW_R_PLLFB	0x12
#define ATW_R_PLLID	0x14
#define ATW_R_PLLOD	0x16		/* write last: the PLL relocks on it */
#define ATW_R_VMLO	0x18
#define ATW_R_VMHI	0x1A
#define ATW_R_MEM	0x1E		/* 1 = 2 MB */
#define ATW_CTRL_8BPP	0x19		/* enable, 8 bpp through the LUT */

typedef struct {
    char	*name;
    int		width, height;
    int		hfp, hsy, hbp, vfp, vsy, vbp;
    int		pllfb, pllid, pllod;
} atwModeRec;

typedef struct {
    volatile unsigned char *fb;		/* card window */
    atwModeRec	*mode;
    ColormapPtr	installedMap;
} atwScreenRec;

extern atwScreenRec atwScreen;
extern int	    atwIkbdFd;
extern long	    atwLastEventTime;

extern KeySymsRec   atwKeySyms;
extern CARD8	    atwModMap[];

extern int  atwMouseProc(), atwKbdProc();
extern void atwInitModMap();
extern void atwReadInput();
extern miPointerScreenFuncRec atwPointerScreenFuncs;

/* IKBD packet codes */
#define IKBD_RELMOUSE	0xF8		/* 0xF8..0xFB: buttons in bits 0-1 */
#define IKBD_ABSMOUSE	0xF7
#define IKBD_TIME	0xFC
#define IKBD_JOYS	0xFD
#define IKBD_JOY0	0xFE
#define IKBD_JOY1	0xFF

#endif
