/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's store on the FM-1 (dmk_fm1.c includes it; host/fm1_flash_test.c runs it on a simulated chip): the core's
 * records in Demake's 64 KiB, a 4 KiB sector a write, the record's header written last so a cut save never reads as a
 * record. Called from the panel (the loop), never inside a render: the second core is idle and reads no flash; the
 * erase stops the sound ~50 ms, so the core saves only while stopped. Needs BANK_AT, BANK_LEN, flash_ok, st_read,
 * abuf, HALF_WORDS and fm1_flash.h */
static int store_read(uint32_t at, void *p, uint32_t n)
{
    if (at > BANK_LEN || n > BANK_LEN - at) return -1;
    return st_read(BANK_AT + at, p, n);
}
static int store_write(uint32_t at, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t took;
    int rc;
    if (!flash_ok || (at & 0xFFFu) || n > 0x1000u || at + 0x1000u > BANK_LEN) return -1;
    { uint32_t i; for (i = 0; i < 2u * HALF_WORDS; i++) abuf[i] = 0; }   /* the DMA loops its two halves while the
                                                   erase holds the interrupts: silence, not the last 11 ms buzzing */
    if ((rc = fl_erase4k(BANK_AT + at, &took)) != 0) return rc;
    if (n > 16u && (rc = fl_write(BANK_AT + at + 16u, b + 16, n - 16u)) != 0) return rc;
    return fl_write(BANK_AT + at, b, n < 16u ? n : 16u);
}
