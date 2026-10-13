// m95m04.c(프로토콜 로직)를 PC에서 검증하는 호스트 테스트.
// 가짜 M95M04 칩이 SPI 바이트 스트림을 해석한다: 명령 WREN/WRITE/READ/RDSR, 3바이트 주소, 페이지 512바이트(경계에서 되감김),
// WEL(쓰기 허용 래치), WIP(쓰기 진행 중), 블록 보호(BP) 비트.
//
// 빌드/실행:  gcc -Wall -Wextra -I.. -o test_m95m04 test_m95m04.c ../../m95m04.c && ./test_m95m04

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "m95m04.h"

#include "fake_m95m04.inc"

// ---- 테스트 ---------------------------------------------------------------------------------------------------
static m95_byte pattern(uint32_t i) { return (m95_byte)((i * 7u + 13u) & 0xFFu); }

static void make_pattern(m95_byte *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) buf[i] = pattern(i);
}

static void test_read_status_of_fresh_chip(void) {
    chip_reset();
    m95_byte sr = 0xAA;
    assert(M95M04_readStatus(&sr) == M95_OK);
    assert(sr == 0x00);
}

static void test_write_then_read_back_small(void) {
    chip_reset();
    const m95_byte data[] = {'h', 'e', 'l', 'l', 'o'};
    assert(M95M04_write(0x100, data, 5) == M95_OK);
    m95_byte back[5] = {0};
    assert(M95M04_read(0x100, back, 5) == M95_OK);
    assert(memcmp(back, data, 5) == 0);
    assert(chip.writeTransactions == 1);
    assert(chip.wrenCount == 1);
}

static void test_write_across_page_boundary_is_split(void) {
    chip_reset();
    // 페이지 0의 끝(주소 510)에서 4바이트: 510~511은 페이지 0, 512~513은 페이지 1이어야 한다
    const m95_byte data[4] = {0x11, 0x22, 0x33, 0x44};
    assert(M95M04_write(510, data, 4) == M95_OK);
    assert(chip.writeTransactions == 2);
    assert(chip.writeAddr[0] == 510 && chip.writeLen[0] == 2);
    assert(chip.writeAddr[1] == 512 && chip.writeLen[1] == 2);
    assert(chip.wrenCount == 2);   // 페이지마다 WREN
    // 나눠 쓰지 않았다면 되감김으로 페이지 0의 앞쪽(주소 0, 1)이 덮어써졌을 것이다
    assert(chip.mem[0] == 0xFF && chip.mem[1] == 0xFF);
    m95_byte back[4];
    assert(M95M04_read(510, back, 4) == M95_OK);
    assert(memcmp(back, data, 4) == 0);
}

static void test_write_exactly_one_page_and_multi_page_unaligned(void) {
    chip_reset();
    static m95_byte data[1500], back[1500];
    make_pattern(data, 1500);

    assert(M95M04_write(512, data, 512) == M95_OK);    // 정확히 한 페이지
    assert(chip.writeTransactions == 1 && chip.writeLen[0] == 512);

    chip_reset();
    assert(M95M04_write(300, data, 1500) == M95_OK);   // 300 .. 1799: 212 + 512 + 512 + 264
    assert(chip.writeTransactions == 4);
    assert(chip.writeLen[0] == 212 && chip.writeLen[1] == 512 && chip.writeLen[2] == 512 && chip.writeLen[3] == 264);
    assert(M95M04_read(300, back, 1500) == M95_OK);
    assert(memcmp(back, data, 1500) == 0);
    assert(chip.mem[299] == 0xFF && chip.mem[1800] == 0xFF);   // 앞뒤 이웃은 건드리지 않음
}

static void test_last_byte_of_chip_is_writable_and_readable(void) {
    chip_reset();
    const m95_byte v = 0x5A;
    assert(M95M04_write(M95M04_SIZE_BYTES - 1, &v, 1) == M95_OK);
    m95_byte back = 0;
    assert(M95M04_read(M95M04_SIZE_BYTES - 1, &back, 1) == M95_OK);
    assert(back == 0x5A);
}

static void test_range_errors_do_not_touch_the_chip(void) {
    chip_reset();
    m95_byte buf[16] = {0};
    assert(M95M04_write(M95M04_SIZE_BYTES, buf, 1) == M95_ERR_RANGE);
    assert(M95M04_write(M95M04_SIZE_BYTES - 4, buf, 16) == M95_ERR_RANGE);   // 끝을 넘어가면 일부만 쓰지 않고 거부
    assert(M95M04_read(M95M04_SIZE_BYTES - 4, buf, 16) == M95_ERR_RANGE);
    assert(chip.writeTransactions == 0 && chip.wrenCount == 0);
    assert(chip.mem[M95M04_SIZE_BYTES - 4] == 0xFF);
}

static void test_zero_length_and_null_buffer(void) {
    chip_reset();
    m95_byte b = 0;
    assert(M95M04_write(0, &b, 0) == M95_OK);
    assert(M95M04_read(0, &b, 0) == M95_OK);
    assert(M95M04_write(0, NULL, 4) == M95_ERR_PARAM);
    assert(M95M04_read(0, NULL, 4) == M95_ERR_PARAM);
    assert(chip.writeTransactions == 0);
}

static void test_write_timeout_when_wip_never_clears(void) {
    chip_reset();
    chip.neverReady = true;
    const m95_byte data[2] = {1, 2};
    assert(M95M04_write(0, data, 2) == M95_ERR_TIMEOUT);
    assert(chip.waitSteps >= 100 && chip.waitSteps <= 400);   // 한계(20ms)까지만 기다린다(100µs 단계 약 200회)
}

static void test_write_protected_chip_is_detected_by_verify(void) {
    chip_reset();
    chip.bp = 0x3;   // 전체 보호: 쓰기가 조용히 무시된다
    const m95_byte data[4] = {9, 8, 7, 6};
    assert(M95M04_write(0x200, data, 4) == M95_OK);   // 칩은 에러를 알리지 않는다
    uint32_t pos = 0;
    assert(M95M04_verify(0x200, data, 4, &pos) == M95_ERR_VERIFY);
    assert(pos == 0);
    m95_byte sr = 0;
    assert(M95M04_readStatus(&sr) == M95_OK);
    assert((sr & M95M04_SR_BP_MASK) == 0x0C);
}

static void test_verify_reports_first_mismatch_position(void) {
    chip_reset();
    m95_byte data[100], expect[100];
    make_pattern(data, 100);
    memcpy(expect, data, 100);
    assert(M95M04_write(1000, data, 100) == M95_OK);
    uint32_t pos = 12345;
    assert(M95M04_verify(1000, expect, 100, &pos) == M95_OK);
    expect[37] ^= 0x01;
    expect[80] ^= 0x80;
    assert(M95M04_verify(1000, expect, 100, &pos) == M95_ERR_VERIFY);
    assert(pos == 37);
}

static void test_verify_longer_than_internal_chunk(void) {
    chip_reset();
    static m95_byte data[700];
    make_pattern(data, 700);
    assert(M95M04_write(0, data, 700) == M95_OK);
    uint32_t pos = 0;
    assert(M95M04_verify(0, data, 700, &pos) == M95_OK);
    data[699] ^= 0xFF;
    assert(M95M04_verify(0, data, 700, &pos) == M95_ERR_VERIFY);
    assert(pos == 699);
}

static void test_fill_across_pages(void) {
    chip_reset();
    assert(M95M04_fill(500, 0xA5, 1100) == M95_OK);   // 500 .. 1599
    assert(chip.mem[499] == 0xFF && chip.mem[500] == 0xA5 && chip.mem[1599] == 0xA5 && chip.mem[1600] == 0xFF);
    uint32_t pos = 0;
    static m95_byte expect[1100];
    memset(expect, 0xA5, sizeof(expect));
    assert(M95M04_verify(500, expect, 1100, &pos) == M95_OK);
}

static void test_init_calls_hal_init_once(void) {
    chip_reset();
    halInitCalls = 0;
    M95M04_init();
    assert(halInitCalls == 1);
}

int main(void) {
    test_init_calls_hal_init_once();
    test_read_status_of_fresh_chip();
    test_write_then_read_back_small();
    test_write_across_page_boundary_is_split();
    test_write_exactly_one_page_and_multi_page_unaligned();
    test_last_byte_of_chip_is_writable_and_readable();
    test_range_errors_do_not_touch_the_chip();
    test_zero_length_and_null_buffer();
    test_write_timeout_when_wip_never_clears();
    test_write_protected_chip_is_detected_by_verify();
    test_verify_reports_first_mismatch_position();
    test_verify_longer_than_internal_chunk();
    test_fill_across_pages();
    printf("test_m95m04: all tests passed\n");
    return 0;
}
