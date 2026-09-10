#include "keymon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QSocketNotifier>
#include <QVariantMap>

#include <algorithm>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Older kernel headers keep the codes in input.h, newer ones split them out.
#ifndef INPUT_PROP_DIRECT
#define INPUT_PROP_DIRECT 0x01
#endif
#ifndef INPUT_PROP_POINTER
#define INPUT_PROP_POINTER 0x00
#endif

namespace {

struct CodeName { int code; const char *name; };

// The kernel's own names for the key codes, from linux/input-event-codes.h.
// Carrying the table rather than deriving it keeps the readout identical on
// every target: the three Sailfish SDKs ship three different header vintages,
// and a name that changes with the build tells the reader nothing.
static const CodeName kKeyNames[] = {
    { 0, "RESERVED" },
    { 1, "ESC" },
    { 2, "1" },
    { 3, "2" },
    { 4, "3" },
    { 5, "4" },
    { 6, "5" },
    { 7, "6" },
    { 8, "7" },
    { 9, "8" },
    { 10, "9" },
    { 11, "0" },
    { 12, "MINUS" },
    { 13, "EQUAL" },
    { 14, "BACKSPACE" },
    { 15, "TAB" },
    { 16, "Q" },
    { 17, "W" },
    { 18, "E" },
    { 19, "R" },
    { 20, "T" },
    { 21, "Y" },
    { 22, "U" },
    { 23, "I" },
    { 24, "O" },
    { 25, "P" },
    { 26, "LEFTBRACE" },
    { 27, "RIGHTBRACE" },
    { 28, "ENTER" },
    { 29, "LEFTCTRL" },
    { 30, "A" },
    { 31, "S" },
    { 32, "D" },
    { 33, "F" },
    { 34, "G" },
    { 35, "H" },
    { 36, "J" },
    { 37, "K" },
    { 38, "L" },
    { 39, "SEMICOLON" },
    { 40, "APOSTROPHE" },
    { 41, "GRAVE" },
    { 42, "LEFTSHIFT" },
    { 43, "BACKSLASH" },
    { 44, "Z" },
    { 45, "X" },
    { 46, "C" },
    { 47, "V" },
    { 48, "B" },
    { 49, "N" },
    { 50, "M" },
    { 51, "COMMA" },
    { 52, "DOT" },
    { 53, "SLASH" },
    { 54, "RIGHTSHIFT" },
    { 55, "KPASTERISK" },
    { 56, "LEFTALT" },
    { 57, "SPACE" },
    { 58, "CAPSLOCK" },
    { 59, "F1" },
    { 60, "F2" },
    { 61, "F3" },
    { 62, "F4" },
    { 63, "F5" },
    { 64, "F6" },
    { 65, "F7" },
    { 66, "F8" },
    { 67, "F9" },
    { 68, "F10" },
    { 69, "NUMLOCK" },
    { 70, "SCROLLLOCK" },
    { 71, "KP7" },
    { 72, "KP8" },
    { 73, "KP9" },
    { 74, "KPMINUS" },
    { 75, "KP4" },
    { 76, "KP5" },
    { 77, "KP6" },
    { 78, "KPPLUS" },
    { 79, "KP1" },
    { 80, "KP2" },
    { 81, "KP3" },
    { 82, "KP0" },
    { 83, "KPDOT" },
    { 85, "ZENKAKUHANKAKU" },
    { 86, "102ND" },
    { 87, "F11" },
    { 88, "F12" },
    { 89, "RO" },
    { 90, "KATAKANA" },
    { 91, "HIRAGANA" },
    { 92, "HENKAN" },
    { 93, "KATAKANAHIRAGANA" },
    { 94, "MUHENKAN" },
    { 95, "KPJPCOMMA" },
    { 96, "KPENTER" },
    { 97, "RIGHTCTRL" },
    { 98, "KPSLASH" },
    { 99, "SYSRQ" },
    { 100, "RIGHTALT" },
    { 101, "LINEFEED" },
    { 102, "HOME" },
    { 103, "UP" },
    { 104, "PAGEUP" },
    { 105, "LEFT" },
    { 106, "RIGHT" },
    { 107, "END" },
    { 108, "DOWN" },
    { 109, "PAGEDOWN" },
    { 110, "INSERT" },
    { 111, "DELETE" },
    { 112, "MACRO" },
    { 113, "MUTE" },
    { 114, "VOLUMEDOWN" },
    { 115, "VOLUMEUP" },
    { 116, "POWER" },
    { 117, "KPEQUAL" },
    { 118, "KPPLUSMINUS" },
    { 119, "PAUSE" },
    { 120, "SCALE" },
    { 121, "KPCOMMA" },
    { 122, "HANGEUL" },
    { 123, "HANJA" },
    { 124, "YEN" },
    { 125, "LEFTMETA" },
    { 126, "RIGHTMETA" },
    { 127, "COMPOSE" },
    { 128, "STOP" },
    { 129, "AGAIN" },
    { 130, "PROPS" },
    { 131, "UNDO" },
    { 132, "FRONT" },
    { 133, "COPY" },
    { 134, "OPEN" },
    { 135, "PASTE" },
    { 136, "FIND" },
    { 137, "CUT" },
    { 138, "HELP" },
    { 139, "MENU" },
    { 140, "CALC" },
    { 141, "SETUP" },
    { 142, "SLEEP" },
    { 143, "WAKEUP" },
    { 144, "FILE" },
    { 145, "SENDFILE" },
    { 146, "DELETEFILE" },
    { 147, "XFER" },
    { 148, "PROG1" },
    { 149, "PROG2" },
    { 150, "WWW" },
    { 151, "MSDOS" },
    { 152, "COFFEE" },
    { 153, "ROTATE_DISPLAY" },
    { 154, "CYCLEWINDOWS" },
    { 155, "MAIL" },
    { 156, "BOOKMARKS" },
    { 157, "COMPUTER" },
    { 158, "BACK" },
    { 159, "FORWARD" },
    { 160, "CLOSECD" },
    { 161, "EJECTCD" },
    { 162, "EJECTCLOSECD" },
    { 163, "NEXTSONG" },
    { 164, "PLAYPAUSE" },
    { 165, "PREVIOUSSONG" },
    { 166, "STOPCD" },
    { 167, "RECORD" },
    { 168, "REWIND" },
    { 169, "PHONE" },
    { 170, "ISO" },
    { 171, "CONFIG" },
    { 172, "HOMEPAGE" },
    { 173, "REFRESH" },
    { 174, "EXIT" },
    { 175, "MOVE" },
    { 176, "EDIT" },
    { 177, "SCROLLUP" },
    { 178, "SCROLLDOWN" },
    { 179, "KPLEFTPAREN" },
    { 180, "KPRIGHTPAREN" },
    { 181, "NEW" },
    { 182, "REDO" },
    { 183, "F13" },
    { 184, "F14" },
    { 185, "F15" },
    { 186, "F16" },
    { 187, "F17" },
    { 188, "F18" },
    { 189, "F19" },
    { 190, "F20" },
    { 191, "F21" },
    { 192, "F22" },
    { 193, "F23" },
    { 194, "F24" },
    { 200, "PLAYCD" },
    { 201, "PAUSECD" },
    { 202, "PROG3" },
    { 203, "PROG4" },
    { 204, "ALL_APPLICATIONS" },
    { 205, "SUSPEND" },
    { 206, "CLOSE" },
    { 207, "PLAY" },
    { 208, "FASTFORWARD" },
    { 209, "BASSBOOST" },
    { 210, "PRINT" },
    { 211, "HP" },
    { 212, "CAMERA" },
    { 213, "SOUND" },
    { 214, "QUESTION" },
    { 215, "EMAIL" },
    { 216, "CHAT" },
    { 217, "SEARCH" },
    { 218, "CONNECT" },
    { 219, "FINANCE" },
    { 220, "SPORT" },
    { 221, "SHOP" },
    { 222, "ALTERASE" },
    { 223, "CANCEL" },
    { 224, "BRIGHTNESSDOWN" },
    { 225, "BRIGHTNESSUP" },
    { 226, "MEDIA" },
    { 227, "SWITCHVIDEOMODE" },
    { 228, "KBDILLUMTOGGLE" },
    { 229, "KBDILLUMDOWN" },
    { 230, "KBDILLUMUP" },
    { 231, "SEND" },
    { 232, "REPLY" },
    { 233, "FORWARDMAIL" },
    { 234, "SAVE" },
    { 235, "DOCUMENTS" },
    { 236, "BATTERY" },
    { 237, "BLUETOOTH" },
    { 238, "WLAN" },
    { 239, "UWB" },
    { 240, "UNKNOWN" },
    { 241, "VIDEO_NEXT" },
    { 242, "VIDEO_PREV" },
    { 243, "BRIGHTNESS_CYCLE" },
    { 244, "BRIGHTNESS_AUTO" },
    { 245, "DISPLAY_OFF" },
    { 246, "WWAN" },
    { 247, "RFKILL" },
    { 248, "MICMUTE" },
    { 256, "BTN_MISC" },
    { 257, "BTN_1" },
    { 258, "BTN_2" },
    { 259, "BTN_3" },
    { 260, "BTN_4" },
    { 261, "BTN_5" },
    { 262, "BTN_6" },
    { 263, "BTN_7" },
    { 264, "BTN_8" },
    { 265, "BTN_9" },
    { 272, "BTN_MOUSE" },
    { 273, "BTN_RIGHT" },
    { 274, "BTN_MIDDLE" },
    { 275, "BTN_SIDE" },
    { 276, "BTN_EXTRA" },
    { 277, "BTN_FORWARD" },
    { 278, "BTN_BACK" },
    { 279, "BTN_TASK" },
    { 288, "BTN_JOYSTICK" },
    { 289, "BTN_THUMB" },
    { 290, "BTN_THUMB2" },
    { 291, "BTN_TOP" },
    { 292, "BTN_TOP2" },
    { 293, "BTN_PINKIE" },
    { 294, "BTN_BASE" },
    { 295, "BTN_BASE2" },
    { 296, "BTN_BASE3" },
    { 297, "BTN_BASE4" },
    { 298, "BTN_BASE5" },
    { 299, "BTN_BASE6" },
    { 303, "BTN_DEAD" },
    { 304, "BTN_GAMEPAD" },
    { 305, "BTN_EAST" },
    { 306, "BTN_C" },
    { 307, "BTN_NORTH" },
    { 308, "BTN_WEST" },
    { 309, "BTN_Z" },
    { 310, "BTN_TL" },
    { 311, "BTN_TR" },
    { 312, "BTN_TL2" },
    { 313, "BTN_TR2" },
    { 314, "BTN_SELECT" },
    { 315, "BTN_START" },
    { 316, "BTN_MODE" },
    { 317, "BTN_THUMBL" },
    { 318, "BTN_THUMBR" },
    { 320, "BTN_DIGI" },
    { 321, "BTN_TOOL_RUBBER" },
    { 322, "BTN_TOOL_BRUSH" },
    { 323, "BTN_TOOL_PENCIL" },
    { 324, "BTN_TOOL_AIRBRUSH" },
    { 325, "BTN_TOOL_FINGER" },
    { 326, "BTN_TOOL_MOUSE" },
    { 327, "BTN_TOOL_LENS" },
    { 328, "BTN_TOOL_QUINTTAP" },
    { 329, "BTN_STYLUS3" },
    { 330, "BTN_TOUCH" },
    { 331, "BTN_STYLUS" },
    { 332, "BTN_STYLUS2" },
    { 333, "BTN_TOOL_DOUBLETAP" },
    { 334, "BTN_TOOL_TRIPLETAP" },
    { 335, "BTN_TOOL_QUADTAP" },
    { 336, "BTN_WHEEL" },
    { 337, "BTN_GEAR_UP" },
    { 352, "OK" },
    { 353, "SELECT" },
    { 354, "GOTO" },
    { 355, "CLEAR" },
    { 356, "POWER2" },
    { 357, "OPTION" },
    { 358, "INFO" },
    { 359, "TIME" },
    { 360, "VENDOR" },
    { 361, "ARCHIVE" },
    { 362, "PROGRAM" },
    { 363, "CHANNEL" },
    { 364, "FAVORITES" },
    { 365, "EPG" },
    { 366, "PVR" },
    { 367, "MHP" },
    { 368, "LANGUAGE" },
    { 369, "TITLE" },
    { 370, "SUBTITLE" },
    { 371, "ANGLE" },
    { 372, "FULL_SCREEN" },
    { 373, "MODE" },
    { 374, "KEYBOARD" },
    { 375, "ASPECT_RATIO" },
    { 376, "PC" },
    { 377, "TV" },
    { 378, "TV2" },
    { 379, "VCR" },
    { 380, "VCR2" },
    { 381, "SAT" },
    { 382, "SAT2" },
    { 383, "CD" },
    { 384, "TAPE" },
    { 385, "RADIO" },
    { 386, "TUNER" },
    { 387, "PLAYER" },
    { 388, "TEXT" },
    { 389, "DVD" },
    { 390, "AUX" },
    { 391, "MP3" },
    { 392, "AUDIO" },
    { 393, "VIDEO" },
    { 394, "DIRECTORY" },
    { 395, "LIST" },
    { 396, "MEMO" },
    { 397, "CALENDAR" },
    { 398, "RED" },
    { 399, "GREEN" },
    { 400, "YELLOW" },
    { 401, "BLUE" },
    { 402, "CHANNELUP" },
    { 403, "CHANNELDOWN" },
    { 404, "FIRST" },
    { 405, "LAST" },
    { 406, "AB" },
    { 407, "NEXT" },
    { 408, "RESTART" },
    { 409, "SLOW" },
    { 410, "SHUFFLE" },
    { 411, "BREAK" },
    { 412, "PREVIOUS" },
    { 413, "DIGITS" },
    { 414, "TEEN" },
    { 415, "TWEN" },
    { 416, "VIDEOPHONE" },
    { 417, "GAMES" },
    { 418, "ZOOMIN" },
    { 419, "ZOOMOUT" },
    { 420, "ZOOMRESET" },
    { 421, "WORDPROCESSOR" },
    { 422, "EDITOR" },
    { 423, "SPREADSHEET" },
    { 424, "GRAPHICSEDITOR" },
    { 425, "PRESENTATION" },
    { 426, "DATABASE" },
    { 427, "NEWS" },
    { 428, "VOICEMAIL" },
    { 429, "ADDRESSBOOK" },
    { 430, "MESSENGER" },
    { 431, "DISPLAYTOGGLE" },
    { 432, "SPELLCHECK" },
    { 433, "LOGOFF" },
    { 434, "DOLLAR" },
    { 435, "EURO" },
    { 436, "FRAMEBACK" },
    { 437, "FRAMEFORWARD" },
    { 438, "CONTEXT_MENU" },
    { 439, "MEDIA_REPEAT" },
    { 440, "10CHANNELSUP" },
    { 441, "10CHANNELSDOWN" },
    { 442, "IMAGES" },
    { 444, "NOTIFICATION_CENTER" },
    { 445, "PICKUP_PHONE" },
    { 446, "HANGUP_PHONE" },
    { 447, "LINK_PHONE" },
    { 448, "DEL_EOL" },
    { 449, "DEL_EOS" },
    { 450, "INS_LINE" },
    { 451, "DEL_LINE" },
    { 464, "FN" },
    { 465, "FN_ESC" },
    { 466, "FN_F1" },
    { 467, "FN_F2" },
    { 468, "FN_F3" },
    { 469, "FN_F4" },
    { 470, "FN_F5" },
    { 471, "FN_F6" },
    { 472, "FN_F7" },
    { 473, "FN_F8" },
    { 474, "FN_F9" },
    { 475, "FN_F10" },
    { 476, "FN_F11" },
    { 477, "FN_F12" },
    { 478, "FN_1" },
    { 479, "FN_2" },
    { 480, "FN_D" },
    { 481, "FN_E" },
    { 482, "FN_F" },
    { 483, "FN_S" },
    { 484, "FN_B" },
    { 485, "FN_RIGHT_SHIFT" },
    { 497, "BRL_DOT1" },
    { 498, "BRL_DOT2" },
    { 499, "BRL_DOT3" },
    { 500, "BRL_DOT4" },
    { 501, "BRL_DOT5" },
    { 502, "BRL_DOT6" },
    { 503, "BRL_DOT7" },
    { 504, "BRL_DOT8" },
    { 505, "BRL_DOT9" },
    { 506, "BRL_DOT10" },
    { 512, "NUMERIC_0" },
    { 513, "NUMERIC_1" },
    { 514, "NUMERIC_2" },
    { 515, "NUMERIC_3" },
    { 516, "NUMERIC_4" },
    { 517, "NUMERIC_5" },
    { 518, "NUMERIC_6" },
    { 519, "NUMERIC_7" },
    { 520, "NUMERIC_8" },
    { 521, "NUMERIC_9" },
    { 522, "NUMERIC_STAR" },
    { 523, "NUMERIC_POUND" },
    { 524, "NUMERIC_A" },
    { 525, "NUMERIC_B" },
    { 526, "NUMERIC_C" },
    { 527, "NUMERIC_D" },
    { 528, "CAMERA_FOCUS" },
    { 529, "WPS_BUTTON" },
    { 530, "TOUCHPAD_TOGGLE" },
    { 531, "TOUCHPAD_ON" },
    { 532, "TOUCHPAD_OFF" },
    { 533, "CAMERA_ZOOMIN" },
    { 534, "CAMERA_ZOOMOUT" },
    { 535, "CAMERA_UP" },
    { 536, "CAMERA_DOWN" },
    { 537, "CAMERA_LEFT" },
    { 538, "CAMERA_RIGHT" },
    { 539, "ATTENDANT_ON" },
    { 540, "ATTENDANT_OFF" },
    { 541, "ATTENDANT_TOGGLE" },
    { 542, "LIGHTS_TOGGLE" },
    { 544, "BTN_DPAD_UP" },
    { 545, "BTN_DPAD_DOWN" },
    { 546, "BTN_DPAD_LEFT" },
    { 547, "BTN_DPAD_RIGHT" },
    { 548, "BTN_GRIPL" },
    { 549, "BTN_GRIPR" },
    { 550, "BTN_GRIPL2" },
    { 551, "BTN_GRIPR2" },
    { 560, "ALS_TOGGLE" },
    { 561, "ROTATE_LOCK_TOGGLE" },
    { 562, "REFRESH_RATE_TOGGLE" },
    { 576, "BUTTONCONFIG" },
    { 577, "TASKMANAGER" },
    { 578, "JOURNAL" },
    { 579, "CONTROLPANEL" },
    { 580, "APPSELECT" },
    { 581, "SCREENSAVER" },
    { 582, "VOICECOMMAND" },
    { 583, "ASSISTANT" },
    { 584, "KBD_LAYOUT_NEXT" },
    { 585, "EMOJI_PICKER" },
    { 586, "DICTATE" },
    { 587, "CAMERA_ACCESS_ENABLE" },
    { 588, "CAMERA_ACCESS_DISABLE" },
    { 589, "CAMERA_ACCESS_TOGGLE" },
    { 590, "ACCESSIBILITY" },
    { 591, "DO_NOT_DISTURB" },
    { 592, "BRIGHTNESS_MIN" },
    { 594, "EPRIVACY_SCREEN_ON" },
    { 595, "EPRIVACY_SCREEN_OFF" },
    { 596, "ACTION_ON_SELECTION" },
    { 597, "CONTEXTUAL_INSERT" },
    { 598, "CONTEXTUAL_QUERY" },
    { 608, "KBDINPUTASSIST_PREV" },
    { 609, "KBDINPUTASSIST_NEXT" },
    { 610, "KBDINPUTASSIST_PREVGROUP" },
    { 611, "KBDINPUTASSIST_NEXTGROUP" },
    { 612, "KBDINPUTASSIST_ACCEPT" },
    { 613, "KBDINPUTASSIST_CANCEL" },
    { 614, "RIGHT_UP" },
    { 615, "RIGHT_DOWN" },
    { 616, "LEFT_UP" },
    { 617, "LEFT_DOWN" },
    { 618, "ROOT_MENU" },
    { 619, "MEDIA_TOP_MENU" },
    { 620, "NUMERIC_11" },
    { 621, "NUMERIC_12" },
    { 622, "AUDIO_DESC" },
    { 623, "3D_MODE" },
    { 624, "NEXT_FAVORITE" },
    { 625, "STOP_RECORD" },
    { 626, "PAUSE_RECORD" },
    { 627, "VOD" },
    { 628, "UNMUTE" },
    { 629, "FASTREVERSE" },
    { 630, "SLOWREVERSE" },
    { 631, "DATA" },
    { 632, "ONSCREEN_KEYBOARD" },
    { 633, "PRIVACY_SCREEN_TOGGLE" },
    { 634, "SELECTIVE_SCREENSHOT" },
    { 635, "NEXT_ELEMENT" },
    { 636, "PREVIOUS_ELEMENT" },
    { 637, "AUTOPILOT_ENGAGE_TOGGLE" },
    { 638, "MARK_WAYPOINT" },
    { 639, "SOS" },
    { 640, "NAV_CHART" },
    { 641, "FISHING_CHART" },
    { 642, "SINGLE_RANGE_RADAR" },
    { 643, "DUAL_RANGE_RADAR" },
    { 644, "RADAR_OVERLAY" },
    { 645, "TRADITIONAL_SONAR" },
    { 646, "CLEARVU_SONAR" },
    { 647, "SIDEVU_SONAR" },
    { 648, "NAV_INFO" },
    { 649, "BRIGHTNESS_MENU" },
    { 656, "MACRO1" },
    { 657, "MACRO2" },
    { 658, "MACRO3" },
    { 659, "MACRO4" },
    { 660, "MACRO5" },
    { 661, "MACRO6" },
    { 662, "MACRO7" },
    { 663, "MACRO8" },
    { 664, "MACRO9" },
    { 665, "MACRO10" },
    { 666, "MACRO11" },
    { 667, "MACRO12" },
    { 668, "MACRO13" },
    { 669, "MACRO14" },
    { 670, "MACRO15" },
    { 671, "MACRO16" },
    { 672, "MACRO17" },
    { 673, "MACRO18" },
    { 674, "MACRO19" },
    { 675, "MACRO20" },
    { 676, "MACRO21" },
    { 677, "MACRO22" },
    { 678, "MACRO23" },
    { 679, "MACRO24" },
    { 680, "MACRO25" },
    { 681, "MACRO26" },
    { 682, "MACRO27" },
    { 683, "MACRO28" },
    { 684, "MACRO29" },
    { 685, "MACRO30" },
    { 688, "MACRO_RECORD_START" },
    { 689, "MACRO_RECORD_STOP" },
    { 690, "MACRO_PRESET_CYCLE" },
    { 691, "MACRO_PRESET1" },
    { 692, "MACRO_PRESET2" },
    { 693, "MACRO_PRESET3" },
    { 696, "KBD_LCD_MENU1" },
    { 697, "KBD_LCD_MENU2" },
    { 698, "KBD_LCD_MENU3" },
    { 699, "KBD_LCD_MENU4" },
    { 700, "KBD_LCD_MENU5" },
    { 701, "PERFORMANCE" },
    { 704, "BTN_TRIGGER_HAPPY" },
    { 705, "BTN_TRIGGER_HAPPY2" },
    { 706, "BTN_TRIGGER_HAPPY3" },
    { 707, "BTN_TRIGGER_HAPPY4" },
    { 708, "BTN_TRIGGER_HAPPY5" },
    { 709, "BTN_TRIGGER_HAPPY6" },
    { 710, "BTN_TRIGGER_HAPPY7" },
    { 711, "BTN_TRIGGER_HAPPY8" },
    { 712, "BTN_TRIGGER_HAPPY9" },
    { 713, "BTN_TRIGGER_HAPPY10" },
    { 714, "BTN_TRIGGER_HAPPY11" },
    { 715, "BTN_TRIGGER_HAPPY12" },
    { 716, "BTN_TRIGGER_HAPPY13" },
    { 717, "BTN_TRIGGER_HAPPY14" },
    { 718, "BTN_TRIGGER_HAPPY15" },
    { 719, "BTN_TRIGGER_HAPPY16" },
    { 720, "BTN_TRIGGER_HAPPY17" },
    { 721, "BTN_TRIGGER_HAPPY18" },
    { 722, "BTN_TRIGGER_HAPPY19" },
    { 723, "BTN_TRIGGER_HAPPY20" },
    { 724, "BTN_TRIGGER_HAPPY21" },
    { 725, "BTN_TRIGGER_HAPPY22" },
    { 726, "BTN_TRIGGER_HAPPY23" },
    { 727, "BTN_TRIGGER_HAPPY24" },
    { 728, "BTN_TRIGGER_HAPPY25" },
    { 729, "BTN_TRIGGER_HAPPY26" },
    { 730, "BTN_TRIGGER_HAPPY27" },
    { 731, "BTN_TRIGGER_HAPPY28" },
    { 732, "BTN_TRIGGER_HAPPY29" },
    { 733, "BTN_TRIGGER_HAPPY30" },
    { 734, "BTN_TRIGGER_HAPPY31" },
    { 735, "BTN_TRIGGER_HAPPY32" },
    { 736, "BTN_TRIGGER_HAPPY33" },
    { 737, "BTN_TRIGGER_HAPPY34" },
    { 738, "BTN_TRIGGER_HAPPY35" },
    { 739, "BTN_TRIGGER_HAPPY36" },
    { 740, "BTN_TRIGGER_HAPPY37" },
    { 741, "BTN_TRIGGER_HAPPY38" },
    { 742, "BTN_TRIGGER_HAPPY39" },
    { 743, "BTN_TRIGGER_HAPPY40" },
};

static const CodeName kSwitchNames[] = {
    { 0, "LID" },
    { 1, "TABLET_MODE" },
    { 2, "HEADPHONE_INSERT" },
    { 3, "RFKILL_ALL" },
    { 4, "MICROPHONE_INSERT" },
    { 5, "DOCK" },
    { 6, "LINEOUT_INSERT" },
    { 7, "JACK_PHYSICAL_INSERT" },
    { 8, "VIDEOOUT_INSERT" },
    { 9, "CAMERA_LENS_COVER" },
    { 10, "KEYPAD_SLIDE" },
    { 11, "FRONT_PROXIMITY" },
    { 12, "ROTATE_LOCK" },
    { 13, "LINEIN_INSERT" },
    { 14, "MUTE_DEVICE" },
    { 15, "PEN_INSERTED" },
    { 16, "MACHINE_COVER" },
    { 17, "USB_INSERT" },};

static QString lookup(const CodeName *tab, int n, int code)
{
    for (int i = 0; i < n; ++i)
        if (tab[i].code == code)
            return QString::fromLatin1(tab[i].name);
    return QString();
}

static inline bool bitSet(const unsigned char *b, int bit)
{
    return (b[bit / 8] >> (bit % 8)) & 1;
}

} // namespace

QString KeyMon::keyName(int code)
{
    const QString n = lookup(kKeyNames, (int)(sizeof(kKeyNames) / sizeof(kKeyNames[0])), code);
    return n.isEmpty() ? QStringLiteral("code %1").arg(code) : n;
}

KeyMon::KeyMon(QObject *parent) : QObject(parent)
{
}

KeyMon::~KeyMon()
{
    closeAll();
}

// Walk /dev/input and ask each node what it is. Everything here comes from the
// input core itself -- the name the driver registered, the bus and the
// vendor/product/version the hardware answered with, and the bitmaps of what
// the device says it can report. Nothing is inferred from the name.
void KeyMon::refresh()
{
    m_devices.clear();
    m_keys.clear();
    m_switches.clear();
    m_held.clear();
    m_seenCount = 0;

    const QDir d(QStringLiteral("/dev/input"));
    const QStringList nodes = d.entryList(QStringList() << QStringLiteral("event*"),
                                          QDir::System | QDir::Files, QDir::Name);
    QStringList denied;
    // event10 must sort after event9, which a plain string sort does not do.
    QVector<QPair<int, QString>> ordered;
    for (const QString &n : nodes)
        ordered.append(qMakePair(n.mid(5).toInt(), n));
    std::sort(ordered.begin(), ordered.end());

    for (const auto &entry : ordered) {
        const QString path = d.filePath(entry.second);
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            denied.append(entry.second);
            continue;
        }
        char nameBuf[256] = { 0 };
        char physBuf[256] = { 0 };
        struct input_id id;
        memset(&id, 0, sizeof(id));
        if (::ioctl(fd, EVIOCGNAME(sizeof(nameBuf) - 1), nameBuf) < 0)
            nameBuf[0] = 0;
        if (::ioctl(fd, EVIOCGPHYS(sizeof(physBuf) - 1), physBuf) < 0)
            physBuf[0] = 0;
        ::ioctl(fd, EVIOCGID, &id);

        unsigned char evBits[(EV_MAX + 8) / 8];
        unsigned char keyBits[(KEY_MAX + 8) / 8];
        unsigned char swBits[(SW_MAX + 8) / 8];
        unsigned char swState[(SW_MAX + 8) / 8];
        unsigned char keyState[(KEY_MAX + 8) / 8];
        unsigned char absBits[(ABS_MAX + 8) / 8];
        unsigned char propBits[(INPUT_PROP_MAX + 8) / 8];
        memset(evBits, 0, sizeof(evBits));
        memset(keyBits, 0, sizeof(keyBits));
        memset(swBits, 0, sizeof(swBits));
        memset(swState, 0, sizeof(swState));
        memset(keyState, 0, sizeof(keyState));
        memset(absBits, 0, sizeof(absBits));
        memset(propBits, 0, sizeof(propBits));
        ::ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits);
        const bool hasKey = bitSet(evBits, EV_KEY);
        const bool hasSw = bitSet(evBits, EV_SW);
        const bool hasAbs = bitSet(evBits, EV_ABS);
        if (hasKey) {
            ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);
            // Not every key is a button. A latching switch - the slider on the
            // left of this phone - is wired as a key that stays down for as
            // long as it sits in that position, so its code is held rather
            // than pressed. EVIOCGKEY reports what is held right now, which is
            // the only way to tell a position from an event: a button that is
            // not being touched reads as released, a slider reads as its
            // position.
            ::ioctl(fd, EVIOCGKEY(sizeof(keyState)), keyState);
        }
        if (hasSw) {
            ::ioctl(fd, EVIOCGBIT(EV_SW, sizeof(swBits)), swBits);
            ::ioctl(fd, EVIOCGSW(sizeof(swState)), swState);
        }
        if (hasAbs)
            ::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
        ::ioctl(fd, EVIOCGPROP(sizeof(propBits)), propBits);

        // A touchscreen is a device that reports absolute positions on the
        // surface you look at: multi-touch slots, or INPUT_PROP_DIRECT. It
        // declares keys too, and those are not buttons.
        const bool touch = (hasAbs && bitSet(absBits, ABS_MT_POSITION_X))
                           || bitSet(propBits, INPUT_PROP_DIRECT);

        QStringList keyNames;
        QVariantList keyCodes, heldList;
        if (hasKey) {
            for (int c = 0; c <= KEY_MAX; ++c) {
                if (!bitSet(keyBits, c))
                    continue;
                keyCodes.append(c);
                keyNames.append(keyName(c));
                if (bitSet(keyState, c)) {
                    QVariantMap h;
                    h.insert(QStringLiteral("code"), c);
                    h.insert(QStringLiteral("name"), keyName(c));
                    h.insert(QStringLiteral("device"), QString::fromLatin1(nameBuf));
                    heldList.append(h);
                    m_held.append(h);
                }
            }
        }
        QVariantList swList;
        if (hasSw) {
            for (int c = 0; c <= SW_MAX; ++c) {
                if (!bitSet(swBits, c))
                    continue;
                QVariantMap s;
                const QString nm = lookup(kSwitchNames,
                                          (int)(sizeof(kSwitchNames) / sizeof(kSwitchNames[0])), c);
                s.insert(QStringLiteral("code"), c);
                s.insert(QStringLiteral("name"),
                         nm.isEmpty() ? QStringLiteral("code %1").arg(c) : nm);
                s.insert(QStringLiteral("closed"), bitSet(swState, c));
                s.insert(QStringLiteral("device"), QString::fromLatin1(nameBuf));
                swList.append(s);
                m_switches.append(s);
            }
        }

        QVariantMap m;
        m.insert(QStringLiteral("node"), entry.second);
        m.insert(QStringLiteral("name"), QString::fromLatin1(nameBuf));
        m.insert(QStringLiteral("phys"), QString::fromLatin1(physBuf));
        m.insert(QStringLiteral("bus"), QStringLiteral("%1").arg(id.bustype, 4, 16, QLatin1Char('0')));
        m.insert(QStringLiteral("vendor"), QStringLiteral("%1").arg(id.vendor, 4, 16, QLatin1Char('0')));
        m.insert(QStringLiteral("product"), QStringLiteral("%1").arg(id.product, 4, 16, QLatin1Char('0')));
        m.insert(QStringLiteral("version"), QStringLiteral("%1").arg(id.version, 4, 16, QLatin1Char('0')));
        m.insert(QStringLiteral("hasKeys"), hasKey);
        m.insert(QStringLiteral("touch"), touch);
        m.insert(QStringLiteral("keyCount"), keyCodes.size());
        m.insert(QStringLiteral("keyNames"), keyNames);
        m.insert(QStringLiteral("switches"), swList);
        m.insert(QStringLiteral("held"), heldList);
        m_devices.append(m);

        // The test list: keys from everything that is not the touchscreen.
        if (hasKey && !touch) {
            for (int i = 0; i < keyCodes.size() && m_keys.size() < 400; ++i) {
                QVariantMap k;
                k.insert(QStringLiteral("code"), keyCodes.at(i));
                k.insert(QStringLiteral("name"), keyNames.at(i));
                k.insert(QStringLiteral("device"), QString::fromLatin1(nameBuf));
                k.insert(QStringLiteral("node"), entry.second);
                k.insert(QStringLiteral("seen"), false);
                k.insert(QStringLiteral("pressed"), false);
                m_keys.append(k);
            }
        }
        ::close(fd);
    }

    m_openError = denied.isEmpty()
        ? QString()
        : tr("%1 of %2 event nodes could not be opened (%3)")
              .arg(denied.size()).arg(denied.size() + m_devices.size())
              .arg(QString::fromLocal8Bit(strerror(EACCES)));
    emit devicesChanged();
    emit keysChanged();
    emit listeningChanged();
}

void KeyMon::start()
{
    if (!m_fds.isEmpty())
        return;
    if (m_devices.isEmpty())
        refresh();

    QStringList failed;
    for (const QVariant &v : m_devices) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("hasKeys")).toBool()
            && m.value(QStringLiteral("switches")).toList().isEmpty())
            continue;
        if (m.value(QStringLiteral("touch")).toBool())
            continue;
        const QString node = m.value(QStringLiteral("node")).toString();
        const QString path = QStringLiteral("/dev/input/") + node;
        // Read-only and never EVIOCGRAB: the compositor keeps every event.
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            failed.append(node);
            continue;
        }
        Node n;
        n.fd = fd;
        n.name = m.value(QStringLiteral("name")).toString();
        n.dev = node;
        m_fds.append(n);
        QSocketNotifier *sn = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(sn, &QSocketNotifier::activated, this, &KeyMon::readReady);
        m_notifiers.append(sn);
    }
    if (!failed.isEmpty())
        m_openError = tr("Cannot read %1 — the app is not in the input group")
                          .arg(failed.join(QStringLiteral(", ")));
    else if (m_fds.isEmpty())
        m_openError = tr("No button device could be opened");
    else
        m_openError.clear();
    emit listeningChanged();
}

void KeyMon::stop()
{
    closeAll();
    emit listeningChanged();
}

void KeyMon::closeAll()
{
    for (QSocketNotifier *sn : m_notifiers) {
        sn->setEnabled(false);
        sn->deleteLater();
    }
    m_notifiers.clear();
    for (const Node &n : m_fds)
        ::close(n.fd);
    m_fds.clear();
}

void KeyMon::resetSeen()
{
    for (int i = 0; i < m_keys.size(); ++i) {
        QVariantMap k = m_keys.at(i).toMap();
        k.insert(QStringLiteral("seen"), false);
        k.insert(QStringLiteral("pressed"), false);
        m_keys[i] = k;
    }
    m_seenCount = 0;
    m_lastKey.clear();
    m_lastDevice.clear();
    m_lastCode = -1;
    m_lastPressed = false;
    emit keysChanged();
    emit lastChanged();
}

void KeyMon::markSeen(int code, bool pressed, const QString &device)
{
    bool changed = false;
    for (int i = 0; i < m_keys.size(); ++i) {
        QVariantMap k = m_keys.at(i).toMap();
        if (k.value(QStringLiteral("code")).toInt() != code
            || k.value(QStringLiteral("device")).toString() != device)
            continue;
        if (!k.value(QStringLiteral("seen")).toBool()) {
            k.insert(QStringLiteral("seen"), true);
            ++m_seenCount;
        }
        k.insert(QStringLiteral("pressed"), pressed);
        m_keys[i] = k;
        changed = true;
    }
    if (changed)
        emit keysChanged();
}

void KeyMon::readReady(int fd)
{
    QString devName;
    QString node;
    for (const Node &n : m_fds) {
        if (n.fd == fd) {
            devName = n.name;
            node = n.dev;
            break;
        }
    }
    struct input_event ev[32];
    for (;;) {
        const ssize_t got = ::read(fd, ev, sizeof(ev));
        if (got <= 0)
            break;
        const int count = (int)(got / sizeof(struct input_event));
        for (int i = 0; i < count; ++i) {
            if (ev[i].type == EV_KEY) {
                // value 2 is auto-repeat; it says the key is still down.
                const bool down = ev[i].value != 0;
                m_lastCode = ev[i].code;
                m_lastKey = keyName(ev[i].code);
                m_lastPressed = down;
                m_lastDevice = devName;
                markSeen(ev[i].code, down, devName);
                emit lastChanged();
            } else if (ev[i].type == EV_SW) {
                const QString nm = lookup(kSwitchNames,
                                          (int)(sizeof(kSwitchNames) / sizeof(kSwitchNames[0])),
                                          ev[i].code);
                m_lastCode = ev[i].code;
                m_lastKey = (nm.isEmpty() ? QStringLiteral("switch %1").arg(ev[i].code)
                                          : nm) + QStringLiteral(" (switch)");
                m_lastPressed = ev[i].value != 0;
                m_lastDevice = devName;
                for (int s = 0; s < m_switches.size(); ++s) {
                    QVariantMap sm = m_switches.at(s).toMap();
                    if (sm.value(QStringLiteral("code")).toInt() == ev[i].code
                        && sm.value(QStringLiteral("device")).toString() == devName) {
                        sm.insert(QStringLiteral("closed"), ev[i].value != 0);
                        m_switches[s] = sm;
                        emit devicesChanged();
                    }
                }
                emit lastChanged();
            }
        }
        if (got < (ssize_t)sizeof(ev))
            break;
    }
}
