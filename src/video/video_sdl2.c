/** @file src/video/video_sdl2.c SDL 2 video driver. */

#include <SDL.h>
#include "types.h"
#include "../os/error.h"

#include "video.h"

#include "../file.h"
#include "../gfx.h"
#include "../input/input.h"
#include "../input/mouse.h"
#include "../opendune.h"

enum {
	SDL_WINDOW_WIDTH = 640,
	SDL_WINDOW_HEIGHT = 400
};

static bool s_video_initialized = false;
static bool s_video_lock = false;

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static uint32 *s_gfx_screen;

static uint32 s_palette[256];

static uint8 s_keyBufferLatest = 0;

static uint16 s_mousePosX = 0;
static uint16 s_mousePosY = 0;
static bool s_mouseButtonLeft  = false;
static bool s_mouseButtonRight = false;

static uint16 s_mouseMinX = 0;
static uint16 s_mouseMaxX = 0;
static uint16 s_mouseMinY = 0;
static uint16 s_mouseMaxY = 0;

/* Partly copied from http://webster.cs.ucr.edu/AoA/DOS/pdf/apndxc.pdf */
static uint8 s_SDL_keymap[] = {
           0,    0,    0,    0,    0,    0,    0,    0, 0x0E, 0x0F,    0,    0,    0, 0x1C,    0,    0, /*  0x00 -  0x0F */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x01,    0,    0,    0,    0, /*  0x10 -  0x1F */
        0x39,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x33, 0x0C, 0x34, 0x35, /*  0x20 -  0x2F */
        0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,    0,    0,    0, 0x0D,    0,    0, /*  0x30 -  0x3F */
           0, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, /*  0x40 -  0x4F */
        0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,    0, 0x2B,    0,    0,    0, /*  0x50 -  0x5F */
        0x29, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, /*  0x60 -  0x6F */
        0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,    0,    0,    0,    0, 0x53, /*  0x70 -  0x7F */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0x80 -  0x8F */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0x90 -  0x9F */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xA0 -  0xAF */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xB0 -  0xBF */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xC0 -  0xCF */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xD0 -  0xDF */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xE0 -  0xEF */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /*  0xF0 -  0xFF */
           0, 0x4F, 0x50, 0x51, 0x4B, 0x1C, 0x4D, 0x47, 0x48, 0x49,    0,    0,    0,    0,    0,    0, /* 0x100 - 0x10F */
           0, 0x48, 0x50, 0x4D, 0x4B, 0x52, 0x47, 0x4F, 0x49, 0x51, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, /* 0x110 - 0x11F */
        0x41, 0x42, 0x43, 0x44, 0x57, 0x58,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x36, /* 0x120 - 0x12F */
        0x36,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, /* 0x130 - 0x13F */
};

/**
 * Callback wrapper for mouse actions.
 */
static void Video_Mouse_Callback(void)
{
	Mouse_EventHandler(s_mousePosX, s_mousePosY, s_mouseButtonLeft, s_mouseButtonRight);
}

/**
 * Callback wrapper for key actions.
 */
static void Video_Key_Callback(uint8 key)
{
	s_keyBufferLatest = key;
	Input_EventHandler(key);
}

/**
 * Handle the moving of the mouse.
 * @param x The new X-position of the mouse.
 * @param y The new Y-position of the mouse.
 */
static void Video_Mouse_Move(uint16 x, uint16 y)
{
	uint16 rx, ry;

	rx = x;
	ry = y;

	if (s_mouseMinX != 0 && rx < s_mouseMinX) rx = s_mouseMinX;
	if (s_mouseMaxX != 0 && rx > s_mouseMaxX) rx = s_mouseMaxX;
	if (s_mouseMinY != 0 && ry < s_mouseMinY) ry = s_mouseMinY;
	if (s_mouseMaxY != 0 && ry > s_mouseMaxY) ry = s_mouseMaxY;

	/* If we moved, send the signal back to the window to correct for it */
	if (x != rx || y != ry) {
		SDL_WarpMouseInWindow(s_window, rx, ry);
		return;
	}

	s_mousePosX = rx;
	s_mousePosY = ry;

	Video_Mouse_Callback();
}

/**
 * Handle the clicking of a mouse button.
 * @param left True if the left button, otherwise the right button.
 * @param down True if the button is down, otherwise it is up.
 */
static void Video_Mouse_Button(bool left, bool down)
{
	if (left) {
		s_mouseButtonLeft = down;
	} else {
		s_mouseButtonRight = down;
	}

	Video_Mouse_Callback();
}

/**
 * Set the current position of the mouse.
 * @param x The new X-position of the mouse.
 * @param y The new Y-position of the mouse.
 */
void Video_Mouse_SetPosition(uint16 x, uint16 y)
{
	SDL_WarpMouseInWindow(s_window, x, y);
}

/**
 * Set the region in which the mouse is allowed to move, or 0 for no limitation.
 * @param minX The minimal X-position.
 * @param maxX The maximal X-position.
 * @param minY The minimal Y-position.
 * @param maxY The maximal Y-position.
 */
void Video_Mouse_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY)
{
	s_mouseMinX = minX;
	s_mouseMaxX = maxX;
	s_mouseMinY = minY;
	s_mouseMaxY = maxY;
}

/**
 * Initialize the video driver.
 */
bool Video_Init(void)
{
	int err;

	if (s_video_initialized) return true;

	err = SDL_Init(SDL_INIT_VIDEO);

	if (err != 0) {
		Error("Could not initialize SDL: %s\n", SDL_GetError());
		return false;
	}

	err = SDL_CreateWindowAndRenderer(
			SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, 0,
			&s_window,
			&s_renderer);

	if (err != 0) {
		Error("Could not set resolution: %s\n", SDL_GetError());
		return false;
	}

	SDL_SetWindowTitle(s_window, window_caption);

	err = SDL_RenderSetLogicalSize(s_renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

	if (err != 0) {
		Error("Could not set logical size: %s\n", SDL_GetError());
		return false;
	}

	s_texture = SDL_CreateTexture(s_renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			SCREEN_WIDTH, SCREEN_HEIGHT);

	if (!s_texture) {
		Error("Could not create texture: %s\n", SDL_GetError());
		return false;
	}

	s_gfx_screen = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32));
	if (!s_gfx_screen) {
		Error("Could not create screen buffer\n");
		return false;
	}

	SDL_ShowCursor(SDL_DISABLE);

	s_video_initialized = true;
	return true;
}

/**
 * Uninitialize the video driver.
 */
void Video_Uninit(void)
{
	s_video_initialized = false;

	if (s_gfx_screen) {
		free(s_gfx_screen);
		s_gfx_screen = NULL;
	}

	if (s_texture) {
		SDL_DestroyTexture(s_texture);
		s_texture = NULL;
	}

	if (s_renderer) {
		SDL_DestroyRenderer(s_renderer);
		s_renderer = NULL;
	}

	if (s_window) {
		SDL_DestroyWindow(s_window);
		s_window = NULL;
	}

	SDL_Quit();
}

/**
 * This function copies the 320x200 buffer to the real screen.
 * Scaling is done automatically.
 */
static void Video_DrawScreen(void)
{
	const uint8 *gfx_screen8 = GFX_Screen_Get_ByIndex(SCREEN_0);
	int i;

	for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
		s_gfx_screen[i] = s_palette[gfx_screen8[i]];
	}

	SDL_UpdateTexture(s_texture, NULL, s_gfx_screen, SCREEN_WIDTH * sizeof(uint32));
	SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
}

/**
 * Runs every tick to handle video driver updates.
 */
void Video_Tick(void)
{
	SDL_Event event;

	if (!s_video_initialized) return;
	if (g_fileOperation != 0) return;
	if (s_video_lock) return;

	s_video_lock = true;

	while (SDL_PollEvent(&event)) {
		uint8 keyup = 1;

		switch (event.type) {
			case SDL_QUIT: {
				s_video_lock = false;
				PrepareEnd();
				exit(0);
			} break;

			case SDL_MOUSEMOTION:
				Video_Mouse_Move(event.motion.x, event.motion.y);
				break;

			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button == SDL_BUTTON_LEFT)  Video_Mouse_Button(true,  true);
				if (event.button.button == SDL_BUTTON_RIGHT) Video_Mouse_Button(false, true);
				break;
			case SDL_MOUSEBUTTONUP:
				if (event.button.button == SDL_BUTTON_LEFT)  Video_Mouse_Button(true,  false);
				if (event.button.button == SDL_BUTTON_RIGHT) Video_Mouse_Button(false, false);
				break;

			case SDL_KEYDOWN:
				keyup = 0;
				/* Fall Through */
			case SDL_KEYUP:
			{
				unsigned int sym = event.key.keysym.sym;
				if (sym >= sizeof(s_SDL_keymap)) continue;
				if (s_SDL_keymap[sym] == 0) {
					Warning("Unhandled key %X\n", sym);
					continue;
				}
				Video_Key_Callback(s_SDL_keymap[sym] | (keyup ? 0x80 : 0x0));
			} break;
		}
	}

	Video_DrawScreen();
	SDL_RenderPresent(s_renderer);

	s_video_lock = false;
}

/**
 * Change the palette with the palette supplied.
 * @param palette The palette to replace the current with.
 * @param from From which colour.
 * @param length The length of the palette (in colours).
 */
void Video_SetPalette(void *palette, int from, int length)
{
	uint8 *p = palette;
	int i;

	s_video_lock = true;

	for (i = from; i < from + length; i++) {
		s_palette[i] = 0xff000000 /* a */
		             | ((((p[0] & 0x3F) * 0x41) << 12) & 0x00ff0000) /* r */
		             | ((((p[1] & 0x3F) * 0x41) << 4)  & 0x0000ff00) /* g */
		             |  (((p[2] & 0x3F) * 0x41) >> 4); /* b */
		p += 3;
	}

	s_video_lock = false;
}
