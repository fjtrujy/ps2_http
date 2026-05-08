// Runtime configuration for ps2_http.
//
// Read once at boot from <g_root>/ps2_http.cfg (next to the ELF). Format is
// INI-style: `key = value`, '#' starts a comment, whitespace tolerant.
//
//     # ps2_http.cfg
//     backend = ee        # ee | iop  (only honoured by the "both" build)
//     ip_mode = dhcp      # dhcp | static
//     # ip      = 192.168.1.10
//     # netmask = 255.255.255.0
//     # gateway = 192.168.1.1

#ifndef PS2_HTTP_CONFIG_H
#define PS2_HTTP_CONFIG_H

#include <stdbool.h>

typedef enum {
    PS2HTTP_BACKEND_AUTO = 0,
    PS2HTTP_BACKEND_EE,
    PS2HTTP_BACKEND_IOP,
} ps2http_backend_t;

typedef enum {
    PS2HTTP_IP_MODE_DHCP = 0,
    PS2HTTP_IP_MODE_STATIC,
} ps2http_ip_mode_t;

typedef struct {
    ps2http_backend_t backend;
    ps2http_ip_mode_t ip_mode;
    unsigned char     ip[4];
    unsigned char     netmask[4];
    unsigned char     gateway[4];
} ps2http_config_t;

// Fill cfg with built-in defaults (backend AUTO, DHCP, 192.168.1.10/24 gw .1
// for the static fallback). Always succeeds.
void ps2http_config_defaults(ps2http_config_t *cfg);

// Read cfg from <dir>/ps2_http.cfg. Returns 0 if the file was opened and
// parsed, -1 if the file was missing, -2 on I/O / parse error. In every
// failure case the caller's cfg is preserved as-is (call ps2http_config_defaults
// first), so a missing file just falls back to defaults.
int  ps2http_config_load(ps2http_config_t *cfg, const char *dir);

// Resolve a config that came in with PS2HTTP_BACKEND_AUTO or impossible
// combinations into something the caller can act on:
//   - AUTO chooses EE if available, else IOP, else aborts.
//   - IOP + dhcp is not supported; force static and warn.
//   - A backend the build doesn't include is replaced with the available one.
// Prints warnings via dprintf. Returns 0 on success, -1 if no backend at all
// is compiled in (should be impossible).
int  ps2http_config_resolve(ps2http_config_t *cfg,
                            bool ee_available,
                            bool iop_available);

#endif
