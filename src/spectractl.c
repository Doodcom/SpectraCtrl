/*
 * spectractl - control Zotac SPECTRA RGB lighting on GeForce 40-series
 * "4A" cards (Trinity / Twin Edge / Solid, incl. 4070 Ti SUPER) from Linux.
 *
 * The RGB MCU sits on the GPU's i2c bus at address 0x4A but only speaks at
 * 10 kHz, which the kernel's /dev/i2c-* adapters (fixed 100 kHz) can't do.
 * We go through the NVIDIA RM API (/dev/nvidiactl) instead and issue
 * NV402C_CTRL_CMD_I2C_TRANSACTION with SPEED_MODE_10KHZ - the same path
 * Zotac FireStorm uses on Windows via NvAPI.
 *
 * Protocol reverse-engineered by Peter Berendi in OpenRGB MR !2625.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "nvtypes.h"
#include "nvstatus.h"
#include "nvos.h"
#include "nv_escape.h"
#include "nv-ioctl-numbers.h"
#include "nv-ioctl.h"
#include "class/cl0080.h"
#include "class/cl2080.h"
#include "class/cl402c.h"
#include "ctrl/ctrl402c.h"

#define NVCTL_DEV "/dev/nvidiactl"
#define NVDEV_FMT "/dev/nvidia%d"

#define H_DEVICE 0x5F000001
#define H_SUBDEV 0x5F000002
#define H_I2C    0x5F000003

#define SPECTRA_ADDR      0x4A
#define SPECTRA_REG       0x5A
#define SPECTRA_ID_LEN    10
#define SPECTRA_PKT_LEN   10
#define SPECTRA_MAXSPEED  0xFE

enum {
    MODE_STATIC   = 0x00,
    MODE_BREATHE  = 0x01,
    MODE_FADE     = 0x02,
    MODE_WINK     = 0x03,
    MODE_SPECTRUM = 0x04,   /* spectrum breathe, not exposed in FireStorm */
    MODE_RANDOM   = 0x05,
};

static const char *mode_names[] = {
    "static", "breathe", "fade", "wink", "spectrum", "random"
};
#define N_MODES 6

struct spectra {
    int      ctl;
    int      dev0;
    NvHandle hClient;
    int      port;
    char     id[SPECTRA_ID_LEN + 1];
    NvU8     state[32];
};

static int nv_ioctl(int fd, unsigned cmd, void *data, size_t size)
{
    return ioctl(fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, cmd, size), data);
}

static int check_version(int fd)
{
    char ver[64] = {0};
    FILE *f = fopen("/proc/driver/nvidia/version", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            for (char *p = line; *p; ) {
                if (isdigit((unsigned char)*p)) {
                    int dots = 0;
                    char *q = p;
                    while (isdigit((unsigned char)*q) || *q == '.') {
                        if (*q == '.') dots++;
                        q++;
                    }
                    if (dots >= 1 && q - p < 63) {
                        memcpy(ver, p, q - p);
                        break;
                    }
                    p = q;
                } else {
                    p++;
                }
            }
        }
        fclose(f);
    }
    nv_ioctl_rm_api_version_t v;
    memset(&v, 0, sizeof(v));
    v.cmd = NV_RM_API_VERSION_CMD_STRICT;
    snprintf(v.versionString, sizeof(v.versionString), "%s", ver);
    if (nv_ioctl(fd, NV_ESC_CHECK_VERSION_STR, &v, sizeof(v)) < 0)
        return -1;
    return v.reply == NV_RM_API_VERSION_REPLY_RECOGNIZED ? 0 : -1;
}

static NvU32 rm_alloc(int fd, NvHandle hClient, NvHandle hParent, NvHandle hNew,
                      NvU32 hClass, void *parms, NvU32 parmsSize, NvHandle *hOut)
{
    NVOS21_PARAMETERS p;
    memset(&p, 0, sizeof(p));
    p.hRoot         = hClient;
    p.hObjectParent = hParent;
    p.hObjectNew    = hNew;
    p.hClass        = hClass;
    p.pAllocParms   = (NvP64)(uintptr_t)parms;
    p.paramsSize    = parmsSize;
    if (nv_ioctl(fd, NV_ESC_RM_ALLOC, &p, sizeof(p)) < 0)
        return 0xFFFFFFFF;
    if (p.status == 0 && hOut)
        *hOut = p.hObjectNew;
    return p.status;
}

static NvU32 rm_control(int fd, NvHandle hClient, NvHandle hObject,
                        NvU32 cmd, void *params, NvU32 size)
{
    NVOS54_PARAMETERS p;
    memset(&p, 0, sizeof(p));
    p.hClient    = hClient;
    p.hObject    = hObject;
    p.cmd        = cmd;
    p.params     = (NvP64)(uintptr_t)params;
    p.paramsSize = size;
    if (nv_ioctl(fd, NV_ESC_RM_CONTROL, &p, sizeof(p)) < 0)
        return 0xFFFFFFFF;
    return p.status;
}

static NvU32 i2c_xfer(struct spectra *s, int port, int write, NvU8 *buf, NvU32 len)
{
    NV402C_CTRL_I2C_TRANSACTION_PARAMS t;
    memset(&t, 0, sizeof(t));
    t.portId        = (NvU8)port;
    /* NV402C_CTRL_I2C_FLAGS_SPEED_MODE is bitfield 4:1 of flags */
    t.flags         = NV402C_CTRL_I2C_FLAGS_SPEED_MODE_10KHZ << 1;
    t.deviceAddress = SPECTRA_ADDR << 1;
    t.transType     = NV402C_CTRL_I2C_TRANSACTION_TYPE_I2C_BUFFER_RW;
    t.transData.i2cBufferData.bWrite          = write ? NV_TRUE : NV_FALSE;
    t.transData.i2cBufferData.registerAddress = SPECTRA_REG;
    t.transData.i2cBufferData.messageLength   = len;
    t.transData.i2cBufferData.pMessage        = (NvP64)(uintptr_t)buf;
    return rm_control(s->ctl, s->hClient, H_I2C,
                      NV402C_CTRL_CMD_I2C_TRANSACTION, &t, sizeof(t));
}

static int id_plausible(const NvU8 *buf)
{
    /* controller IDs look like "N702E-1002" */
    for (int i = 0; i < SPECTRA_ID_LEN; i++)
        if (buf[i] < 32 || buf[i] > 126)
            return 0;
    return memchr(buf, '-', SPECTRA_ID_LEN) != NULL;
}

/* The MCU wants ~200ms between transactions and drops requests that come
 * sooner. Retry a few times with that spacing. */
static int try_port(struct spectra *s, int port, int attempts)
{
    for (int i = 0; i < attempts; i++) {
        if (i > 0)
            usleep(200 * 1000);
        memset(s->state, 0, sizeof(s->state));
        if (i2c_xfer(s, port, 0, s->state, sizeof(s->state)) == 0 &&
            id_plausible(s->state))
            return 0;
    }
    return -1;
}

static const char *port_cache_path(void)
{
    static char path[512];
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    snprintf(path, sizeof(path), "%s/.cache/spectractl.port", home);
    return path;
}

static int spectra_open(struct spectra *s)
{
    memset(s, 0, sizeof(*s));
    s->ctl = open(NVCTL_DEV, O_RDWR);
    if (s->ctl < 0) {
        perror(NVCTL_DEV);
        return -1;
    }
    char devpath[32];
    snprintf(devpath, sizeof(devpath), NVDEV_FMT, 0);
    s->dev0 = open(devpath, O_RDWR);
    if (s->dev0 < 0) {
        perror(devpath);
        return -1;
    }
    if (check_version(s->ctl) != 0) {
        fprintf(stderr, "RM API version handshake failed\n");
        return -1;
    }

    NvU32 st = rm_alloc(s->ctl, 0, 0, 0, NV01_ROOT_CLIENT, NULL, 0, &s->hClient);
    if (st != 0) goto fail;

    NV0080_ALLOC_PARAMETERS dp;
    memset(&dp, 0, sizeof(dp));
    dp.deviceId = 0;
    dp.hClientShare = s->hClient;
    st = rm_alloc(s->ctl, s->hClient, s->hClient, H_DEVICE, NV01_DEVICE_0, &dp, sizeof(dp), NULL);
    if (st != 0) goto fail;

    NV2080_ALLOC_PARAMETERS sp;
    memset(&sp, 0, sizeof(sp));
    st = rm_alloc(s->ctl, s->hClient, H_DEVICE, H_SUBDEV, NV20_SUBDEVICE_0, &sp, sizeof(sp), NULL);
    if (st != 0) goto fail;

    st = rm_alloc(s->ctl, s->hClient, H_SUBDEV, H_I2C, NV40_I2C, NULL, 0, NULL);
    if (st != 0) goto fail;

    /* find the port the Spectra MCU answers on; cached after first success */
    const char *cache = port_cache_path();
    if (cache) {
        FILE *f = fopen(cache, "r");
        if (f) {
            int port = -1;
            if (fscanf(f, "%d", &port) == 1 && port >= 0 && port < 16 &&
                try_port(s, port, 3) == 0) {
                fclose(f);
                s->port = port;
                memcpy(s->id, s->state, SPECTRA_ID_LEN);
                return 0;
            }
            fclose(f);
        }
    }
    for (int port = 0; port < 16; port++) {
        if (try_port(s, port, port == 0 ? 1 : 2) == 0) {
            s->port = port;
            memcpy(s->id, s->state, SPECTRA_ID_LEN);
            if (cache) {
                FILE *f = fopen(cache, "w");
                if (f) {
                    fprintf(f, "%d\n", port);
                    fclose(f);
                }
            }
            return 0;
        }
    }
    fprintf(stderr, "no Zotac Spectra controller found on any i2c port\n");
    return -1;

fail:
    fprintf(stderr, "RM object allocation failed (status 0x%08x)\n", st);
    return -1;
}

/* status response: bytes 0-9 = ID, then a copy of the last control packet
 * starting at its on/off byte, i.e. on=state[10], mode=state[11], ... */
struct cfg {
    NvU8 on, mode, r, g, b, t_on, t_off, preset;
};

static struct cfg read_cfg(struct spectra *s)
{
    struct cfg c = {
        .on     = s->state[10],
        .mode   = s->state[11],
        .r      = s->state[12],
        .g      = s->state[13],
        .b      = s->state[14],
        .t_on   = s->state[15],
        .t_off  = s->state[16],
        .preset = s->state[17],
    };
    return c;
}

static int write_cfg(struct spectra *s, struct cfg c, int reset)
{
    NvU8 pkt[SPECTRA_PKT_LEN] = {
        reset ? 1 : 0, c.on, c.mode, c.r, c.g, c.b,
        c.t_on, c.t_off, c.preset, 0x00
    };
    usleep(200 * 1000);   /* MCU cooldown after the open()-time read */
    NvU32 st = i2c_xfer(s, s->port, 1, pkt, sizeof(pkt));
    if (st != 0) {
        fprintf(stderr, "i2c write failed (status 0x%08x)\n", st);
        return -1;
    }
    return 0;
}

static int parse_color(const char *hex, struct cfg *c)
{
    unsigned r, g, b;
    if (strlen(hex) == 7 && hex[0] == '#')
        hex++;
    if (strlen(hex) != 6 || sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) {
        fprintf(stderr, "bad color '%s' (want RRGGBB)\n", hex);
        return -1;
    }
    c->r = r; c->g = g; c->b = b;
    return 0;
}

static void print_status(struct spectra *s)
{
    struct cfg c = read_cfg(s);
    printf("controller: %s (i2c port %d, addr 0x%02X)\n", s->id, s->port, SPECTRA_ADDR);
    printf("power:      %s\n", c.on ? "on" : "off");
    printf("mode:       %s\n", c.mode < N_MODES ? mode_names[c.mode] : "unknown");
    printf("color:      #%02X%02X%02X\n", c.r, c.g, c.b);
    printf("timing:     on=%u off=%u preset=%u\n", c.t_on, c.t_off, c.preset);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s <command>\n"
        "\n"
        "  status                       show controller ID and current settings\n"
        "  color RRGGBB                 static color\n"
        "  mode <name> [RRGGBB] [speed] set effect; speed 1-253, default 209\n"
        "       names: static breathe fade wink spectrum random\n"
        "  on | off                     lights on/off (settings kept)\n"
        "  reset                        restore controller defaults\n"
        "\n"
        "settings persist in the card across reboots\n",
        argv0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    struct spectra s;
    if (spectra_open(&s) != 0)
        return 1;

    const char *cmd = argv[1];
    struct cfg c = read_cfg(&s);

    if (strcmp(cmd, "status") == 0) {
        print_status(&s);
        return 0;
    }

    if (strcmp(cmd, "on") == 0 || strcmp(cmd, "off") == 0) {
        c.on = cmd[1] == 'n';
        return write_cfg(&s, c, 0) ? 1 : 0;
    }

    if (strcmp(cmd, "reset") == 0)
        return write_cfg(&s, c, 1) ? 1 : 0;

    if (strcmp(cmd, "color") == 0) {
        if (argc < 3) { usage(argv[0]); return 2; }
        if (parse_color(argv[2], &c) != 0)
            return 2;
        c.on = 1;
        c.mode = MODE_STATIC;
        c.t_on = 45; c.t_off = 10; c.preset = 8;
        return write_cfg(&s, c, 0) ? 1 : 0;
    }

    if (strcmp(cmd, "mode") == 0) {
        if (argc < 3) { usage(argv[0]); return 2; }
        int mode = -1;
        for (int i = 0; i < N_MODES; i++)
            if (strcmp(argv[2], mode_names[i]) == 0)
                mode = i;
        if (mode < 0) {
            fprintf(stderr, "unknown mode '%s'\n", argv[2]);
            return 2;
        }
        int speed = 209;   /* SPECTRA_MAXSPEED - 45, the controller default */
        for (int i = 3; i < argc; i++) {
            if (strchr(argv[i], '#') || strlen(argv[i]) == 6) {
                if (parse_color(argv[i], &c) != 0)
                    return 2;
            } else {
                speed = atoi(argv[i]);
                if (speed < 1 || speed > SPECTRA_MAXSPEED - 1) {
                    fprintf(stderr, "speed out of range (1-%d)\n", SPECTRA_MAXSPEED - 1);
                    return 2;
                }
            }
        }
        c.on = 1;
        c.mode = mode;
        c.preset = 8;
        switch (mode) {
        case MODE_WINK:
        case MODE_RANDOM:
            c.t_on = 10;
            c.t_off = SPECTRA_MAXSPEED - speed;
            break;
        case MODE_BREATHE:
        case MODE_SPECTRUM:
        case MODE_FADE:
            c.t_on = SPECTRA_MAXSPEED - speed;
            c.t_off = 10;
            break;
        default:
            c.t_on = 45;
            c.t_off = 10;
        }
        return write_cfg(&s, c, 0) ? 1 : 0;
    }

    usage(argv[0]);
    return 2;
}
