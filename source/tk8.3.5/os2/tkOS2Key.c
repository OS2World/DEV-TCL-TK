/* 
 * tkOS2Key.c --
 *
 *	This file contains X emulation routines for keyboard related
 *	functions.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1996-2003 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkOS2Int.h"
/*
 * The keymap table holds mappings of OS/2 keycodes to X keysyms.
 * If OS/2 ever comes along and changes the value of their keycodes,
 * this will break all kinds of things.  However, this table lookup is much
 * faster than the alternative, in which we walked a list of keycodes looking
 * for a match.  Since this lookup is performed for every OS/2 keypress
 * event, it seems like a worthwhile improvement to use the table.
 */
#define MAX_KEYCODE 0x0041 /* the last entry in our table below */
static KeySym keymap[] = {
    /*                  0x0000 */ NoSymbol,
    /* VK_BUTTON1	0x0001 */ NoSymbol,
    /* VK_BUTTON2	0x0002 */ NoSymbol,
    /* VK_BUTTON3	0x0003 */ NoSymbol,
    /* VK_BREAK		0x0004 */ XK_Cancel,
    /* VK_BACKSPACE	0x0005 */ XK_BackSpace,
    /* VK_TAB		0x0006 */ XK_Tab,
    /* VK_BACKTAB	0x0007 */ NoSymbol,
    /* VK_NEWLINE	0x0008 */ XK_Return,
    /* VK_SHIFT		0x0009 */ XK_Shift_L,
    /* VK_CTRL		0x000a */ XK_Control_L,
    /* VK_ALT		0x000b */ XK_Alt_L,
    /* VK_ALTGRAF	0x000c */ XK_Alt_R,
    /* VK_PAUSE		0x000d */ XK_Pause,
    /* VK_CAPSLOCK	0x000e */ XK_Caps_Lock,
    /* VK_ESC		0x000f */ XK_Escape,
    /* VK_SPACE		0x0010 */ XK_space, /* lowercase 'space' is NO error! */
    /* VK_PAGEUP	0x0011 */ XK_Prior,
    /* VK_PAGEDOWN	0x0012 */ XK_Next,
    /* VK_END		0x0013 */ XK_End,
    /* VK_HOME		0x0014 */ XK_Home,
    /* VK_LEFT		0x0015 */ XK_Left,
    /* VK_UP		0x0016 */ XK_Up,
    /* VK_RIGHT		0x0017 */ XK_Right,
    /* VK_DOWN		0x0018 */ XK_Down,
    /* VK_PRINTSCRN	0x0019 */ XK_Print,
    /* VK_INSERT	0x001a */ XK_Insert,
    /* VK_DELETE	0x001b */ XK_Delete,
    /* VK_SCRLLOCK	0x001c */ XK_Scroll_Lock,
    /* VK_NUMLOCK	0x001d */ XK_Num_Lock,
    /* VK_ENTER		0x001e */ XK_Return,
    /* VK_SYSRQ		0x001f */ XK_Execute,
    /* VK_F1		0x0020 */ XK_F1,
    /* VK_F2		0x0021 */ XK_F2,
    /* VK_F3		0x0022 */ XK_F3,
    /* VK_F4		0x0023 */ XK_F4,
    /* VK_F5		0x0024 */ XK_F5,
    /* VK_F6		0x0025 */ XK_F6,
    /* VK_F7		0x0026 */ XK_F6,
    /* VK_F8		0x0027 */ XK_F8,
    /* VK_F9		0x0028 */ XK_F9,
    /* VK_F10		0x0029 */ XK_F10,
    /* VK_F11		0x002a */ XK_F11,
    /* VK_F12		0x002b */ XK_F12,
    /* VK_F13		0x002c */ XK_F13,
    /* VK_F14		0x002d */ XK_F14,
    /* VK_F15		0x002e */ XK_F15,
    /* VK_F16		0x002f */ XK_F16,
    /* VK_F17		0x0030 */ XK_F17,
    /* VK_F18		0x0031 */ XK_F18,
    /* VK_F19		0x0032 */ XK_F19,
    /* VK_F20		0x0033 */ XK_F20,
    /* VK_F21		0x0034 */ XK_F21,
    /* VK_F22		0x0035 */ XK_F22,
    /* VK_F23		0x0036 */ XK_F23,
    /* VK_F24		0x0037 */ XK_F24,
    /* VK_ENDDRAG	0x0038 */ NoSymbol,
    /* VK_CLEAR		0x0039 */ NoSymbol,
    /* VK_EREOF		0x003a */ NoSymbol,
    /* VK_PA1		0x003b */ NoSymbol,
    /* VK_ATTN		0x003c */ NoSymbol,
    /* VK_CRSEL		0x003d */ NoSymbol,
    /* VK_EXSEL		0x003e */ NoSymbol,
    /* VK_COPY		0x003f */ NoSymbol,
    /* VK_BLK1		0x0040 */ NoSymbol,
    /* VK_BLK2		0x0041 */ NoSymbol
};

/*
 * Prototypes for local procedures defined in this file:
 */

static KeySym           KeycodeToKeysym _ANSI_ARGS_((unsigned int keycode,
                            int state, int noascii));

/*
 *----------------------------------------------------------------------
 *
 * XLookupString --
 *
 * TkpGetString --
 *
 *      Retrieve the UTF string equivalent for the given keyboard event.
 *
 * Results:
 *      Returns the UTF string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TkpGetString(winPtr, eventPtr, dsPtr)
    TkWindow *winPtr;           /* Window where event occurred:  needed to
                                 * get input context. */
    XEvent *eventPtr;           /* X keyboard event. */
    Tcl_DString *dsPtr;         /* Uninitialized or empty string to hold
                                 * result. */
{
    KeySym keysym;
    XKeyEvent* keyEv = &eventPtr->xkey;

#ifdef VERBOSE
    printf("XLookupString\n");
#endif

    Tcl_DStringInit(dsPtr);
    if (eventPtr->xkey.send_event == -1) {
        if (eventPtr->xkey.nbytes > 0) {
            Tcl_ExternalToUtfDString(NULL, eventPtr->xkey.trans_chars,
                                     eventPtr->xkey.nbytes, dsPtr);
        }
    } else if (eventPtr->xkey.send_event == -2) {
        /*
         * Special case for win2000 multi-lingal IME input.
         * xkey.trans_chars[] already contains a UNICODE char.
         */

        int unichar;
        char buf[TCL_UTF_MAX];
        int len;

        unichar = (eventPtr->xkey.trans_chars[1] & 0xff);
        unichar <<= 8;
        unichar |= (eventPtr->xkey.trans_chars[0] & 0xff);

        len = Tcl_UniCharToUtf((Tcl_UniChar) unichar, buf);

        Tcl_DStringAppend(dsPtr, buf, len);
    } else  {
        /*
         * This is an event generated from generic code.  It has no
         * nchars or trans_chars members.
         */

        keysym = KeycodeToKeysym(eventPtr->xkey.keycode,
                eventPtr->xkey.state, 0);
        if (((keysym != NoSymbol) && (keysym > 0) && (keysym < 256))
                || (keysym == XK_Return)
                || (keysym == XK_Tab)) {
            char buf[TCL_UTF_MAX];
            int len = Tcl_UniCharToUtf((Tcl_UniChar) (keysym & 255), buf);
            Tcl_DStringAppend(dsPtr, buf, len);
        }
    }
    return Tcl_DStringValue(dsPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * XKeycodeToKeysym --
 *
 *	Translate from a system-dependent keycode to a
 *	system-independent keysym.
 *
 * Results:
 *	Returns the translated keysym, or NoSymbol on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeySym
XKeycodeToKeysym(display, keycode, index)
    Display* display;
    unsigned int keycode;
    int index;
{
    int state = 0;

    if (index & 0x01) {
        state |= ShiftMask;
    }
    return KeycodeToKeysym(keycode, state, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * KeycodeToKeysym --
 *
 *      Translate from a system-dependent keycode to a
 *      system-independent keysym.
 *
 * Results:
 *      Returns the translated keysym, or NoSymbol on failure.
 *
 * Side effects:
 *      It may affect the internal state of the keyboard, such as
 *      remembered dead key or lock indicator lamps.
 *
 *----------------------------------------------------------------------
 */

static KeySym
KeycodeToKeysym(keycode, state, noascii)
    unsigned int keycode;
    int state;
    int noascii;
{
    int result, deadkey, shift;

    if (noascii || keycode == VK_CAPSLOCK || keycode == VK_SCRLLOCK ||
            keycode == VK_NUMLOCK)
        goto skipToAscii;

    /* Move to VK_USER range */
    result = (keycode > 0x120 && keycode < 0x200);

#ifdef VERBOSE
    printf("KeycodeToKeysym keycode %x (%c), state %d, noascii %d\n", keycode,
           keycode, state, noascii);
#endif


    /*
     * Keycode mapped to a valid Latin-1 character.  Since the keysyms
     * for alphanumeric characters map onto Latin-1, we just return it.
     *
     * We treat 0x7F as a special case mostly for backwards compatibility.
     * In versions of Tk<=8.2, Control-Backspace returned "XK_BackSpace"
     * as the X Keysym.  This was due to the fact that we did not
     * initialize the keys array properly when we passed it to ToAscii, above.
     * We had previously not been setting the state bit for the Control key.
     * When we fixed that, we found that Control-Backspace on Windows is
     * interpreted as ASCII-127 (0x7F), which corresponds to the Delete key.
     *
     * Upon discovering this, we realized we had two choices:  return XK_Delete
     * or return XK_BackSpace.  If we returned XK_Delete, that could be
     * considered "more correct" (although the correctness would be dependant
     * on whether you believe that ToAscii is doing the right thing in that
     * case); however, this would break backwards compatibility, and worse,
     * it would limit application programmers -- they would effectively be
     * unable to bind to <Control-Backspace> on Windows.  We therefore chose
     * instead to return XK_BackSpace (handled here by letting the code
     * "fall-through" to the return statement below, which works because the
     * keycode for this event is VK_BACKSPACE, and the keymap table maps that
     * keycode to XK_BackSpace).
     */

    if (result && keycode >= 0x20 /* && keycode != 0x7F */) {
	keycode -= 0x100;
#ifdef VERBOSE
        printf("KeycodeToKeysym returning char %x (%c)\n", keycode, keycode);
#endif
	return (KeySym) keycode;
    }

    /*
     * Keycode is a non-alphanumeric key, so we have to do the lookup.
     */

    skipToAscii:
    if (keycode < 0 || keycode > MAX_KEYCODE) {
        return NoSymbol;
    }
        /*
         * OS/2 only gives us an undifferentiated VK_CTRL
         * code (for example) when either Control key is pressed.
         * There is no way to distinguish between the left and the right
         * ones, except for the _asynchronous_ hardware scan code check.
         * I won't get into that kind of hacking (especially since OS/2
         * DOES already distinguish between the left ALT (VK_ALT) and the
         * right one on some keyboards (VK_ALTGRAF on eg. NL keyboard).
         */
#ifdef VERBOSE
    printf("KeycodeToKeysym returning entry %x (%x)\n", keycode,
           keymap[keycode]);
#endif
    return keymap[keycode];
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *      Given an X KeyPress or KeyRelease event, map the
 *      keycode in the event into a KeySym.
 *
 * Results:
 *      The return value is the KeySym corresponding to
 *      eventPtr, or NoSymbol if no matching Keysym could be
 *      found.
 *
 * Side effects:
 *      In the first call for a given display, keycode-to-
 *      KeySym maps get loaded.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(dispPtr, eventPtr)
    TkDisplay *dispPtr;         /* Display in which to map keycode. */
    XEvent *eventPtr;           /* Description of X event. */
{
    KeySym sym;
    int state = eventPtr->xkey.state;

    /*
     * Refresh the mapping information if it's stale
     */

    if (dispPtr->bindInfoStale) {
        TkpInitKeymapInfo(dispPtr);
    }

    sym = KeycodeToKeysym(eventPtr->xkey.keycode, state, 0);

    /*
     * Special handling: if this is a ctrl-alt or shifted key, and there
     * is no keysym defined, try without the modifiers.
     */

    if ((sym == NoSymbol) && ((state & ControlMask) || (state & Mod2Mask))) {
        state &=  ~(ControlMask | Mod2Mask);
        sym = KeycodeToKeysym(eventPtr->xkey.keycode, state, 0);
    }
    if ((sym == NoSymbol) && (state & ShiftMask)) {
        state &=  ~ShiftMask;
        sym = KeycodeToKeysym(eventPtr->xkey.keycode, state, 0);
    }
    return sym;
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitKeymapInfo --
 *
 *      This procedure is invoked to scan keymap information
 *      to recompute stuff that's important for binding, such
 *      as the modifier key (if any) that corresponds to "mode
 *      switch".
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Keymap-related information in dispPtr is updated.
 *
 *--------------------------------------------------------------
 */

void
TkpInitKeymapInfo(dispPtr)
    TkDisplay *dispPtr;         /* Display for which to recompute keymap
                                 * information. */
{
    XModifierKeymap *modMapPtr;
    KeyCode *codePtr;
    KeySym keysym;
    int count, i, j, max, arraySize;
#define KEYCODE_ARRAY_SIZE 20

    dispPtr->bindInfoStale = 0;
    modMapPtr = XGetModifierMapping(dispPtr->display);

    /*
     * Check the keycodes associated with the Lock modifier.  If
     * any of them is associated with the XK_Shift_Lock modifier,
     * then Lock has to be interpreted as Shift Lock, not Caps Lock.
     */

    dispPtr->lockUsage = LU_IGNORE;
    codePtr = modMapPtr->modifiermap + modMapPtr->max_keypermod*LockMapIndex;
    for (count = modMapPtr->max_keypermod; count > 0; count--, codePtr++) {
        if (*codePtr == 0) {
            continue;
        }
        keysym = KeycodeToKeysym(*codePtr, 0, 1);
        if (keysym == XK_Shift_Lock) {
            dispPtr->lockUsage = LU_SHIFT;
            break;
        }
        if (keysym == XK_Caps_Lock) {
            dispPtr->lockUsage = LU_CAPS;
            break;
        }
    }

    /*
     * Look through the keycodes associated with modifiers to see if
     * the the "mode switch", "meta", or "alt" keysyms are associated
     * with any modifiers.  If so, remember their modifier mask bits.
     */

    dispPtr->modeModMask = 0;
    dispPtr->metaModMask = 0;
    dispPtr->altModMask = 0;
    codePtr = modMapPtr->modifiermap;
    max = 8*modMapPtr->max_keypermod;
    for (i = 0; i < max; i++, codePtr++) {
        if (*codePtr == 0) {
            continue;
        }
        keysym = KeycodeToKeysym(*codePtr, 0, 1);
        if (keysym == XK_Mode_switch) {
            dispPtr->modeModMask |= ShiftMask << (i/modMapPtr->max_keypermod);
        }
        if ((keysym == XK_Meta_L) || (keysym == XK_Meta_R)) {
            dispPtr->metaModMask |= ShiftMask << (i/modMapPtr->max_keypermod);
        }
        if ((keysym == XK_Alt_L) || (keysym == XK_Alt_R)) {
            dispPtr->altModMask |= ShiftMask << (i/modMapPtr->max_keypermod);
        }
    }

    /*
     * Create an array of the keycodes for all modifier keys.
     */

    if (dispPtr->modKeyCodes != NULL) {
        ckfree((char *) dispPtr->modKeyCodes);
    }
    dispPtr->numModKeyCodes = 0;
    arraySize = KEYCODE_ARRAY_SIZE;
    dispPtr->modKeyCodes = (KeyCode *) ckalloc((unsigned)
            (KEYCODE_ARRAY_SIZE * sizeof(KeyCode)));
    for (i = 0, codePtr = modMapPtr->modifiermap; i < max; i++, codePtr++) {
        if (*codePtr == 0) {
            continue;
        }

        /*
         * Make sure that the keycode isn't already in the array.
         */

        for (j = 0; j < dispPtr->numModKeyCodes; j++) {
            if (dispPtr->modKeyCodes[j] == *codePtr) {
                goto nextModCode;
            }
        }
        if (dispPtr->numModKeyCodes >= arraySize) {
            KeyCode *new;

            /*
             * Ran out of space in the array;  grow it.
             */

            arraySize *= 2;
            new = (KeyCode *) ckalloc((unsigned)
                    (arraySize * sizeof(KeyCode)));
            memcpy((VOID *) new, (VOID *) dispPtr->modKeyCodes,
                    (dispPtr->numModKeyCodes * sizeof(KeyCode)));
            ckfree((char *) dispPtr->modKeyCodes);
            dispPtr->modKeyCodes = new;
        }
        dispPtr->modKeyCodes[dispPtr->numModKeyCodes] = *codePtr;
        dispPtr->numModKeyCodes++;
        nextModCode: continue;
    }
    XFreeModifiermap(modMapPtr);
}

/*
 * When mapping from a keysym to a keycode, need
 * information about the modifier state that should be used
 * so that when they call XKeycodeToKeysym taking into
 * account the xkey.state, they will get back the original
 * keysym.
 */

void
TkpSetKeycodeAndState(tkwin, keySym, eventPtr)
    Tk_Window tkwin;
    KeySym keySym;
    XEvent *eventPtr;
{
    int i;
    SHORT result;
    int shift;

    eventPtr->xkey.keycode = 0;
    if (keySym == NoSymbol) {
        return;
    }

    /*
     * We check our private map first for a virtual keycode,
     * as VkKeyScan will return values that don't map to X
     * for the "extended" Syms.  This may be due to just casting
     * problems below, but this works.
     */
    for (i = 0; i <= MAX_KEYCODE; i++) {
        if (keymap[i] == keySym) {
            eventPtr->xkey.keycode = i;
            return;
        }
    }
    if (keySym >= 0x20 && keySym <= 0xFF) {
#ifdef VERBOSE
        printf("  returning keySym + 0x100: %d\n", keySym + 0x100);
#endif
        eventPtr->xkey.keycode = (KeyCode) (keySym + 0x100);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToKeycode --
 *
 *	Translate a keysym back into a keycode.
 *
 * Results:
 *	Returns the keycode that would generate the specified keysym.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeyCode
XKeysymToKeycode(display, keysym)
    Display* display;
    KeySym keysym;
{
    int i;
    LONG result;

#ifdef VERBOSE
    printf("XKeysymToKeycode\n");
#endif

    /*
     * We check our private map first for a virtual keycode.
     */
    if (keysym == NoSymbol) {
        return 0;
    }
    for (i = 0; i <= MAX_KEYCODE; i++) {
        if (keymap[i] == keysym) {
            return ((KeyCode) i);
        }
    }
    if (keysym >= 0x20 && keysym <= 0xFF) {
#ifdef VERBOSE
        printf("  returning keysym + 0x100: %d\n", keysym + 0x100);
#endif
        return (KeyCode) (keysym + 0x100);
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetModifierMapping --
 *
 *	Fetch the current keycodes used as modifiers.
 *
 * Results:
 *	Returns a new modifier map.
 *
 * Side effects:
 *	Allocates a new modifier map data structure.
 *
 *----------------------------------------------------------------------
 */

XModifierKeymap	*
XGetModifierMapping(display)
    Display* display;
{
    XModifierKeymap *map = (XModifierKeymap *)ckalloc(sizeof(XModifierKeymap));

#ifdef VERBOSE
    printf("XGetModifierMapping\n");
#endif

    map->max_keypermod = 1;
    map->modifiermap = (KeyCode *) ckalloc(sizeof(KeyCode)*8);
    map->modifiermap[ShiftMapIndex] = VK_SHIFT;
    map->modifiermap[LockMapIndex] = VK_CAPSLOCK;
    map->modifiermap[ControlMapIndex] = VK_CTRL;
    map->modifiermap[Mod1MapIndex] = VK_NUMLOCK;
    map->modifiermap[Mod2MapIndex] = VK_MENU;
    map->modifiermap[Mod3MapIndex] = VK_SCRLLOCK;
    map->modifiermap[Mod4MapIndex] = VK_ALT;
    map->modifiermap[Mod5MapIndex] = 0;
    return map;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeModifiermap --
 *
 *	Deallocate a modifier map that was created by
 *	XGetModifierMapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the datastructure referenced by modmap.
 *
 *----------------------------------------------------------------------
 */

void
XFreeModifiermap(modmap)
    XModifierKeymap* modmap;
{
    ckfree((char *) modmap->modifiermap);
    ckfree((char *) modmap);
}

/*
 *----------------------------------------------------------------------
 *
 * XStringToKeysym --
 *
 *	Translate a keysym name to the matching keysym. 
 *
 * Results:
 *	Returns the keysym.  Since this is already handled by
 *	Tk's StringToKeysym function, we just return NoSymbol.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeySym
XStringToKeysym(string)
    _Xconst char *string;
{
    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToString --
 *
 *	Convert a keysym to character form.
 *
 * Results:
 *	Returns NULL, since Tk will have handled this already.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
XKeysymToString(keysym)
    KeySym keysym;
{
    return NULL;
}
