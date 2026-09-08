/* SPDX-License-Identifier: GPL-2.0-only */
/* C ABI over the g25 protocol library, for consumers outside the C++ tree
 * (the .NET GeForce NOW bridge). Everything here is a thin wrapper over
 * src/protocol/. Byte layouts live there and in docs/protocol.md. */
#ifndef LIBG25_H
#define LIBG25_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(LIBG25_BUILD)
#    define LIBG25_API __declspec(dllexport)
#  else
#    define LIBG25_API __declspec(dllimport)
#  endif
#else
#  define LIBG25_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -1 bad args, otherwise the value described. */

enum g25_model {
    G25_MODEL_UNKNOWN = 0,
    G25_MODEL_G25 = 1,
    G25_MODEL_G27 = 2,
    G25_MODEL_G29 = 3
};

typedef struct g25_input_state {
    uint16_t wheel;    /* 0..16383, 8191/8192 = centre */
    uint8_t throttle;  /* 0..255, 255 = released */
    uint8_t brake;
    uint8_t clutch;
    uint8_t hat;       /* 0..7 = 45-degree steps, >=8 = centred */
    uint32_t buttons;  /* bit 0 = button 1 */
} g25_input_state;

/* Model from USB pid + bcdDevice/revision. */
LIBG25_API int32_t g25_identify_model(uint16_t pid, uint16_t revision);

/* Decode a Windows HID input report: 12 bytes, report id 0 + 11-byte payload.
 * Returns 0 on success, -1 on bad input. */
LIBG25_API int32_t g25_decode_input(const uint8_t *report, int32_t len,
                                    g25_input_state *out);

/* Wheel commands. Each writes an 8-byte Windows HID output report into out8
 * (leading 0x00 report id + 7 command bytes). */
LIBG25_API void g25_cmd_native_mode(uint8_t *out8);
LIBG25_API void g25_cmd_stop_all(uint8_t *out8);
LIBG25_API void g25_cmd_disable_autocenter(uint8_t *out8);
/* degrees 40..900. Returns 0 on success, -1 out of range. */
LIBG25_API int32_t g25_cmd_set_range(int32_t degrees, uint8_t *out8);

/* Translate one force-feedback output report that the virtual G29 received into
 * zero or more 8-byte G25 output reports.
 *   mode   : 0 = passthrough (first 7 payload bytes -> one report, unchanged);
 *            1 = translate GeForce NOW's G29 reports to G25 classic commands
 *                (constant force, condition effects) - see docs/ffb-protocol.md.
 *   in / in_len : the raw report from the virtual G29 (a leading report-id byte
 *                 is tolerated).
 *   out    : buffer of out_cap * 8 bytes.
 * Returns the number of 8-byte reports written (0..out_cap), or -1 on error. */
LIBG25_API int32_t g25_ffb_translate(int32_t mode, const uint8_t *in, int32_t in_len,
                                     uint8_t *out, int32_t out_cap);

/* Semver of this DLL's contract. */
LIBG25_API const char *g25_libg25_version(void);

#ifdef __cplusplus
}
#endif
#endif /* LIBG25_H */
