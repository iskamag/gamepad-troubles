#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    printf("SDL Gamepad Test (SDL %d.%d.%d)\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    printf("Drivers compiled:");
#ifdef SDL_VIDEO_DRIVER_X11
    printf(" x11");
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
    printf(" wayland");
#endif
    printf("\n");

    int num_maps = SDL_GameControllerAddMappingsFromRW(
        SDL_RWFromFile("/usr/share/gamecontrollerdb/gamecontrollerdb.txt", "rb"), 1);
    if (num_maps < 0) {
        char *home = getenv("HOME");
        if (home) {
            char path[512];
            snprintf(path, sizeof(path),
                     "%s/.config/SDL_GameControllerDB/gamecontrollerdb.txt", home);
            num_maps = SDL_GameControllerAddMappingsFromRW(
                SDL_RWFromFile(path, "rb"), 1);
        }
    }
    printf("Controller DB mappings loaded: %d\n", num_maps > 0 ? num_maps : 0);

    int joysticks = SDL_NumJoysticks();
    printf("Joysticks found: %d\n", joysticks);

    for (int i = 0; i < joysticks; i++) {
        printf("\n--- Joystick %d ---\n", i);
        printf("  Name:       %s\n", SDL_JoystickNameForIndex(i));
        printf("  GUID:       ");
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
        char guid_str[33];
        SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
        printf("%s\n", guid_str);

        SDL_Joystick *joy = SDL_JoystickOpen(i);
        if (joy) {
            printf("  Axes:       %d\n", SDL_JoystickNumAxes(joy));
            printf("  Buttons:    %d\n", SDL_JoystickNumButtons(joy));
            printf("  Hats:       %d\n", SDL_JoystickNumHats(joy));
            printf("  Balls:      %d\n", SDL_JoystickNumBalls(joy));
            printf("  Vendor:     0x%04x\n", SDL_JoystickGetDeviceVendor(i));
            printf("  Product:    0x%04x\n", SDL_JoystickGetDeviceProduct(i));
            printf("  Type:       %d\n", SDL_JoystickGetDeviceType(i));
            SDL_JoystickClose(joy);
        }

        if (SDL_IsGameController(i)) {
            SDL_GameController *ctrl = SDL_GameControllerOpen(i);
            if (ctrl) {
                printf("  Gamepad:    YES (%s)\n",
                       SDL_GameControllerName(ctrl));
                printf("  Mapping:    %s\n",
                       SDL_GameControllerMapping(ctrl));
                SDL_GameControllerClose(ctrl);
            }
        } else {
            printf("  Gamepad:    NO (not in controller DB)\n");
        }
    }

    printf("\n");
    printf("Press buttons/axes on your gamepad to test.\n");
    printf("Press Enter, Escape, or close the window to finish.\n\n");

    SDL_Window *win = SDL_CreateWindow(
        "SDL2 Gamepad Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, 0);
    if (!win) {
        fprintf(stderr, "WARN: could not create window: %s\n", SDL_GetError());
        fprintf(stderr, "Continuing without window...\n");
    } else {
        SDL_RaiseWindow(win);
    }

    SDL_GameController *active_ctrl = NULL;
    for (int i = 0; i < joysticks; i++) {
        if (SDL_IsGameController(i)) {
            active_ctrl = SDL_GameControllerOpen(i);
            if (active_ctrl) {
                printf("Listening on: %s\n",
                       SDL_GameControllerName(active_ctrl));
                break;
            }
        }
    }

    int running = 1;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!active_ctrl) {
                    active_ctrl = SDL_GameControllerOpen(e.cdevice.which);
                    if (active_ctrl) {
                        printf("\n[ATTACHED] %s\n",
                               SDL_GameControllerName(active_ctrl));
                    }
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (active_ctrl &&
                    e.cdevice.which ==
                        SDL_JoystickInstanceID(
                            SDL_GameControllerGetJoystick(active_ctrl))) {
                    printf("\n[DETACHED] controller removed\n");
                    SDL_GameControllerClose(active_ctrl);
                    active_ctrl = NULL;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                printf("[BTN DOWN] %s\n",
                       SDL_GameControllerGetStringForButton(e.cbutton.button));
                break;
            case SDL_CONTROLLERBUTTONUP:
                printf("[BTN UP]   %s\n",
                       SDL_GameControllerGetStringForButton(e.cbutton.button));
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (abs(e.caxis.value) > 8000) {
                    printf("[AXIS]     %s = %d\n",
                           SDL_GameControllerGetStringForAxis(e.caxis.axis),
                           e.caxis.value);
                }
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE ||
                    e.key.keysym.sym == SDLK_RETURN ||
                    e.key.keysym.sym == SDLK_KP_ENTER)
                    running = 0;
                break;
            }
        }
        SDL_Delay(16);
    }

    if (active_ctrl)
        SDL_GameControllerClose(active_ctrl);
    if (win)
        SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
