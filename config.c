#include "config.h"

#include <ctype.h>
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mirror of main.c's dprintf — duplicated here so config.c doesn't have to
// pull in main.c's headers. Both write to the GS debug screen and stdout.
#define DPRINTF_PAD "       "
#define dprintf(fmt, ...) do { \
    scr_printf(DPRINTF_PAD fmt, ##__VA_ARGS__); \
    printf(fmt, ##__VA_ARGS__); \
} while (0)

void ps2http_config_defaults(ps2http_config_t *cfg)
{
    cfg->backend     = PS2HTTP_BACKEND_AUTO;
    cfg->ip_mode     = PS2HTTP_IP_MODE_DHCP;
    cfg->ip[0]       = 192; cfg->ip[1]       = 168; cfg->ip[2]       = 1; cfg->ip[3]       = 10;
    cfg->netmask[0]  = 255; cfg->netmask[1]  = 255; cfg->netmask[2]  = 255; cfg->netmask[3]  = 0;
    cfg->gateway[0]  = 192; cfg->gateway[1]  = 168; cfg->gateway[2]  = 1; cfg->gateway[3]  = 1;
}

static char *strip(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static bool parse_ipv4(const char *s, unsigned char out[4])
{
    unsigned a, b, c, d;
    char trail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trail) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out[0] = (unsigned char)a;
    out[1] = (unsigned char)b;
    out[2] = (unsigned char)c;
    out[3] = (unsigned char)d;
    return true;
}

static bool ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

int ps2http_config_load(ps2http_config_t *cfg, const char *dir)
{
    char path[FILENAME_MAX];
    int n = snprintf(path, sizeof(path), "%s/ps2_http.cfg", dir);
    if (n <= 0 || n >= (int)sizeof(path)) return -2;

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    dprintf("config: reading %s\n", path);

    char line[256];
    int  lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        // Strip inline comments — '#' starts one anywhere on the line.
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *p = strip(line);
        if (*p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) {
            dprintf("config: line %d: missing '='\n", lineno);
            continue;
        }
        *eq = '\0';
        char *key = strip(p);
        char *val = strip(eq + 1);

        if (ieq(key, "backend")) {
            if      (ieq(val, "ee"))   cfg->backend = PS2HTTP_BACKEND_EE;
            else if (ieq(val, "iop"))  cfg->backend = PS2HTTP_BACKEND_IOP;
            else if (ieq(val, "auto")) cfg->backend = PS2HTTP_BACKEND_AUTO;
            else dprintf("config: line %d: unknown backend '%s'\n", lineno, val);
        } else if (ieq(key, "ip_mode")) {
            if      (ieq(val, "dhcp"))   cfg->ip_mode = PS2HTTP_IP_MODE_DHCP;
            else if (ieq(val, "static")) cfg->ip_mode = PS2HTTP_IP_MODE_STATIC;
            else dprintf("config: line %d: unknown ip_mode '%s'\n", lineno, val);
        } else if (ieq(key, "ip")) {
            if (!parse_ipv4(val, cfg->ip))
                dprintf("config: line %d: bad ip '%s'\n", lineno, val);
        } else if (ieq(key, "netmask")) {
            if (!parse_ipv4(val, cfg->netmask))
                dprintf("config: line %d: bad netmask '%s'\n", lineno, val);
        } else if (ieq(key, "gateway")) {
            if (!parse_ipv4(val, cfg->gateway))
                dprintf("config: line %d: bad gateway '%s'\n", lineno, val);
        } else {
            dprintf("config: line %d: unknown key '%s'\n", lineno, key);
        }
    }

    fclose(f);
    return 0;
}

int ps2http_config_resolve(ps2http_config_t *cfg,
                           bool ee_available,
                           bool iop_available)
{
    if (!ee_available && !iop_available) return -1;

    if (cfg->backend == PS2HTTP_BACKEND_AUTO) {
        cfg->backend = ee_available ? PS2HTTP_BACKEND_EE : PS2HTTP_BACKEND_IOP;
    } else if (cfg->backend == PS2HTTP_BACKEND_EE && !ee_available) {
        dprintf("config: backend=ee requested but this build only has IOP — using IOP\n");
        cfg->backend = PS2HTTP_BACKEND_IOP;
    } else if (cfg->backend == PS2HTTP_BACKEND_IOP && !iop_available) {
        dprintf("config: backend=iop requested but this build only has EE — using EE\n");
        cfg->backend = PS2HTTP_BACKEND_EE;
    }

    if (cfg->backend == PS2HTTP_BACKEND_IOP && cfg->ip_mode == PS2HTTP_IP_MODE_DHCP) {
        dprintf("config: IOP backend does not support DHCP — falling back to static %u.%u.%u.%u\n",
                cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]);
        cfg->ip_mode = PS2HTTP_IP_MODE_STATIC;
    }
    return 0;
}
