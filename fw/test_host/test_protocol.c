/**
 * @file test_protocol.c
 * @brief Host-side unit tests for pure protocol logic (no hardware needed).
 *
 * Covers:
 *  - elaphureLink handshake packet validation and wire-format ABI
 *  - USBIP stage1/stage2 header ABI (sizes and command codes)
 *
 * Build & run (any host with a C compiler):
 *   gcc -Wall -Wextra -Werror -I fw fw/test_host/test_protocol.c -o test_protocol && ./test_protocol
 * Or from CI: see .github/workflows/build.yml
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "components/elaphureLink/elaphureLink_protocol.h"
#include "components/USBIP/usbip_defs.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

// ---------------------------------------------------------------------------
// elaphureLink: wire-format ABI
// ---------------------------------------------------------------------------
static void test_el_abi(void)
{
    // Handshake request/response are 3 packed big-endian u32 on the wire.
    CHECK(sizeof(el_request_handshake) == 12);
    CHECK(sizeof(el_response_handshake) == 12);
    CHECK(offsetof(el_request_handshake, command) == 4);
    CHECK(offsetof(el_request_handshake, el_proxy_version) == 8);

    // Identifier constant must match the protocol magic "\x8a" "elp".
    CHECK(EL_LINK_IDENTIFIER == 0x8a656c70u);
}

// ---------------------------------------------------------------------------
// elaphureLink: big-endian reader
// ---------------------------------------------------------------------------
static void test_el_be32(void)
{
    const uint8_t buf[] = {0x8a, 0x65, 0x6c, 0x70};
    CHECK(el_be32(buf) == 0x8a656c70u);

    const uint8_t zero[] = {0, 0, 0, 0};
    CHECK(el_be32(zero) == 0);

    // Unaligned access must work.
    const uint8_t shifted[] = {0xFF, 0x12, 0x34, 0x56, 0x78};
    CHECK(el_be32(shifted + 1) == 0x12345678u);
}

// ---------------------------------------------------------------------------
// elaphureLink: handshake validation
// ---------------------------------------------------------------------------
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void test_el_handshake_validate(void)
{
    uint8_t pkt[12];
    put_be32(pkt, EL_LINK_IDENTIFIER);
    put_be32(pkt + 4, EL_COMMAND_HANDSHAKE);
    put_be32(pkt + 8, 0x00010000); // proxy version, not validated

    // Well-formed packet passes.
    CHECK(el_handshake_validate(pkt, sizeof(pkt)) == 0);

    // Wrong length rejected.
    CHECK(el_handshake_validate(pkt, 11) == -1);
    CHECK(el_handshake_validate(pkt, 13) == -1);
    CHECK(el_handshake_validate(pkt, 0) == -1);

    // NULL rejected.
    CHECK(el_handshake_validate(NULL, sizeof(pkt)) == -1);

    // Wrong magic rejected.
    uint8_t bad_magic[12];
    memcpy(bad_magic, pkt, sizeof(pkt));
    put_be32(bad_magic, 0xDEADBEEF);
    CHECK(el_handshake_validate(bad_magic, sizeof(bad_magic)) == -1);

    // Wrong command rejected.
    uint8_t bad_cmd[12];
    memcpy(bad_cmd, pkt, sizeof(pkt));
    put_be32(bad_cmd + 4, 0x00000001);
    CHECK(el_handshake_validate(bad_cmd, sizeof(bad_cmd)) == -1);
}

// ---------------------------------------------------------------------------
// USBIP: header ABI
// ---------------------------------------------------------------------------
static void test_usbip_abi(void)
{
    // Stage1 header: u16 version + u16 command + u32 status, packed.
    CHECK(sizeof(usbip_stage1_header) == 8);

    // Command codes used by tcp_server protocol dispatch.
    CHECK(USBIP_STAGE1_CMD_DEVICE_LIST == 0x05);
    CHECK(USBIP_STAGE1_CMD_DEVICE_ATTACH == 0x03);

    CHECK(USBIP_STAGE2_REQ_SUBMIT == 0x0001);
    CHECK(USBIP_STAGE2_REQ_UNLINK == 0x0002);
    CHECK(USBIP_STAGE2_RSP_SUBMIT == 0x0003);
    CHECK(USBIP_STAGE2_RSP_UNLINK == 0x0004);

    CHECK(USBIP_DIR_OUT == 0);
    CHECK(USBIP_DIR_IN == 1);
}

int main(void)
{
    test_el_abi();
    test_el_be32();
    test_el_handshake_validate();
    test_usbip_abi();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
