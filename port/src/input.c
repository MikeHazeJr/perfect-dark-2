#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#include "platform.h"
#include "input.h"
#include "video.h"
#include "config.h"
#include "utils.h"
#include "system.h"
#include "fs.h"
#include "pdgui.h"
#include "actionmap.h"
#include "inputctx.h"   /* B-259: route SDL_ShowCursor through the authority */

#if !SDL_VERSION_ATLEAST(2, 0, 14)
// this was added in 2.0.14
#define SDL_CONTROLLER_TYPE_VIRTUAL SDL_CONTROLLER_TYPE_UNKNOWN
#endif

#define CONTROLLERDB_FNAME "gamecontrollerdb.txt"

#define TRIG_THRESHOLD (30 * 256)
#define DEFAULT_DEADZONE 4096
#define DEFAULT_DEADZONE_RY 6144

#define WHEEL_UP_MASK SDL_BUTTON(VK_MOUSE_WHEEL_UP - VK_MOUSE_BEGIN + 1)
#define WHEEL_DN_MASK SDL_BUTTON(VK_MOUSE_WHEEL_DN - VK_MOUSE_BEGIN + 1)

#define CURSOR_HIDE_THRESHOLD 1
#define CURSOR_HIDE_TIME 3000000 // us

static SDL_GameController *pads[INPUT_MAX_CONTROLLERS];

#define CONTROLLERCFG_DEFAULT { \
	.rumbleOn = 0, \
	.rumbleScale = 0.5f, \
	.axisMap = { \
		{ SDL_CONTROLLER_AXIS_LEFTX,  SDL_CONTROLLER_AXIS_LEFTY  }, \
		{ SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY }, \
	}, \
	.sens = { 1.f, 1.f, 1.f, 1.f }, \
	.deadzone = { DEFAULT_DEADZONE, DEFAULT_DEADZONE, DEFAULT_DEADZONE, DEFAULT_DEADZONE_RY }, \
	.stickCButtons = 0, \
	.swapSticks = 1, \
	.deviceIndex = -1, \
	.cancelCButtons = 0, \
	.invertRStickY = 0, \
}

static struct controllercfg {
	s32 rumbleOn;
	f32 rumbleScale;
	u32 axisMap[2][2];
	f32 sens[4];
	s32 deadzone[4];
	s32 stickCButtons;
	s32 swapSticks;
	s32 deviceIndex;
	s32 cancelCButtons;
	s32 invertRStickY;
} padsCfg[INPUT_MAX_CONTROLLERS] = {
	CONTROLLERCFG_DEFAULT,
	CONTROLLERCFG_DEFAULT,
	CONTROLLERCFG_DEFAULT,
	CONTROLLERCFG_DEFAULT
};

/* M0.2 Phase D: CK_* bind arrays (binds[], bindStrs[]) removed.
 * All binding is now managed by actionmap.h (g_ImcGameplay, actionmapBind, etc.). */

static s32 fakeControllers = 0;
static s32 firstController = 0;
static s32 connectedMask = 0;

static s32 numJoysticks = 0;

static s32 useHIDAPI = 1;
static s32 useRawInput = 1;

static s32 mouseEnabled = 1;
static s32 mouseX, mouseY;
static s32 mouseDX, mouseDY;
static u32 mouseButtons;
static s32 mouseWheel = 0;

static s32 mouseLocked = 0;
static s32 mouseLockMode = MLOCK_AUTO;
static u64 mouseCursorTime = 0;
static s32 mouseShowCursor = 1;

static f32 mouseSensX = 2.5f;
static f32 mouseSensY = 2.5f;

static s32 lastKey = 0;
static char lastChar = 0;
static s32 textInput = 0;

static char *clipboardText = NULL;

/* M0.2 Phase D: ckNames[] removed — CK_* system replaced by actionmap. */

static const char *vkPunctNames[] = {
	"MINUS", "EQUALS", "LEFTBRACKET", "RIGHTBRACKET", "BACKSLASH",
	"HASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "PERIOD", "SLASH"
};

static const char *vkMouseNames[] = {
	"MOUSE_LEFT",
	"MOUSE_MIDDLE",
	"MOUSE_RIGHT",
	"MOUSE_X1",
	"MOUSE_X2",
	"MOUSE_WHEEL_UP",
	"MOUSE_WHEEL_DN",
};

static const char *vkJoyNames[] = {
	"JOY1_A",
	"JOY1_B",
	"JOY1_X",
	"JOY1_Y",
	"JOY1_BACK",
	"JOY1_GUIDE",
	"JOY1_START",
	"JOY1_LSTICK",
	"JOY1_RSTICK",
	"JOY1_LSHOULDER",
	"JOY1_RSHOULDER",
	"JOY1_DPAD_UP",
	"JOY1_DPAD_DOWN",
	"JOY1_DPAD_LEFT",
	"JOY1_DPAD_RIGHT",
	"JOY1_BUTTON_15",
	"JOY1_BUTTON_16",
	"JOY1_BUTTON_17",
	"JOY1_BUTTON_18",
	"JOY1_BUTTON_19",
	"JOY1_TOUCHPAD",
	"JOY1_BUTTON_21",
	"JOY1_LSTICK_LEFT",
	"JOY1_LSTICK_RIGHT",
	"JOY1_LSTICK_UP",
	"JOY1_LSTICK_DOWN",
	"JOY1_RSTICK_LEFT",
	"JOY1_RSTICK_RIGHT",
	"JOY1_RSTICK_UP",
	"JOY1_RSTICK_DOWN",
	"JOY1_LTRIGGER",
	"JOY1_RTRIGGER",
};

static char vkNames[VK_TOTAL_COUNT][64];

static s8 vkPrevState[VK_TOTAL_COUNT];

/* M0.2 Phase D: inputSetDefaultKeyBinds() removed — use actionmapSetDefaults() instead. */

static inline s32 inputDeviceIndexFromId(const SDL_JoystickID id) {
	for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
		if (SDL_JoystickGetDeviceInstanceID(jidx) == id) {
			return jidx;
		}
	}
	return -1;
}

static inline SDL_JoystickID inputControllerGetId(SDL_GameController *ctrl)
{
	return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
}

static inline void inputInitController(const s32 cidx, const s32 jidx)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// SDL_GameControllerHasRumble() appeared in 2.0.18 even though SDL_GameControllerRumble() is in 2.0.9
	padsCfg[cidx].rumbleOn = SDL_GameControllerHasRumble(pads[cidx]);
#else
	// assume that all joysticks with haptic feedback support will support rumble
	padsCfg[cidx].rumbleOn = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[cidx]));
	if (!padsCfg[cidx].rumbleOn) {
		// at least on Windows some controllers will report no haptics, but rumble will still function
		// just assume it's supported if the controller is of known type
		const SDL_GameControllerType ctype = SDL_GameControllerGetType(pads[cidx]);
		padsCfg[cidx].rumbleOn = ctype && (ctype != SDL_CONTROLLER_TYPE_VIRTUAL);
	}
#endif

	// make the LEDs on the controller indicate which player it's for
	SDL_GameControllerSetPlayerIndex(pads[cidx], cidx);

	// remember the joystick index
	padsCfg[cidx].deviceIndex = jidx;

	connectedMask |= (1 << cidx);

	sysLogPrintf(LOG_NOTE, "input: assigned controller '%d: (%s)' (id %d) to player %d",
		jidx, SDL_GameControllerName(pads[cidx]), inputControllerGetId(pads[cidx]), cidx);

	SDL_Joystick* joy = SDL_GameControllerGetJoystick(pads[cidx]);
	if (joy) {
		char guidStr[1024] = "";
		SDL_JoystickGUID guid = SDL_JoystickGetGUID(joy);
		SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
		sysLogPrintf(LOG_NOTE, "input: GUID for controller %d: %s", jidx, guidStr);
	}
}

static inline void inputCloseController(const s32 cidx)
{
	sysLogPrintf(LOG_NOTE, "input: removed controller '%d: (%s)' (id %d) from player %d",
		padsCfg[cidx].deviceIndex, SDL_GameControllerName(pads[cidx]), inputControllerGetId(pads[cidx]), cidx);

	// reset player LEDs
	SDL_GameControllerSetPlayerIndex(pads[cidx], -1);

	SDL_GameControllerClose(pads[cidx]);

	pads[cidx] = NULL;
	padsCfg[cidx].rumbleOn = 0;

	if (cidx) {
		connectedMask &= ~(1 << cidx);
	}
}

static inline s32 inputControllerGetIndex(SDL_GameController *ctrl)
{
	if (ctrl) {
		for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
			if (pads[i] == ctrl) {
				return i;
			}
		}
	}
	return -1;
}

static inline s32 inputControllerGetIndexByDeviceIndex(const s32 jidx)
{
	for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
		if (pads[cidx] && padsCfg[cidx].deviceIndex == jidx) {
			return cidx;
		}
	}
	return -1;
}

static inline s32 inputControllerGetIndexById(const SDL_JoystickID jid)
{
	for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
		if (pads[cidx]) {
			if (inputControllerGetId(pads[cidx]) == jid) {
				return cidx;
			}
		}
	}
	return -1;
}

static inline void inputCloseAllControllers(void)
{
	for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
		if (pads[cidx]) {
			inputCloseController(cidx);
			pads[cidx] = NULL;
		}
	}

	connectedMask = 1; // always report first controller as connected
}

static inline s32 inputTryController(const s32 cidx, const s32 jidx)
{
	if (!pads[cidx]) {
		pads[cidx] = SDL_GameControllerOpen(jidx);
		if (pads[cidx]) {
			inputInitController(cidx, jidx);
			return 1;
		}
	}
	return 0;
}

static inline void inputInitAllControllers(void)
{
	SDL_GameControllerUpdate();

	numJoysticks = SDL_NumJoysticks();

	connectedMask = 1; // always report first controller as connected

	// first try to assign the controllers that we had last time
	// we're still free to check by device index before any controller device events fire
	for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
		const s32 jidx = padsCfg[cidx].deviceIndex;
		if (jidx >= 0 && jidx < numJoysticks) {
			if (SDL_IsGameController(jidx) && inputControllerGetIndexByDeviceIndex(jidx) < 0) {
				// using the full assign function in case user sets same index for several players
				if (inputTryController(cidx, jidx)) {
					// success
					continue;
				}
			}
			// nothing was there, forget it
			padsCfg[cidx].deviceIndex = -1;
		}
	}

	// now try autofilling the rest, starting with firstController
	for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
		if (SDL_IsGameController(jidx) && inputControllerGetIndexByDeviceIndex(jidx) < 0) {
			for (s32 cidx = firstController; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
				if (inputTryController(cidx, jidx)) {
					break;
				}
			}
		}
	}

	const s32 overrideMask = (1 << fakeControllers) - 1;
	if (overrideMask) {
		connectedMask = overrideMask;
	}
}

static int inputEventFilter(void *data, SDL_Event *event)
{
	switch (event->type) {
		case SDL_CONTROLLERDEVICEADDED:
			for (s32 i = firstController; i < INPUT_MAX_CONTROLLERS; ++i) {
				if (!pads[i]) {
					pads[i] = SDL_GameControllerOpen(event->cdevice.which);
					if (pads[i]) {
						inputInitController(i, event->cdevice.which);
					}
					break;
				}
			}
			break;

		case SDL_CONTROLLERDEVICEREMOVED: {
			SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
			const s32 idx = inputControllerGetIndex(ctrl);
			if (idx >= 0) {
				inputCloseController(idx);
				padsCfg[idx].deviceIndex = -1;
			}
			break;
		}

		case SDL_JOYDEVICEADDED:
		case SDL_JOYDEVICEREMOVED:
			numJoysticks = SDL_NumJoysticks(); // joystick count has changed
			break;

		case SDL_MOUSEWHEEL:
			mouseWheel = event->wheel.y;
			if (!lastKey && mouseWheel) {
				lastKey = (mouseWheel < 0) + VK_MOUSE_WHEEL_UP;
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
			if (!lastKey) {
				lastKey = VK_MOUSE_BEGIN - 1 + event->button.button;
			}
			break;

		case SDL_KEYDOWN:
			if (!lastKey) {
				lastKey = VK_KEYBOARD_BEGIN + event->key.keysym.scancode;
			}
			break;

		case SDL_CONTROLLERBUTTONDOWN:
			if (!lastKey) {
				lastKey = VK_JOY1_BEGIN + event->cbutton.button;
				SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
				const s32 idx = inputControllerGetIndex(ctrl);
				if (idx >= 0) {
					lastKey += idx * INPUT_MAX_CONTROLLER_BUTTONS;
				}
			}
			break;

		case SDL_CONTROLLERAXISMOTION:
			if (!lastKey) {
				if (event->caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT && event->caxis.value > TRIG_THRESHOLD) {
					lastKey = VK_JOY1_LTRIG + (event->caxis.axis - SDL_CONTROLLER_AXIS_TRIGGERLEFT);
					SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
					const s32 idx = inputControllerGetIndex(ctrl);
					if (idx >= 0) {
						lastKey += idx * INPUT_MAX_CONTROLLER_BUTTONS;
					}
				}
				/* Stick axis directions — synthetic VKs for rebinding capture */
				else if (event->caxis.value > TRIG_THRESHOLD || event->caxis.value < -TRIG_THRESHOLD) {
					s32 vk = 0;
					switch (event->caxis.axis) {
					case SDL_CONTROLLER_AXIS_LEFTX:
						vk = (event->caxis.value < 0) ? VK_JOY1_LSTICK_LEFT : VK_JOY1_LSTICK_RIGHT;
						break;
					case SDL_CONTROLLER_AXIS_LEFTY:
						vk = (event->caxis.value < 0) ? VK_JOY1_LSTICK_UP : VK_JOY1_LSTICK_DOWN;
						break;
					case SDL_CONTROLLER_AXIS_RIGHTX:
						vk = (event->caxis.value < 0) ? VK_JOY1_RSTICK_LEFT : VK_JOY1_RSTICK_RIGHT;
						break;
					case SDL_CONTROLLER_AXIS_RIGHTY:
						vk = (event->caxis.value < 0) ? VK_JOY1_RSTICK_UP : VK_JOY1_RSTICK_DOWN;
						break;
					}
					if (vk) {
						lastKey = vk;
						SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
						const s32 idx = inputControllerGetIndex(ctrl);
						if (idx >= 0) {
							lastKey += idx * INPUT_MAX_CONTROLLER_BUTTONS;
						}
					}
				}
			}
			break;

		case SDL_TEXTINPUT:
			if (!lastChar && event->text.text[0] && (u8)event->text.text[0] < 0x80) {
				lastChar = event->text.text[0];
			}
			break;

		default:
			break;
	}

	return 0;
}

static inline void inputGetScancodeName(const SDL_Scancode sc, char *out, size_t len)
{
		const char *scname = SDL_GetScancodeName(sc);
		if (scname) {
			strncpy(out, scname, len - 1);
			out[len - 1] = '\0';
			for (u32 i = 0; i < len && out[i]; ++i) {
				if (out[i] == ' ') {
					out[i] = '_';
				} else {
					out[i] = toupper(out[i]);
				}
			}
		} else {
			snprintf(out, len, "KEY%d", (s32)sc);
		}
}

static inline void inputInitKeyNames(void)
{
	for (SDL_Scancode key = SDL_SCANCODE_A; key <= SDL_SCANCODE_SPACE; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	// special characters
	for (SDL_Scancode key = SDL_SCANCODE_MINUS; key < SDL_SCANCODE_CAPSLOCK; ++key) {
		strncpy(vkNames[key], vkPunctNames[key - SDL_SCANCODE_MINUS], sizeof(vkNames[key]) - 1);
		vkNames[key][sizeof(vkNames[key]) - 1] = '\0';
	}

	for (SDL_Scancode key = SDL_SCANCODE_CAPSLOCK; key <= SDL_SCANCODE_NUMLOCKCLEAR; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	// keypad names
	strncpy(vkNames[SDL_SCANCODE_KP_DIVIDE], "KP_DIVIDE", sizeof(vkNames[SDL_SCANCODE_KP_DIVIDE]) - 1);
	vkNames[SDL_SCANCODE_KP_DIVIDE][sizeof(vkNames[SDL_SCANCODE_KP_DIVIDE]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_MULTIPLY], "KP_MULTIPLY", sizeof(vkNames[SDL_SCANCODE_KP_MULTIPLY]) - 1);
	vkNames[SDL_SCANCODE_KP_MULTIPLY][sizeof(vkNames[SDL_SCANCODE_KP_MULTIPLY]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_MINUS], "KP_MINUS", sizeof(vkNames[SDL_SCANCODE_KP_MINUS]) - 1);
	vkNames[SDL_SCANCODE_KP_MINUS][sizeof(vkNames[SDL_SCANCODE_KP_MINUS]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_PLUS], "KP_PLUS", sizeof(vkNames[SDL_SCANCODE_KP_PLUS]) - 1);
	vkNames[SDL_SCANCODE_KP_PLUS][sizeof(vkNames[SDL_SCANCODE_KP_PLUS]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_ENTER], "KP_ENTER", sizeof(vkNames[SDL_SCANCODE_KP_ENTER]) - 1);
	vkNames[SDL_SCANCODE_KP_ENTER][sizeof(vkNames[SDL_SCANCODE_KP_ENTER]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_PERIOD], "KP_PERIOD", sizeof(vkNames[SDL_SCANCODE_KP_PERIOD]) - 1);
	vkNames[SDL_SCANCODE_KP_PERIOD][sizeof(vkNames[SDL_SCANCODE_KP_PERIOD]) - 1] = '\0';
	strncpy(vkNames[SDL_SCANCODE_KP_EQUALS], "KP_EQUALS", sizeof(vkNames[SDL_SCANCODE_KP_EQUALS]) - 1);
	vkNames[SDL_SCANCODE_KP_EQUALS][sizeof(vkNames[SDL_SCANCODE_KP_EQUALS]) - 1] = '\0';
	for (SDL_Scancode key = SDL_SCANCODE_KP_1; key < SDL_SCANCODE_KP_0; ++key) {
		char tmp[8] = "KP_1";
		tmp[3] = '1' + (key - SDL_SCANCODE_KP_1);
		strncpy(vkNames[key], tmp, sizeof(vkNames[key]) - 1);
		vkNames[key][sizeof(vkNames[key]) - 1] = '\0';
	}

	for (SDL_Scancode key = SDL_SCANCODE_LCTRL; key <= SDL_SCANCODE_RGUI; ++key) {
		inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
	}

	strncpy(vkNames[VK_CHORD_CTRL_TAB], "CTRL+TAB", sizeof(vkNames[VK_CHORD_CTRL_TAB]) - 1);
	strncpy(vkNames[VK_CHORD_CTRL_SHIFT_TAB], "CTRL+SHIFT+TAB", sizeof(vkNames[VK_CHORD_CTRL_SHIFT_TAB]) - 1);
	strncpy(vkNames[VK_CHORD_CTRL_Z], "CTRL+Z", sizeof(vkNames[VK_CHORD_CTRL_Z]) - 1);
	strncpy(vkNames[VK_CHORD_CTRL_SHIFT_Z], "CTRL+SHIFT+Z", sizeof(vkNames[VK_CHORD_CTRL_SHIFT_Z]) - 1);
	strncpy(vkNames[VK_CHORD_CTRL_Y], "CTRL+Y", sizeof(vkNames[VK_CHORD_CTRL_Y]) - 1);
	strncpy(vkNames[VK_CHORD_CTRL_S], "CTRL+S", sizeof(vkNames[VK_CHORD_CTRL_S]) - 1);

	// mouse names
	for (u32 vk = VK_MOUSE_BEGIN; vk < VK_JOY1_BEGIN; ++vk) {
		strncpy(vkNames[vk], vkMouseNames[vk - VK_MOUSE_BEGIN], sizeof(vkNames[vk]) - 1);
		vkNames[vk][sizeof(vkNames[vk]) - 1] = '\0';
	}

	// joystick names
	for (u32 vk = VK_JOY1_BEGIN; vk < VK_TOTAL_COUNT; ++vk) {
		const u32 jidx = (vk - VK_JOY1_BEGIN) / INPUT_MAX_CONTROLLER_BUTTONS;
		const u32 jbtn = (vk - VK_JOY1_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS;
		strncpy(vkNames[vk], vkJoyNames[jbtn], sizeof(vkNames[vk]) - 1);
		vkNames[vk][sizeof(vkNames[vk]) - 1] = '\0';
		vkNames[vk][3] = '1' + jidx;
	}
}

/* M0.2 Phase D: inputSaveBinds/inputParseBindString/inputLoadBinds removed.
 * Use actionmapSaveBinds()/actionmapLoadBinds() instead. */

s32 inputInit(void)
{
	// Set SDL hints before initializing the controller subsystem.
	if (useHIDAPI) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 22)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
		// the two hints below enable Rumble and Motion Sensor for PS4/5 pads connected via bluetooth
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 23, 2)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 25, 1)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "1");
#endif
	}
	if (useRawInput) {
		SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
		SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "1");
	}

	if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC)) {
		SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
	}

	// try to load controller db from an external file in the save folder
	if (fsFileSize("$S/" CONTROLLERDB_FNAME)) {
		const char *dbpath = fsFullPath("$S/" CONTROLLERDB_FNAME);
		const s32 dbcount = SDL_GameControllerAddMappingsFromFile(dbpath);
		if (dbcount >= 0) {
			sysLogPrintf(LOG_NOTE, "input: added %d controller mappings from %s", dbcount, dbpath);
		}
	}

	inputInitAllControllers();

	// since the main event loop is elsewhere, we can receive some events we need using a watcher
	SDL_AddEventWatch(inputEventFilter, NULL);

	inputInitKeyNames();

	/* M0.2 Phase D: CK_* default binds removed — actionmapInit() sets defaults. */

	if (mouseLockMode != MLOCK_AUTO) {
		inputLockMouse(mouseLockMode);
	}

	// update the axis maps
	// NOTE: by default sticks get swapped for 1.2: "right stick" here means left stick on your controller
	for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
		inputControllerSetSticksSwapped(i, padsCfg[i].swapSticks);
	}

	/* M0.2 Phase D: inputLoadBinds() removed — actionmapLoadBinds() called from configLoad path. */

	return connectedMask;
}

/* M0.2 Phase D: inputBindPressed() removed — CK_* system deleted. */

static inline s32 inputAxisScale(s32 x, const s32 deadzone, const f32 scale)
{
	if (abs(x) < deadzone) {
		return 0;
	} else {
		// rescale to fit the non-deadzone range
		if (x < 0) {
			x += deadzone;
		} else {
			x -= deadzone;
		}
		x = x * 32768 / (32768 - deadzone);
		// scale with sensitivity
		x *= scale;
		return (x > 32767) ? 32767 : ((x < -32768) ? -32768 : x);
	}
}

/* M0.2 Phase D: CONT_* → InputAction mapping for inputReadController.
 * Builds npad->button bitmask from actionmap queries instead of CK_* binds. */
static const struct { u32 contbit; InputAction action; } s_ContToAction[] = {
	{ CONT_F,      ACTION_CBUTTON_RIGHT  },
	{ CONT_C,      ACTION_CBUTTON_LEFT   },
	{ CONT_D,      ACTION_CBUTTON_DOWN   },
	{ CONT_E,      ACTION_CBUTTON_UP     },
	{ CONT_R,      ACTION_FIRE_SECONDARY },
	{ CONT_L,      ACTION_FIRE_MODE      },
	{ CONT_EXTRA0, ACTION_RELOAD         },
	{ CONT_EXTRA1, ACTION_WEAPON_NEXT    },
	{ CONT_RIGHT,  ACTION_DPAD_RIGHT     },
	{ CONT_LEFT,   ACTION_DPAD_LEFT      },
	{ CONT_DOWN,   ACTION_DPAD_DOWN      },
	{ CONT_UP,     ACTION_DPAD_UP        },
	{ CONT_START,  ACTION_PAUSE          },
	{ CONT_G,      ACTION_FIRE_PRIMARY   },
	{ CONT_B,      ACTION_CANCEL_USE     },
	{ CONT_A,      ACTION_USE            },
	{ CONT_0010,   ACTION_USE            }, /* BUTTON_UI_ACCEPT → unified use/accept */
	{ CONT_0020,   ACTION_CANCEL_USE     }, /* BUTTON_UI_CANCEL → unified cancel */
	{ CONT_2000,   ACTION_CROUCH         },
	{ CONT_4000,   ACTION_CROUCH         },
	{ CONT_8000,   ACTION_CROUCH         },
};
#define CONT_TO_ACTION_COUNT ((s32)(sizeof(s_ContToAction) / sizeof(s_ContToAction[0])))

s32 inputReadController(s32 idx, OSContPad *npad)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS  || !npad) {
		return -1;
	}

	npad->button = 0;

	/* When any ImGui overlay is active, suppress ALL game input. */
	if (pdguiIsActive()) {
		npad->stick_x = 0;
		npad->stick_y = 0;
		npad->rstick_x = 0;
		npad->rstick_y = 0;
		return 0;
	}

	if (textInput) {
		npad->stick_x = 0;
		npad->stick_y = 0;
		npad->rstick_x = 0;
		npad->rstick_y = 0;
		return 0;
	}

	/* M0.2 Phase D: Build button bitmask from actionmap instead of CK_* binds */
	for (s32 i = 0; i < CONT_TO_ACTION_COUNT; i++) {
		if (actionHeld(idx, s_ContToAction[i].action)) {
			npad->button |= s_ContToAction[i].contbit;
		}
	}

	/* Digital keyboard movement → stick values */
	const s32 xdiff = (actionHeld(idx, ACTION_MOVE_RIGHT) - actionHeld(idx, ACTION_MOVE_LEFT));
	const s32 ydiff = (actionHeld(idx, ACTION_MOVE_FORWARD) - actionHeld(idx, ACTION_MOVE_BACKWARD));
	npad->stick_x = xdiff < 0 ? -0x80 : (xdiff > 0 ? 0x7F : 0);
	npad->stick_y = ydiff < 0 ? -0x80 : (ydiff > 0 ? 0x7F : 0);

	const struct controllercfg *cfg = &padsCfg[idx];

	if (cfg->cancelCButtons) {
		// opposite C buttons cancel each other out
		if ((npad->button & (L_CBUTTONS | R_CBUTTONS)) == (L_CBUTTONS | R_CBUTTONS)) {
			npad->button &= ~(L_CBUTTONS | R_CBUTTONS);
		}
		if ((npad->button & (U_CBUTTONS | D_CBUTTONS)) == (U_CBUTTONS | D_CBUTTONS)) {
			npad->button &= ~(U_CBUTTONS | D_CBUTTONS);
		}
	}

	if (!pads[idx]) {
		return 0;
	}

	/* C-3 fix: Restore controller analog stick population so OSContPad reflects
	 * complete input state. actionmapPollFrame() is the primary source for game
	 * code via actionValue(), but joyGetStickX/Y and joyGetRStickX/Y still read
	 * from OSContPad samples — keeping them populated ensures correctness. */
	{
		s32 leftX = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[0][0]);
		s32 leftY = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[0][1]);
		s32 rightX = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[1][0]);
		s32 rightY = SDL_GameControllerGetAxis(pads[idx], cfg->axisMap[1][1]);

		leftX = inputAxisScale(leftX, cfg->deadzone[cfg->axisMap[0][0]], cfg->sens[cfg->axisMap[0][0]]);
		leftY = inputAxisScale(leftY, cfg->deadzone[cfg->axisMap[0][1]], cfg->sens[cfg->axisMap[0][1]]);
		rightX = inputAxisScale(rightX, cfg->deadzone[cfg->axisMap[1][0]], cfg->sens[cfg->axisMap[1][0]]);
		rightY = inputAxisScale(rightY, cfg->deadzone[cfg->axisMap[1][1]], cfg->sens[cfg->axisMap[1][1]]);

		/* Merge: keyboard digital takes priority (already set above), controller fills gaps */
		if (!npad->stick_x && leftX) {
			npad->stick_x = leftX / 0x100;
		}
		s32 stickY = -leftY / 0x100;
		if (!npad->stick_y && stickY) {
			npad->stick_y = (stickY == 128) ? 127 : stickY;
		}

		if (rightX) {
			npad->rstick_x = rightX / 0x100;
		}
		s32 rStickY = -rightY / 0x100;
		if (rStickY) {
			npad->rstick_y = (rStickY == 128) ? 127 : rStickY;
		}
	}

	return 0;
}

static inline void inputUpdateMouse(void)
{
	/* Suppress mouse input when ImGui overlay is active */
	if (pdguiIsActive()) {
		mouseDX = 0;
		mouseDY = 0;
		mouseButtons = 0;
		mouseWheel = 0;
		return;
	}

	s32 mx, my;
	mouseButtons = SDL_GetMouseState(&mx, &my);

	if (mouseWheel > 0) {
		mouseButtons |= WHEEL_UP_MASK;
	} else if (mouseWheel < 0) {
		mouseButtons |= WHEEL_DN_MASK;
	}

	mouseWheel = 0;

	s32 mdx = 0;
	s32 mdy = 0;
	SDL_GetRelativeMouseState(&mdx, &mdy);
	if (mouseLocked) {
		mouseDX = mdx;
		mouseDY = mdy;
	} else {
		mouseDX = mx - mouseX;
		mouseDY = my - mouseY;
	}

	mouseX = mx;
	mouseY = my;

	// if MLOCK_AUTO is enabled, disable cursor if mouse is unlocked
	// and we haven't moved it for a few seconds
	if (mouseLockMode == MLOCK_AUTO && !mouseLocked) {
		if (abs(mouseDX) > CURSOR_HIDE_THRESHOLD || abs(mouseDY) > CURSOR_HIDE_THRESHOLD) {
			if (!mouseShowCursor) {
				inputMouseShowCursor(1);
			}
		} else if (sysGetMicroseconds() > mouseCursorTime) {
			if (mouseShowCursor) {
				inputMouseShowCursor(0);
			}
		}
	}
}

void inputUpdate(void)
{
	SDL_GameControllerUpdate();

	if (mouseEnabled) {
		inputUpdateMouse();
	}
}

s32 inputControllerConnected(s32 idx)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}
	return pads[idx] || (connectedMask & (1 << idx));
}

void *inputGetPad(s32 idx)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return NULL;
	}
	return pads[idx];
}

s32 inputRumbleSupported(s32 idx)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}
	return padsCfg[idx].rumbleOn;
}

void inputRumble(s32 idx, f32 strength, f32 time)
{
	if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || !pads[idx]) {
		return;
	}

	if (padsCfg[idx].rumbleScale <= 0.f) {
		return;
	}

	if (padsCfg[idx].rumbleOn) {
		strength *= padsCfg[idx].rumbleScale;
		if (strength <= 0.f) {
			strength = 0.f;
			time = 0.f;
		} else {
			strength *= 65535.f;
			time *= 1000.f;
		}
		SDL_GameControllerRumble(pads[idx], (u16)strength, (u16)strength, (u32)time);
	}
}

f32 inputRumbleGetStrength(s32 cidx)
{
	/* L-3: Bounds check to prevent OOB access on padsCfg. */
	if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) return 0.0f;
	return padsCfg[cidx].rumbleScale;
}

void inputRumbleSetStrength(s32 cidx, f32 val)
{
	/* L-3: Bounds check to prevent OOB access on padsCfg. */
	if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) return;
	padsCfg[cidx].rumbleScale = val;
}

s32 inputControllerMask(void)
{
	return connectedMask;
}

s32 inputControllerGetSticksSwapped(s32 cidx)
{
	return padsCfg[cidx].swapSticks;
}

void inputControllerSetSticksSwapped(s32 cidx, s32 swapped)
{
	padsCfg[cidx].swapSticks = swapped;
	if (swapped) {
		padsCfg[cidx].axisMap[0][0] = SDL_CONTROLLER_AXIS_RIGHTX;
		padsCfg[cidx].axisMap[0][1] = SDL_CONTROLLER_AXIS_RIGHTY;
		padsCfg[cidx].axisMap[1][0] = SDL_CONTROLLER_AXIS_LEFTX;
		padsCfg[cidx].axisMap[1][1] = SDL_CONTROLLER_AXIS_LEFTY;
	} else {
		padsCfg[cidx].axisMap[0][0] = SDL_CONTROLLER_AXIS_LEFTX;
		padsCfg[cidx].axisMap[0][1] = SDL_CONTROLLER_AXIS_LEFTY;
		padsCfg[cidx].axisMap[1][0] = SDL_CONTROLLER_AXIS_RIGHTX;
		padsCfg[cidx].axisMap[1][1] = SDL_CONTROLLER_AXIS_RIGHTY;
	}
}

s32 inputControllerGetDualAnalog(s32 cidx)
{
	return !padsCfg[cidx].stickCButtons;
}

void inputControllerSetDualAnalog(s32 cidx, s32 enable)
{
	padsCfg[cidx].stickCButtons = !enable;
}

s32 inputControllerGetCancelCButtons(s32 cidx)
{
	return padsCfg[cidx].cancelCButtons;
}

void inputControllerSetCancelCButtons(s32 cidx, s32 cancel)
{
	padsCfg[cidx].cancelCButtons = cancel;
}

s32 inputControllerGetInvertRStickY(s32 cidx)
{
	return padsCfg[cidx].invertRStickY;
}

void inputControllerSetInvertRStickY(s32 cidx, s32 invert)
{
	padsCfg[cidx].invertRStickY = invert;
}

f32 inputControllerGetAxisScale(s32 cidx, s32 stick, s32 axis)
{
	return padsCfg[cidx].sens[stick * 2 + axis];
}

void inputControllerSetAxisScale(s32 cidx, s32 stick, s32 axis, f32 value)
{
	padsCfg[cidx].sens[stick * 2 + axis] = value;
}

f32 inputControllerGetAxisDeadzone(s32 cidx, s32 stick, s32 axis)
{
	return (f32)padsCfg[cidx].deadzone[stick * 2 + axis] / 32767.f;
}

void inputControllerSetAxisDeadzone(s32 cidx, s32 stick, s32 axis, f32 value)
{
	padsCfg[cidx].deadzone[stick * 2 + axis] = value * 32767.f;
}

s32 inputGetConnectedControllers(s32 *out)
{
	s32 count = 0;

	for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
		if (SDL_IsGameController(jidx)) {
			if (out && count < INPUT_MAX_CONNECTED_CONTROLLERS) {
				out[count] = SDL_JoystickGetDeviceInstanceID(jidx);
			}
			++count;
		}
	}

	return count;
}

s32 inputGetAssignedControllerId(s32 cidx)
{
	if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) {
		return -1;
	}

	if (pads[cidx] == NULL) {
		return -1;
	}

	return inputControllerGetId(pads[cidx]);
}

const char *inputGetConnectedControllerName(s32 id)
{
	static char fullName[256];

	if (id < 0) {
		return "Invalid";
	}

	const s32 jidx = inputDeviceIndexFromId(id);
	if (jidx < 0) {
		return "Invalid";
	}

	const char *name = SDL_GameControllerNameForIndex(jidx);
	if (!name || !name[0]) {
		name = "Unnamed Controller";
	}

	snprintf(fullName, sizeof(fullName), "%d: %s", jidx, name);

	// replace non-ascii chars with spaces
	for (char *p = fullName; *p; ++p) {
		if ((u32)*p >= 0x7f) {
			*p = ' ';
		}
	}

	return fullName;
}

s32 inputAssignController(s32 cidx, s32 id)
{
	if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) {
		return 0;
	}

	if (id < 0) {
		// close current controller, if any
		if (pads[cidx]) {
			inputCloseController(cidx);
			return 1;
		}
		return 0;
	}

	const s32 jidx = inputDeviceIndexFromId(id);
	if (jidx < 0 || jidx >= SDL_NumJoysticks() || !SDL_IsGameController(jidx)) {
		return 0;
	}

	// try to unassign any other instances of this controller
	for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
		if (pads[i] && inputControllerGetId(pads[i]) == id) {
			inputCloseController(i);
			pads[i] = NULL;
			padsCfg[i].deviceIndex = -1;
		}
	}

	SDL_GameController *newpad = SDL_GameControllerOpen(jidx);
	if (!newpad) {
		return 0;
	}

	if (pads[cidx]) {
		inputCloseController(cidx);
	}

	pads[cidx] = newpad;
	inputInitController(cidx, id);

	return 1;
}

/* M0.2 Phase D: inputKeyBind/inputKeyGetBinds removed — use actionmapBind(). */

/**
 * DEPRECATED: Use actionHeld() instead for gameplay/menu input.
 *
 * Only legitimate remaining callers:
 *   - optionsmenu.c: rebind key capture (needs raw VK, not action)
 *   - menu.c: legacy menu mouse input (VK_MOUSE_LEFT/WHEEL)
 *   - inputKeyJustPressed() below (edge wrapper)
 * All other callers should use the action map.
 */
s32 inputKeyPressed(u32 vk)
{
	/* When any ImGui overlay is active, suppress all key/button polling
	 * so the game doesn't act on inputs meant for the overlay. */
	if (pdguiIsActive()) {
		return 0;
	}

	if (vk >= VK_KEYBOARD_BEGIN && vk < VK_MOUSE_BEGIN) {
		const u8 *state = SDL_GetKeyboardState(NULL);
		return state[vk - VK_KEYBOARD_BEGIN];
	}

	if (vk >= VK_MOUSE_BEGIN && vk < VK_JOY_BEGIN) {
		return (mouseButtons & SDL_BUTTON(vk - VK_MOUSE_BEGIN + 1)) != 0;
	}

	if (vk >= VK_JOY_BEGIN && vk < VK_TOTAL_COUNT) {
		vk -= VK_JOY_BEGIN;
		const s32 idx = vk / INPUT_MAX_CONTROLLER_BUTTONS;
		if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || !pads[idx]) {
			return 0;
		}
		vk = vk % INPUT_MAX_CONTROLLER_BUTTONS;
		// triggers
		if (vk == 30 || vk == 31) {
			const s32 trig = SDL_CONTROLLER_AXIS_TRIGGERLEFT + vk - 30;
			return SDL_GameControllerGetAxis(pads[idx], trig) > TRIG_THRESHOLD;
		}
		// stick axis directions (synthetic VKs 22-29)
		if (vk >= 22 && vk <= 29) {
			s32 axis = 0;
			s32 wantNeg = 0;
			switch (vk) {
			case 22: axis = SDL_CONTROLLER_AXIS_LEFTX;  wantNeg = 1; break; // LSTICK_LEFT
			case 23: axis = SDL_CONTROLLER_AXIS_LEFTX;  wantNeg = 0; break; // LSTICK_RIGHT
			case 24: axis = SDL_CONTROLLER_AXIS_LEFTY;  wantNeg = 1; break; // LSTICK_UP
			case 25: axis = SDL_CONTROLLER_AXIS_LEFTY;  wantNeg = 0; break; // LSTICK_DOWN
			case 26: axis = SDL_CONTROLLER_AXIS_RIGHTX; wantNeg = 1; break; // RSTICK_LEFT
			case 27: axis = SDL_CONTROLLER_AXIS_RIGHTX; wantNeg = 0; break; // RSTICK_RIGHT
			case 28: axis = SDL_CONTROLLER_AXIS_RIGHTY; wantNeg = 1; break; // RSTICK_UP
			case 29: axis = SDL_CONTROLLER_AXIS_RIGHTY; wantNeg = 0; break; // RSTICK_DOWN
			default: return 0;
			}
			s32 val = SDL_GameControllerGetAxis(pads[idx], axis);
			if (wantNeg) {
				return val < -TRIG_THRESHOLD;
			} else {
				return val > TRIG_THRESHOLD;
			}
		}
		return SDL_GameControllerGetButton(pads[idx], vk);
	}

	return 0;
}

/**
 * DEPRECATED: Use actionPressed() instead for gameplay/menu input.
 *
 * Only legitimate remaining callers:
 *   - menu.c: legacy menu mouse click (VK_MOUSE_LEFT)
 * All other callers should use the action map.
 *
 * L-2: WARNING — has side-effect: updates vkPrevState[vk] on every call.
 * Calling multiple times per frame for the same VK consumes the edge.
 */
s32 inputKeyJustPressed(u32 vk)
{
	const s8 pressed = inputKeyPressed(vk);
	const s32 result = pressed && !vkPrevState[vk];
	vkPrevState[vk] = pressed;
	return result;
}

/* M0.2 Phase D: inputContToContKey/inputButtonPressed removed.
 * Use actionHeld/actionPressed with InputAction enum instead. */

void inputLockMouse(s32 lock)
{
	mouseLocked = !!lock;

	/* When the ImGui debug overlay is active, don't actually grab the mouse.
	 * The overlay needs absolute coordinates and a visible cursor.
	 * The saved mouseLocked state will be restored when the overlay closes. */
	if (pdguiIsActive()) {
		return;
	}

	SDL_SetRelativeMouseMode(mouseLocked);
}

s32 inputMouseIsLocked(void)
{
	return mouseLocked;
}

s32 inputMouseGetPosition(s32 *x, s32 *y)
{
	if (x) *x = mouseX * videoGetNativeWidth() / videoGetWidth();
	if (y) *y = mouseY * videoGetNativeHeight() / videoGetHeight();
	return (mouseDX != 0 || mouseDY != 0);
}

void inputMouseGetRawDelta(s32 *dx, s32 *dy)
{
	if (dx) *dx = mouseDX;
	if (dy) *dy = mouseDY;
}

void inputMouseGetScaledDelta(f32* dx, f32* dy)
{
		f32 mdx = 0.f, mdy = 0.f;

		if (mouseLocked) {
				mdx = mouseSensX * ((f32)mouseDX / 3.5f) * 0.022f;
				mdy = mouseSensY * ((f32)mouseDY / 3.5f) * 0.022f;
		}
		if (dx) *dx = mdx;
		if (dy) *dy = mdy;
}

void inputMouseGetAbsScaledDelta(f32* dx, f32* dy)
{
		f32 mdx = 0.f, mdy = 0.f;

		if (mouseLocked) {
				mdx = fabsf(mouseSensX) * ((f32)mouseDX / 3.5f) * 0.022f;
				mdy = fabsf(mouseSensY) * ((f32)mouseDY / 3.5f) * 0.022f;
		}
		if (dx) *dx = mdx;
		if (dy) *dy = mdy;
}

void inputMouseGetSpeed(f32 *x, f32 *y)
{
	*x = mouseSensX;
	*y = mouseSensY;
}

void inputMouseSetSpeed(f32 x, f32 y)
{
	mouseSensX = x;
	mouseSensY = y;
}

s32 inputMouseIsEnabled(void)
{
	return mouseEnabled;
}

void inputMouseEnable(s32 enabled)
{
	mouseEnabled = !!enabled;
	if (!mouseEnabled && mouseLockMode != MLOCK_ON && mouseLocked) {
		inputLockMouse(0);
	}
}

s32 inputAutoLockMouse(s32 wantlock)
{
	if (mouseEnabled && mouseLockMode == MLOCK_AUTO) {
		inputLockMouse(wantlock);
		return 1;
	}
	return 0;
}

void inputMouseShowCursor(s32 show)
{
	mouseShowCursor = !!show;

	/* Don't hide the cursor while the debug overlay is active */
	if (pdguiIsActive()) {
		return;
	}

	/* B-259 (2026-04-25): route through the input-ctx authority. The
	 * MLOCK_AUTO 3-second auto-hide timer in inputUpdateMouse() above
	 * calls this with show=0 every tick once idle. Without the helper
	 * the timer fired SDL_ShowCursor(SDL_DISABLE) even when the user
	 * was inside a menu (mouseLocked was already false because the
	 * menu had absolute-mouse mode), producing the "mouse usable but
	 * not visible" symptom Mike playtested in build 21010fbd. The
	 * helper observes that ctx-top != gameplay and refuses to hide. */
	inputCtxApplyCursorVisibility(mouseShowCursor);
	if (show) {
		mouseCursorTime = sysGetMicroseconds() + CURSOR_HIDE_TIME;
	}
}

s32 inputGetMouseLockMode(void)
{
	return mouseLockMode;
}

void inputSetMouseLockMode(s32 lockmode)
{
	mouseLockMode = lockmode;
	if (lockmode == MLOCK_ON) {
		inputLockMouse(1);
	} else {
		inputLockMouse(0);
	}
}

/* M0.2 Phase D: inputGetContKeyName/inputGetContKeyByName removed — CK_* system deleted. */

const char *inputGetKeyName(s32 vk)
{
	if (vk < 0 || vk >= VK_TOTAL_COUNT) {
		vk = 0;
	}
	if (!vkNames[vk][0]) {
		snprintf(vkNames[vk], sizeof(vkNames[vk]), "UNKNOWN%d", vk);
	}
	return vkNames[vk];
}

s32 inputGetKeyByName(const char *name)
{
	s32 start = 0;
	s32 end = 0;

	if (!strncmp(name, "JOY", 3) && isdigit(name[3])) {
		const s32 idx = name[3] - '1';
		if (idx >= 0 && idx < INPUT_MAX_CONTROLLERS) {
			start = VK_JOY1_BEGIN + idx * INPUT_MAX_CONTROLLER_BUTTONS;
			end = start + INPUT_MAX_CONTROLLER_BUTTONS;
		}
	} else if (!strncmp(name, "MOUSE", 5)) {
		start = VK_MOUSE_BEGIN;
		end = VK_JOY1_BEGIN;
	} else if (!strncmp(name, "UNKNOWN", 7) && isdigit(name[7])) {
		const s32 key = atoi(name + 7);
		if (key >= 0 && key < VK_TOTAL_COUNT) {
			return key;
		}
	} else {
		end = VK_MOUSE_BEGIN;
	}

	for (s32 i = start; i < end; ++i) {
		if (!strcmp(vkNames[i], name)) {
			return i;
		}
	}

	sysLogPrintf(LOG_WARNING, "unknown key name: `%s`", name);

	return -1;
}

void inputClearLastKey(void)
{
	lastKey = 0;
}

s32 inputGetLastKey(void)
{
	return lastKey;
}

void inputStartTextInput(void)
{
	lastChar = 0;
	lastKey = 0;
	textInput = 1;
	SDL_StartTextInput();
}

void inputClearLastTextChar(void)
{
	lastChar = 0;
}

char inputGetLastTextChar(void)
{
	return lastChar;
}

static inline s32 filterChar(const char ch)
{
	return isalnum(ch) || ch == ' ' || ch == '?' || ch == '!' || ch == '.';
}

s32 inputTextHandler(char *out, const u32 outSize, s32 *curCol, s32 oskCharsOnly)
{
	const s32 ctrlHeld = inputGetKeyModState() & KM_CTRL;

	if (!ctrlHeld) {
		const char chr = inputGetLastTextChar();
		inputClearLastTextChar();
		const s32 valid = chr && (oskCharsOnly ? filterChar(chr) : isprint(chr));
		if (valid) {
			if (*curCol < outSize - 1) {
				out[(*curCol)++] = chr;
				out[*curCol] = '\0';
			}
		}
	}

	const s32 key = inputGetLastKey();
	inputClearLastKey();
	if (ctrlHeld && (key == VK_A + ('v' - 'a'))) {
		// CTRL+V; paste from clipboard
		const char *clip = inputGetClipboard();
		if (clip) {
			const s32 remain = outSize - *curCol - 1;
			inputClearClipboard();
			*curCol += snprintf(out + *curCol, remain, "%s", clip);
			if (*curCol > outSize) {
				*curCol = outSize;
			}
		}
	} else if (key == VK_BACKSPACE) {
		if (*curCol) {
			out[--*curCol] = '\0';
		} else {
			out[0] = '\0';
		}
	} else if (key == VK_RETURN) {
		if (out[0] && *curCol) {
			return 1;
		}
	} else if (key == VK_ESCAPE) {
		return -1;
	}

	return 0;
}

void inputClearClipboard(void)
{
	if (clipboardText) {
		SDL_free(clipboardText);
		clipboardText = NULL;
	}
}

const char *inputGetClipboard(void)
{
	if (!clipboardText) {
		char *text = SDL_GetClipboardText();
		if (text) {
			clipboardText = text;
			// remove non-printable and multibyte chars
			for (; *text; ++text) {
				if ((u8)*text < 0x20 || (u8)*text >= 0x7F) {
					*text = '?';
				}
			}
		}
	}
	return clipboardText;
}

void inputStopTextInput(void)
{
	SDL_StopTextInput();
	textInput = 0;
}

s32 inputIsTextInputActive(void)
{
	return textInput;
}

u32 inputGetKeyModState(void)
{
	return SDL_GetModState();
}

PD_CONSTRUCTOR static void inputConfigInit(void)
{
	configRegisterInt("Input.MouseEnabled", &mouseEnabled, 0, 1);
	configRegisterInt("Input.MouseLockMode", &mouseLockMode, MLOCK_OFF, MLOCK_AUTO);
	configRegisterFloat("Input.MouseSpeedX", &mouseSensX, -30.f, 30.f);
	configRegisterFloat("Input.MouseSpeedY", &mouseSensY, -30.f, 30.f);
	configRegisterInt("Input.FakeGamepads", &fakeControllers, 0, 4);
	configRegisterInt("Input.FirstGamepadNum", &firstController, 0, 3);
	configRegisterInt("Input.UseHIDAPI", &useHIDAPI, 0, 1);
	configRegisterInt("Input.UseRawInput", &useRawInput, 0, 1);

	char secname[] = "Input.Player1.Binds";
	char keyname[256] = { 0 };
	for (s32 c = 0; c < MAXCONTROLLERS; ++c) {
		secname[12] = '1' + c;
		secname[13] = '\0';
		configRegisterFloat(strFmt("%s.RumbleScale", secname), &padsCfg[c].rumbleScale, 0.f, 1.f);
		configRegisterInt(strFmt("%s.LStickDeadzoneX", secname), &padsCfg[c].deadzone[0], 0, 32767);
		configRegisterInt(strFmt("%s.LStickDeadzoneY", secname), &padsCfg[c].deadzone[1], 0, 32767);
		configRegisterInt(strFmt("%s.RStickDeadzoneX", secname), &padsCfg[c].deadzone[2], 0, 32767);
		configRegisterInt(strFmt("%s.RStickDeadzoneY", secname), &padsCfg[c].deadzone[3], 0, 32767);
		configRegisterFloat(strFmt("%s.LStickScaleX", secname), &padsCfg[c].sens[0], -10.f, 10.f);
		configRegisterFloat(strFmt("%s.LStickScaleY", secname), &padsCfg[c].sens[1], -10.f, 10.f);
		configRegisterFloat(strFmt("%s.RStickScaleX", secname), &padsCfg[c].sens[2], -10.f, 10.f);
		configRegisterFloat(strFmt("%s.RStickScaleY", secname), &padsCfg[c].sens[3], -10.f, 10.f);
		configRegisterInt(strFmt("%s.StickCButtons", secname), &padsCfg[c].stickCButtons, 0, 1);
		configRegisterInt(strFmt("%s.CancelCButtons", secname), &padsCfg[c].cancelCButtons, 0, 1);
		configRegisterInt(strFmt("%s.SwapSticks", secname), &padsCfg[c].swapSticks, 0, 1);
		configRegisterInt(strFmt("%s.InvertRStickY", secname), &padsCfg[c].invertRStickY, 0, 1);
		configRegisterInt(strFmt("%s.ControllerIndex", secname), &padsCfg[c].deviceIndex, -1, 0x7FFFFFFF);
		/* M0.2 Phase D: CK_* bind config registration removed.
		 * Binds now managed by actionmap pd.ini keys (ActionMap.P%d.*). */
	}
}
