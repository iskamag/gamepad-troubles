#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define WIN_W 900
#define WIN_H 520

typedef struct { Uint8 r, g, b, a; } Color;

static const Color C_BG      = { 37, 37, 48, 255};
static const Color C_PANEL   = { 42, 42, 52, 255};
static const Color C_LINE    = { 56, 56, 74, 255};
static const Color C_TEXT    = {200,200,208,255};
static const Color C_DIM     = {120,120,136,255};
static const Color C_ACCENT  = { 74, 90,122,255};
static const Color C_ACTIVE  = {130,160,210,255};
static const Color C_TRIGGER = { 90,160,130,255};
static const Color C_WARN    = {200, 90, 90,255};
static const Color C_OK      = { 90,200,120,255};

typedef struct { SDL_Rect r; const char *label; int pressed; } Btn;

enum { B_A, B_B, B_X, B_Y, B_LB, B_RB, B_LT, B_RT,
       B_BACK, B_START, B_GUIDE, B_L3, B_R3,
       B_UP, B_DOWN, B_LEFT, B_RIGHT, B_COUNT };

static Btn buttons[B_COUNT];

typedef struct { int x, y; int radius; float ax, ay; } Stick;
static Stick left_stick, right_stick;

static int has_controller = 0;
static int no_joy_count = 0;
static char status_text[256] = "";
static char name_text[128] = "";

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, SDL_Rect rc, Color c) {
    set_color(r, c); SDL_RenderFillRect(r, &rc);
}
static void draw_rect(SDL_Renderer *r, SDL_Rect rc, Color c) {
    set_color(r, c); SDL_RenderDrawRect(r, &rc);
}
static void fill_circle(SDL_Renderer *r, int cx, int cy, int rad, Color c) {
    set_color(r, c);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)(sqrtf((float)rad*rad - (float)dy*dy) + 0.5f);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void stroke_circle(SDL_Renderer *r, int cx, int cy, int rad, Color c) {
    set_color(r, c);
    for (int a = 0; a < 360; a += 3) {
        int px = cx + (int)(cosf(a * M_PI / 180.0f) * rad);
        int py = cy + (int)(sinf(a * M_PI / 180.0f) * rad);
        SDL_RenderDrawPoint(r, px, py);
    }
}

static void text_at(SDL_Renderer *r, int x, int y, const char *s, Color c, TTF_Font *f) {
    if (!f || !s[0]) return;
    SDL_Color sc = {c.r, c.g, c.b, 255};
    SDL_Surface *sf = TTF_RenderUTF8_Blended(f, s, sc);
    if (!sf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, sf);
    if (t) { SDL_Rect d = {x, y, sf->w, sf->h}; SDL_RenderCopy(r, t, NULL, &d); SDL_DestroyTexture(t); }
    SDL_FreeSurface(sf);
}
static void text_centered(SDL_Renderer *r, SDL_Rect box, const char *s, Color c, TTF_Font *f) {
    if (!f || !s[0]) return;
    SDL_Color sc = {c.r, c.g, c.b, 255};
    SDL_Surface *sf = TTF_RenderUTF8_Blended(f, s, sc);
    if (!sf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, sf);
    if (t) {
        SDL_Rect d = {box.x + (box.w - sf->w)/2, box.y + (box.h - sf->h)/2, sf->w, sf->h};
        SDL_RenderCopy(r, t, NULL, &d); SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(sf);
}

static void init_buttons(void) {
    const char *labels[B_COUNT] = {
        "A","B","X","Y","LB","RB","LT","RT","Back","Start","Guide","L3","R3",
        "Up","Down","Left","Right"
    };
    for (int i = 0; i < B_COUNT; i++) { buttons[i].label = labels[i]; buttons[i].pressed = 0; }
}

static void layout(void) {
    int mx = WIN_W / 2;
    /* Sticks sit lower; dpad/face buttons are upper, like a real gamepad. */
    left_stick.x = mx - 220; left_stick.y = 290; left_stick.radius = 55;
    right_stick.x = mx + 220; right_stick.y = 290; right_stick.radius = 55;

    int bs = 34;
    /* ABXY face buttons - diamond above the right stick */
    buttons[B_A].r = (SDL_Rect){mx + 220, 198, bs, bs};
    buttons[B_B].r = (SDL_Rect){mx + 258, 160, bs, bs};
    buttons[B_X].r = (SDL_Rect){mx + 182, 160, bs, bs};
    buttons[B_Y].r = (SDL_Rect){mx + 220, 122, bs, bs};
    /* Shoulders / triggers */
    buttons[B_LB].r = (SDL_Rect){mx - 220,  85, 80, 22};
    buttons[B_RB].r = (SDL_Rect){mx + 220,  85, 80, 22};
    buttons[B_LT].r = (SDL_Rect){mx - 220,  55, 80, 22};
    buttons[B_RT].r = (SDL_Rect){mx + 220,  55, 80, 22};
    /* Center buttons - vertically centered on the guide button */
    buttons[B_BACK].r  = (SDL_Rect){mx - 90, 262, 50, 20};
    buttons[B_GUIDE].r = (SDL_Rect){mx - 22, 250, 44, 44};
    buttons[B_START].r = (SDL_Rect){mx + 40, 262, 50, 20};
    /* D-pad - diamond above the left stick */
    buttons[B_UP].r    = (SDL_Rect){mx - 246, 137, 26, 26};
    buttons[B_DOWN].r  = (SDL_Rect){mx - 246, 193, 26, 26};
    buttons[B_LEFT].r  = (SDL_Rect){mx - 274, 165, 26, 26};
    buttons[B_RIGHT].r = (SDL_Rect){mx - 218, 165, 26, 26};
    /* L3/R3 labels below the sticks */
    buttons[B_L3].r = (SDL_Rect){left_stick.x - 50, 380, 100, 18};
    buttons[B_R3].r = (SDL_Rect){right_stick.x - 50, 380, 100, 18};
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError()); SDL_Quit(); return 1;
    }

    SDL_Window *win = SDL_CreateWindow("SDL2 Gamepad GUI Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, 0);

    TTF_Font *font = NULL, *small = NULL;
    const char *fonts[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/gnu-free/FreeSans.ttf",
    };
    for (int i = 0; i < 5 && !font; i++) if (fonts[i]) font = TTF_OpenFont(fonts[i], 14);
    for (int i = 0; i < 5 && !small; i++) if (fonts[i]) small = TTF_OpenFont(fonts[i], 11);
    if (!font) font = small;
    if (!font) fprintf(stderr, "WARN: no font found, text will be invisible\n");

    init_buttons();
    layout();

    SDL_GameController *ctrl = NULL;
    SDL_Joystick *active_joy = NULL;

    int n = SDL_NumJoysticks();
    no_joy_count = n;
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            ctrl = SDL_GameControllerOpen(i);
            if (ctrl) {
                active_joy = SDL_GameControllerGetJoystick(ctrl);
                const char *nm = SDL_GameControllerName(ctrl);
                snprintf(name_text, sizeof(name_text), "%s", nm ? nm : "?");
                has_controller = 1;
                break;
            }
        }
    }
    if (!has_controller)
        snprintf(status_text, sizeof(status_text),
                 "No SDL gamepad detected (%d joystick%s found)", n, n == 1 ? "" : "s");
    else
        snprintf(status_text, sizeof(status_text), "Connected");

    int running = 1;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !ctrl) {
                ctrl = SDL_GameControllerOpen(e.cdevice.which);
                if (ctrl) {
                    active_joy = SDL_GameControllerGetJoystick(ctrl);
                    const char *nm = SDL_GameControllerName(ctrl);
                    snprintf(name_text, sizeof(name_text), "%s", nm ? nm : "?");
                    has_controller = 1;
                }
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED && ctrl &&
                e.cdevice.which == SDL_JoystickInstanceID(active_joy)) {
                SDL_GameControllerClose(ctrl);
                ctrl = NULL; active_joy = NULL; has_controller = 0;
                snprintf(status_text, sizeof(status_text), "Controller disconnected");
            }
        }

        for (int i = 0; i < B_COUNT; i++) buttons[i].pressed = 0;
        left_stick.ax = 0; left_stick.ay = 0;
        right_stick.ax = 0; right_stick.ay = 0;

        if (ctrl) {
            SDL_GameControllerButton map[] = {
                [B_A]=SDL_CONTROLLER_BUTTON_A, [B_B]=SDL_CONTROLLER_BUTTON_B,
                [B_X]=SDL_CONTROLLER_BUTTON_X, [B_Y]=SDL_CONTROLLER_BUTTON_Y,
                [B_LB]=SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
                [B_RB]=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                [B_BACK]=SDL_CONTROLLER_BUTTON_BACK, [B_START]=SDL_CONTROLLER_BUTTON_START,
                [B_GUIDE]=SDL_CONTROLLER_BUTTON_GUIDE, [B_L3]=SDL_CONTROLLER_BUTTON_LEFTSTICK,
                [B_R3]=SDL_CONTROLLER_BUTTON_RIGHTSTICK, [B_UP]=SDL_CONTROLLER_BUTTON_DPAD_UP,
                [B_DOWN]=SDL_CONTROLLER_BUTTON_DPAD_DOWN, [B_LEFT]=SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                [B_RIGHT]=SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            };
            for (int i = 0; i < B_COUNT; i++) {
                if (i == B_LT || i == B_RT) continue;
                if (map[i]) buttons[i].pressed = SDL_GameControllerGetButton(ctrl, map[i]);
            }
            buttons[B_LT].pressed = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
            buttons[B_RT].pressed = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
            left_stick.ax  = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX)  / 32768.0f;
            left_stick.ay  = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY)  / 32768.0f;
            right_stick.ax = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTX) / 32768.0f;
            right_stick.ay = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTY) / 32768.0f;
        }

        /* ---- render ---- */
        set_color(r, C_BG); SDL_RenderClear(r);

        fill_rect(r, (SDL_Rect){80, 50, WIN_W-160, 380}, C_PANEL);
        draw_rect(r, (SDL_Rect){80, 50, WIN_W-160, 380}, C_LINE);

        text_at(r, 100, 18, "SDL2 Gamepad GUI", C_TEXT, font);
        if (has_controller) {
            text_at(r, 100, 450, status_text, C_OK, font);
            text_at(r, WIN_W - 320, 18, name_text, C_DIM, font);
        } else {
            text_at(r, 100, 450, status_text, C_WARN, font);
            if (no_joy_count == 0)
                text_at(r, 100, 472,
                    "SDL sees 0 joysticks. Run ./diagnose.sh to find the break.",
                    C_DIM, small ? small : font);
        }

        /* sticks */
        for (int s = 0; s < 2; s++) {
            Stick *st = s == 0 ? &left_stick : &right_stick;
            fill_circle(r, st->x, st->y, st->radius, C_LINE);
            float mag = sqrtf(st->ax*st->ax + st->ay*st->ay);
            int dotx = st->x + (int)(st->ax * (st->radius - 12));
            int doty = st->y + (int)(st->ay * (st->radius - 12));
            fill_circle(r, dotx, doty, 14, mag > 0.15f ? C_ACTIVE : C_ACCENT);
            stroke_circle(r, dotx, doty, 14, C_LINE);
            text_centered(r, (SDL_Rect){st->x - 30, st->y + st->radius + 6, 60, 16},
                          s == 0 ? "L STICK" : "R STICK", C_DIM, small ? small : font);
        }

        /* triggers LT/RT */
        for (int i = B_LT; i <= B_RT; i++) {
            fill_rect(r, buttons[i].r, buttons[i].pressed ? C_TRIGGER : C_ACCENT);
            draw_rect(r, buttons[i].r, C_LINE);
            text_centered(r, buttons[i].r, buttons[i].label, C_TEXT, small ? small : font);
        }
        /* shoulders LB/RB */
        for (int i = B_LB; i <= B_RB; i++) {
            fill_rect(r, buttons[i].r, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
            draw_rect(r, buttons[i].r, C_LINE);
            text_centered(r, buttons[i].r, buttons[i].label, C_TEXT, small ? small : font);
        }
        /* ABXY face buttons (circles) */
        for (int i = B_A; i <= B_Y; i++) {
            SDL_Rect rc = buttons[i].r;
            int cx = rc.x + rc.w/2, cy = rc.y + rc.h/2, rad = rc.w/2;
            fill_circle(r, cx, cy, rad, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
            stroke_circle(r, cx, cy, rad, C_LINE);
            text_centered(r, rc, buttons[i].label, C_TEXT, font);
        }
        /* dpad */
        for (int i = B_UP; i <= B_RIGHT; i++) {
            fill_rect(r, buttons[i].r, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
            draw_rect(r, buttons[i].r, C_LINE);
            const char *sym = i == B_UP ? "^" : i == B_DOWN ? "v" : i == B_LEFT ? "<" : ">";
            text_centered(r, buttons[i].r, sym, C_TEXT, font);
        }
        /* center: Back, Guide, Start */
        for (int i = B_BACK; i <= B_START; i++) {
            SDL_Rect rc = buttons[i].r;
            if (i == B_GUIDE) {
                int cx = rc.x + rc.w/2, cy = rc.y + rc.h/2, rad = rc.w/2;
                fill_circle(r, cx, cy, rad, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
                stroke_circle(r, cx, cy, rad, C_LINE);
            } else {
                fill_rect(r, rc, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
                draw_rect(r, rc, C_LINE);
            }
            text_centered(r, rc, buttons[i].label, C_TEXT, small ? small : font);
        }
        /* L3/R3 labels */
        for (int i = B_L3; i <= B_R3; i++) {
            fill_rect(r, buttons[i].r, buttons[i].pressed ? C_ACTIVE : C_ACCENT);
            draw_rect(r, buttons[i].r, C_LINE);
            text_centered(r, buttons[i].r, buttons[i].label, C_TEXT, small ? small : font);
        }

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    if (ctrl) SDL_GameControllerClose(ctrl);
    if (font) TTF_CloseFont(font);
    if (small) TTF_CloseFont(small);
    TTF_Quit();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
