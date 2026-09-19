#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif  // DOOMGENERIC_RESX

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif  // DOOMGENERIC_RESY


#ifdef CMAP256

typedef uint8_t pixel_t;

#else  // CMAP256

typedef uint32_t pixel_t;

#endif  // CMAP256


extern pixel_t* DG_ScreenBuffer;

#ifdef __cplusplus
extern "C" {
#endif

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

// Analog stick deflection, each -128..127 with the platform's deadzone already
// applied and the centre snapped to 0. Left stick moves and strafes, right
// stick turns; G_BuildTiccmd scales these into forwardmove/sidemove/angleturn.
// The engine's own joystick path is digital -- it only tests joyxmove > 0 --
// so analog movement cannot go through it.
void DG_GetAnalog(int *lx, int *ly, int *rx, int *ry);

// What the menus are doing, so the pad layer can give one button the right
// meaning for the context. Implemented engine-side: the platform layer cannot
// see menuactive, currentMenu or the message state, and `boolean` is either an
// enum or an unsigned char depending on whether <stdbool.h> reached doomtype.h.
#define DG_MENU_NONE   0   // in the game
#define DG_MENU_SUB    1   // a menu with a parent to go back to
#define DG_MENU_ROOT   2   // the top menu; back has nowhere to go
#define DG_MENU_PROMPT 3   // a yes/no box, which only takes confirm or abort
int DG_MenuState(void);

// The engine's configured yes/no keys, 'y' and 'n' by default. Nothing on a
// pad produces those, so the mapping has to fetch them.
int DG_MenuConfirmKey(void);
int DG_MenuAbortKey(void);

#ifdef __cplusplus
}
#endif

#endif //DOOM_GENERIC
