/*
 * KEELOQ test vectors — validate implementation against li0ard/keeloq
 * These are exactly the vectors from the reference TypeScript library tests.
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "keeloq.h"
#include "pfx_keys.h"

static int passed = 0, failed = 0;

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    if (got == expected) {
        printf("  ✓ %-40s got=0x%08X\n", name, got);
        passed++;
    } else {
        printf("  ✗ %-40s got=0x%08X expected=0x%08X\n", name, got, expected);
        failed++;
    }
}

static void check_u64(const char *name, uint64_t got, uint64_t expected) {
    if (got == expected) {
        printf("  ✓ %-40s got=0x%016" PRIX64 "\n", name, got);
        passed++;
    } else {
        printf("  ✗ %-40s got=0x%016" PRIX64 " expected=0x%016" PRIX64 "\n",
               name, got, expected);
        failed++;
    }
}

int main(void) {
    printf("=== KEELOQ Test Vectors (=from li0ard/keeloq reference) ===\n\n");

    /* Test 1: Simple learning
     * key = 0x123456789ABCDEFn (=63 bit truncated), hop = 0xf16c47a6
     * expected decrypt raw = 0x21eb000a
     */
    printf("[Simple learning]\n");
    uint64_t key1 = 0x123456789ABCDEFULL;
    uint32_t hop1 = 0xf16c47a6;
    uint32_t dec1 = keeloq_decrypt(hop1, key1);
    check_u32("decrypt(0xf16c47a6, key=0x123..)", dec1, 0x21eb000a);

    /* Test 2: Normal learning
     * fix = 0x1ee2b949, key = 0x123456789ABCDEFn
     * expected man (=derived crypt_key) = 0xd107d43929d4778n
     */
    printf("\n[Normal learning]\n");
    uint64_t man2 = keeloq_normal_learning(0x1ee2b949, key1);
    check_u64("normal_learning(serial=0x1ee2b949)", man2, 0x0d107d43929d4778ULL);

    /* Then decrypt hop 0xc2be08b0 with the derived key */
    uint32_t hop2 = 0xc2be08b0;
    uint32_t dec2 = keeloq_decrypt(hop2, man2);
    check_u32("decrypt(0xc2be08b0, normal_key)", dec2, 0x11490006);

    /* Test 3: Secure learning
     * fix=0x11111111, seed=0x22222222, key=0x123..
     * expected man = 0xf2d8c1b388888888n
     */
    printf("\n[Secure learning]\n");
    uint64_t man3 = keeloq_secure_learning(0x11111111, 0x22222222, key1);
    check_u64("secure_learning(0x11..., seed=0x22...)", man3, 0xf2d8c1b388888888ULL);

    /* Test 4: encrypt/decrypt round-trip */
    printf("\n[Round-trip encrypt/decrypt]\n");
    uint32_t original = 0xDEADBEEF;
    uint32_t enc = keeloq_encrypt(original, key1);
    uint32_t dec = keeloq_decrypt(enc, key1);
    check_u32("decrypt(encrypt(0xDEADBEEF))", dec, original);

    /* Test 5: ancrage DEVMEL - le decrypt doit etre byte-identique au binaire
     * AirSendWebService (528-loop @0x5059e9 force en gdb, session reverse). */
    printf("\n[Ancrage DEVMEL]\n");
    uint32_t dvm = keeloq_decrypt(0x12345678u, 0xabcdef0123456789ULL);
    check_u32("decrypt(0x12345678, 0xabcdef..) == DEVMEL", dvm, 0x7CC862BEu);

    /* Test 6: sélection PFX par slot DEVMEL (table 0x970D08),
     * plus oracle PAIRMODE slot 54. */
    printf("\n[Selection cle PFX + roundtrip 63 cles]\n");
    uint64_t k0 = 0; uint8_t i0 = 0xFF;
    bool ok0 = pfx_key_for_serial(0x1067u, &k0, &i0);   /* slot 1, index 0 */
    check_u32("pfx serial 0x1067 -> index", (ok0 ? i0 : 0xFF), 0);
    check_u64("pfx serial 0x1067 -> crypt_key", k0, 0x8F96866FBFD866B1ULL);
    /* serial non-PFX (mauvais marqueur) doit etre rejete */
    check_u32("pfx serial 0x0400 rejete (pas 0x067)",
              pfx_key_for_serial(0x0400u, &k0, &i0) ? 1 : 0, 0);
    int rt_ok = 1;
    for (uint32_t idx = 0; idx < PFX_KEY_COUNT; idx++) {
        uint32_t serial = ((idx + 1) << 12) | PFX_FAMILY_MARKER;
        uint64_t k; if (!pfx_key_for_serial(serial, &k, NULL)) { rt_ok = 0; break; }
        uint32_t P = 0x0abc0007u ^ (idx << 20);
        if (keeloq_decrypt(keeloq_encrypt(P, k), k) != P) { rt_ok = 0; break; }
    }
    check_u32("roundtrip encrypt/decrypt sur 63 cles PFX", rt_ok, 1);

    uint64_t k54 = 0; uint8_t i54 = 0xFF;
    bool ok54 = pfx_key_for_serial(0x36067u, &k54, &i54);
    check_u32("oracle slot 54 -> index", ok54 ? i54 : 0xFF, 53);
    check_u64("oracle slot 54 -> key", k54, 0x41AA649E6ED6E5A7ULL);
    check_u32("oracle PAIRMODE hop decrypt",
              keeloq_decrypt(0xF029775Bu, k54), 0x00670002u);

    /* Trame spéciale d'enrôlement observée chez DEVMEL : ce n'est ni le
     * placeholder zéro, ni le bouton PROG physique 0x8. */
    uint32_t enroll_plain = (0x5u << 28) | (0x067u << 16) | 0x0003u;
    check_u32("plaintext ENROLL DEVMEL btn=5/disc=067/cnt=3",
              enroll_plain, 0x50670003u);
    check_u32("roundtrip ENROLL DEVMEL slot 54",
              keeloq_decrypt(keeloq_encrypt(enroll_plain, k54), k54),
              0x50670003u);

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
