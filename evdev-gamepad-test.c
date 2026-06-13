#define _GNU_SOURCE
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define BITS_PER_LONG (8 * sizeof(long))
#define NBITS(x) ((((x)-1) / BITS_PER_LONG) + 1)
#define OFF(x)  ((x) % BITS_PER_LONG)
#define LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

static void print_bits(int fd) {
    unsigned long keybit[NBITS(KEY_MAX)] = {0};
    unsigned long absbit[NBITS(ABS_MAX)] = {0};

    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit);

    printf("  ABS axes present:\n");
    for (int i = 0; i < ABS_MAX; i++) {
        if (test_bit(i, absbit)) {
            struct input_absinfo abs;
            if (ioctl(fd, EVIOCGABS(i), &abs) == 0) {
                printf("    %-14s value=%6d min=%6d max=%6d fuzz=%4d flat=%4d\n",
                       i == ABS_X ? "ABS_X" :
                       i == ABS_Y ? "ABS_Y" :
                       i == ABS_Z ? "ABS_Z" :
                       i == ABS_RX ? "ABS_RX" :
                       i == ABS_RY ? "ABS_RY" :
                       i == ABS_RZ ? "ABS_RZ" :
                       i == ABS_HAT0X ? "HAT0X" :
                       i == ABS_HAT0Y ? "HAT0Y" :
                       i == ABS_BRAKE ? "BRAKE" :
                       i == ABS_GAS ? "GAS" : "OTHER",
                       abs.value, abs.minimum, abs.maximum, abs.fuzz, abs.flat);
            }
        }
    }

    printf("  KEY/BTN present:");
    int count = 0;
    for (int i = BTN_MISC; i < KEY_MAX; i++) {
        if (test_bit(i, keybit)) {
            if (count % 6 == 0) printf("\n    ");
            printf("%-14s", i == BTN_SOUTH ? "BTN_SOUTH(A)" :
                              i == BTN_EAST  ? "BTN_EAST(B)"  :
                              i == BTN_NORTH ? "BTN_NORTH(Y)" :
                              i == BTN_WEST  ? "BTN_WEST(X)"  :
                              i == BTN_TL    ? "BTN_TL(LB)"   :
                              i == BTN_TR    ? "BTN_TR(RB)"   :
                              i == BTN_SELECT? "BTN_SELECT"   :
                              i == BTN_START ? "BTN_START"    :
                              i == BTN_MODE  ? "BTN_MODE"     :
                              i == BTN_THUMBL? "BTN_THUMBL"   :
                              i == BTN_THUMBR? "BTN_THUMBR"   :
                              i == BTN_TL2   ? "BTN_TL2(LT)"  :
                              i == BTN_TR2   ? "BTN_TR2(RT)"  :
                              i == BTN_DPAD_UP   ? "DPAD_UP"   :
                              i == BTN_DPAD_DOWN ? "DPAD_DOWN" :
                              i == BTN_DPAD_LEFT ? "DPAD_LEFT" :
                              i == BTN_DPAD_RIGHT? "DPAD_RIGHT": "BTN_?");
            count++;
        }
    }
    printf("\n");
}

static int is_gamepad_like(int fd) {
    unsigned long absbit[NBITS(ABS_MAX)] = {0};
    unsigned long keybit[NBITS(KEY_MAX)] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) < 0) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) return 0;
    int has_abs = test_bit(ABS_X, absbit);
    int has_btn = test_bit(BTN_SOUTH, keybit) || test_bit(BTN_GAMEPAD, keybit)
               || test_bit(BTN_A, keybit) || test_bit(BTN_THUMBL, keybit);
    return has_abs && has_btn;
}

static int scan_devices(int *out_idx, int only_gamepad) {
    DIR *d = opendir("/dev/input");
    if (!d) { perror("opendir /dev/input"); return 0; }
    struct dirent *de;
    int count = 0;
    printf("Scanning /dev/input/event* ...\n\n");
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("[%s] cannot open (%s)\n", path, strerror(errno));
            continue;
        }
        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        int gamepad = is_gamepad_like(fd);

        if (only_gamepad && !gamepad) { close(fd); continue; }

        struct input_id id;
        memset(&id, 0, sizeof(id));
        ioctl(fd, EVIOCGID, &id);

        printf("[%d] %s\n", count, path);
        printf("    Name:    %s\n", name);
        printf("    Vendor:  %04x  Product: %04x  Version: %04x\n",
               id.vendor, id.product, id.version);
        printf("    Bus:     %d (%s)\n", id.bustype,
               id.bustype == BUS_USB ? "USB" :
               id.bustype == BUS_BLUETOOTH ? "Bluetooth" :
               id.bustype == BUS_VIRTUAL ? "virtual" : "other");
        printf("    Gamepad: %s\n", gamepad ? "YES" : "no");
        if (gamepad) print_bits(fd);
        printf("\n");

        out_idx[count] = fd;
        count++;
    }
    closedir(d);
    return count;
}

static const char *btn_name(int code) {
    switch (code) {
    case BTN_SOUTH: return "A/South";
    case BTN_EAST:  return "B/East";
    case BTN_NORTH: return "Y/North";
    case BTN_WEST:  return "X/West";
    case BTN_TL:    return "LB";
    case BTN_TR:    return "RB";
    case BTN_TL2:   return "LT";
    case BTN_TR2:   return "RT";
    case BTN_SELECT:return "Back";
    case BTN_START: return "Start";
    case BTN_MODE:  return "Guide";
    case BTN_THUMBL:return "L3";
    case BTN_THUMBR:return "R3";
    case BTN_DPAD_UP: return "DPad Up";
    case BTN_DPAD_DOWN: return "DPad Down";
    case BTN_DPAD_LEFT: return "DPad Left";
    case BTN_DPAD_RIGHT:return "DPad Right";
    default: return "BTN_?";
    }
}

static const char *abs_name(int code) {
    switch (code) {
    case ABS_X: return "LX";
    case ABS_Y: return "LY";
    case ABS_Z: return "Z/LT";
    case ABS_RX: return "RX";
    case ABS_RY: return "RY";
    case ABS_RZ: return "RZ/RT";
    case ABS_HAT0X: return "HatX";
    case ABS_HAT0Y: return "HatY";
    case ABS_BRAKE: return "Brake";
    case ABS_GAS: return "Gas";
    default: return "ABS_?";
    }
}

int main(int argc, char *argv[]) {
    printf("evdev Gamepad Test (direct /dev/input/event* interface)\n");
    printf("=========================================================\n\n");

    int fds[64];
    int n = scan_devices(fds, 1);

    if (n == 0) {
        printf("No gamepad-like evdev devices found.\n");
        printf("This means the kernel is NOT exposing the controller as an input\n");
        printf("device. Check: lsusb (is it on USB bus?), dmesg (driver errors?),\n");
        printf("loaded modules (xpad, hid-generic, etc.).\n");
        return 1;
    }

    int pick = 0;
    if (argc > 1) pick = atoi(argv[1]);
    if (pick < 0 || pick >= n) pick = 0;
    printf(">>> Listening on device index %d (pass 0..%d as arg to pick another)\n",
           pick, n - 1);
    printf(">>> Press Ctrl+C to exit.\n\n");
    int fd = fds[pick];
    for (int i = 0; i < n; i++) if (i != pick) close(fds[i]);

    int abs_range[ABS_MAX][2];
    memset(abs_range, 0, sizeof(abs_range));
    for (int i = 0; i < ABS_MAX; i++) {
        struct input_absinfo abs;
        if (ioctl(fd, EVIOCGABS(i), &abs) == 0) {
            abs_range[i][0] = abs.minimum;
            abs_range[i][1] = abs.maximum;
        }
    }

    struct input_event ev;
    while (1) {
        ssize_t r = read(fd, &ev, sizeof(ev));
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (r < (ssize_t)sizeof(ev)) continue;

        if (ev.type == EV_KEY) {
            printf("[BTN] %-12s %s\n", btn_name(ev.code),
                   ev.value ? "DOWN" : "UP  ");
        } else if (ev.type == EV_ABS) {
            int lo = abs_range[ev.code][0];
            int hi = abs_range[ev.code][1];
            float norm = (hi > lo) ? (float)(ev.value - lo) / (hi - lo) * 2.0f - 1.0f : 0;
            printf("[AXIS] %-8s raw=%6d  norm=%+.3f\n",
                   abs_name(ev.code), ev.value, norm);
        } else if (ev.type == EV_SYN) {
            /* frame sync, ignore for brevity */
        }
        fflush(stdout);
    }

    close(fd);
    return 0;
}
