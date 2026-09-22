/*
 * atwKMap.c - keysyms for the Atari TT keyboard, indexed by IKBD scan
 * code (US layout). Keycode = scan code + 8. Two columns: plain, shifted.
 */
#define NEED_EVENTS
#define NEED_REPLIES
#include "X.h"
#include "Xmd.h"
#include "Xproto.h"
#include "keysym.h"
#include "input.h"
#include "inputstr.h"

#define K(a, b)	a, b
#define N	NoSymbol, NoSymbol

static KeySym atwMap[] = {
/* 00 */ N,
/* 01 */ K(XK_Escape, NoSymbol),
/* 02 */ K(XK_1, XK_exclam),
/* 03 */ K(XK_2, XK_at),
/* 04 */ K(XK_3, XK_numbersign),
/* 05 */ K(XK_4, XK_dollar),
/* 06 */ K(XK_5, XK_percent),
/* 07 */ K(XK_6, XK_asciicircum),
/* 08 */ K(XK_7, XK_ampersand),
/* 09 */ K(XK_8, XK_asterisk),
/* 0a */ K(XK_9, XK_parenleft),
/* 0b */ K(XK_0, XK_parenright),
/* 0c */ K(XK_minus, XK_underscore),
/* 0d */ K(XK_equal, XK_plus),
/* 0e */ K(XK_BackSpace, NoSymbol),
/* 0f */ K(XK_Tab, NoSymbol),
/* 10 */ K(XK_q, XK_Q),
/* 11 */ K(XK_w, XK_W),
/* 12 */ K(XK_e, XK_E),
/* 13 */ K(XK_r, XK_R),
/* 14 */ K(XK_t, XK_T),
/* 15 */ K(XK_y, XK_Y),
/* 16 */ K(XK_u, XK_U),
/* 17 */ K(XK_i, XK_I),
/* 18 */ K(XK_o, XK_O),
/* 19 */ K(XK_p, XK_P),
/* 1a */ K(XK_bracketleft, XK_braceleft),
/* 1b */ K(XK_bracketright, XK_braceright),
/* 1c */ K(XK_Return, NoSymbol),
/* 1d */ K(XK_Control_L, NoSymbol),
/* 1e */ K(XK_a, XK_A),
/* 1f */ K(XK_s, XK_S),
/* 20 */ K(XK_d, XK_D),
/* 21 */ K(XK_f, XK_F),
/* 22 */ K(XK_g, XK_G),
/* 23 */ K(XK_h, XK_H),
/* 24 */ K(XK_j, XK_J),
/* 25 */ K(XK_k, XK_K),
/* 26 */ K(XK_l, XK_L),
/* 27 */ K(XK_semicolon, XK_colon),
/* 28 */ K(XK_apostrophe, XK_quotedbl),
/* 29 */ K(XK_grave, XK_asciitilde),
/* 2a */ K(XK_Shift_L, NoSymbol),
/* 2b */ K(XK_backslash, XK_bar),
/* 2c */ K(XK_z, XK_Z),
/* 2d */ K(XK_x, XK_X),
/* 2e */ K(XK_c, XK_C),
/* 2f */ K(XK_v, XK_V),
/* 30 */ K(XK_b, XK_B),
/* 31 */ K(XK_n, XK_N),
/* 32 */ K(XK_m, XK_M),
/* 33 */ K(XK_comma, XK_less),
/* 34 */ K(XK_period, XK_greater),
/* 35 */ K(XK_slash, XK_question),
/* 36 */ K(XK_Shift_R, NoSymbol),
/* 37 */ N,
/* 38 */ K(XK_Alt_L, NoSymbol),
/* 39 */ K(XK_space, NoSymbol),
/* 3a */ K(XK_Caps_Lock, NoSymbol),
/* 3b */ K(XK_F1, NoSymbol),
/* 3c */ K(XK_F2, NoSymbol),
/* 3d */ K(XK_F3, NoSymbol),
/* 3e */ K(XK_F4, NoSymbol),
/* 3f */ K(XK_F5, NoSymbol),
/* 40 */ K(XK_F6, NoSymbol),
/* 41 */ K(XK_F7, NoSymbol),
/* 42 */ K(XK_F8, NoSymbol),
/* 43 */ K(XK_F9, NoSymbol),
/* 44 */ K(XK_F10, NoSymbol),
/* 45 */ N,
/* 46 */ N,
/* 47 */ K(XK_Home, NoSymbol),
/* 48 */ K(XK_Up, NoSymbol),
/* 49 */ N,
/* 4a */ K(XK_KP_Subtract, NoSymbol),
/* 4b */ K(XK_Left, NoSymbol),
/* 4c */ N,
/* 4d */ K(XK_Right, NoSymbol),
/* 4e */ K(XK_KP_Add, NoSymbol),
/* 4f */ N,
/* 50 */ K(XK_Down, NoSymbol),
/* 51 */ N,
/* 52 */ K(XK_Insert, NoSymbol),
/* 53 */ K(XK_Delete, NoSymbol),
/* 54 */ N, /* 55 */ N, /* 56 */ N, /* 57 */ N,
/* 58 */ N, /* 59 */ N, /* 5a */ N, /* 5b */ N,
/* 5c */ N, /* 5d */ N, /* 5e */ N, /* 5f */ N,
/* 60 */ K(XK_less, XK_greater),		/* ISO key */
/* 61 */ K(XK_Undo, NoSymbol),
/* 62 */ K(XK_Help, NoSymbol),
/* 63 */ K(XK_parenleft, NoSymbol),		/* keypad ( */
/* 64 */ K(XK_parenright, NoSymbol),		/* keypad ) */
/* 65 */ K(XK_KP_Divide, NoSymbol),
/* 66 */ K(XK_KP_Multiply, NoSymbol),
/* 67 */ K(XK_KP_7, NoSymbol),
/* 68 */ K(XK_KP_8, NoSymbol),
/* 69 */ K(XK_KP_9, NoSymbol),
/* 6a */ K(XK_KP_4, NoSymbol),
/* 6b */ K(XK_KP_5, NoSymbol),
/* 6c */ K(XK_KP_6, NoSymbol),
/* 6d */ K(XK_KP_1, NoSymbol),
/* 6e */ K(XK_KP_2, NoSymbol),
/* 6f */ K(XK_KP_3, NoSymbol),
/* 70 */ K(XK_KP_0, NoSymbol),
/* 71 */ K(XK_KP_Decimal, NoSymbol),
/* 72 */ K(XK_KP_Enter, NoSymbol),
};

#define MIN_KEYCODE	8
#define NSCAN		(sizeof atwMap / sizeof atwMap[0] / 2)

KeySymsRec atwKeySyms = {
    atwMap,			/* map */
    MIN_KEYCODE,		/* minKeyCode: scan code 0 */
    MIN_KEYCODE + NSCAN - 1,	/* maxKeyCode: 0x72 */
    2				/* mapWidth */
};

/* modifier map, indexed by keycode */
CARD8 atwModMap[MAP_LENGTH] = {
    /* filled in at first use, see atwInitModMap */
};

void
atwInitModMap()
{
    atwModMap[MIN_KEYCODE + 0x2a] = ShiftMask;
    atwModMap[MIN_KEYCODE + 0x36] = ShiftMask;
    atwModMap[MIN_KEYCODE + 0x3a] = LockMask;
    atwModMap[MIN_KEYCODE + 0x1d] = ControlMask;
    atwModMap[MIN_KEYCODE + 0x38] = Mod1Mask;
}
