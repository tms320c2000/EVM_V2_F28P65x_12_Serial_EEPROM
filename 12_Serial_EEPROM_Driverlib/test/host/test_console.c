// eeprom_cmds.c(명령어 쉘)를 PC에서 검증하는 호스트 테스트.
// 대상에서 쓰는 것과 같은 utils/ustdlib.c(경량 printf)와 utils/cmdline.c를 링크해서, 대상에서 실제로 나오는 출력 서식과
// 명령행 파싱을 그대로 확인한다. UART는 문자열 버퍼로, EEPROM은 가짜 칩으로 대체한다.
//
// 빌드/실행 (예제 폴더에서, 아래 두 줄은 한 명령입니다):
//   gcc -Wall -Itest/host -Itest/host/shim -I. -Iutils -DEEPROM_WITH_RTOS -o test_console test/host/test_console.c
//       eeprom_cmds.c m95m04.c utils/cmdline.c utils/ustdlib.c   그리고 ./test_console 실행

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fake_m95m04.inc"
#include "cmdline.h"
#include "uartstdio.h"
#include "ustdlib.h"
#include "eeprom_cmds.h"

// ---- UART 대체 ---------------------------------------------------------------------------------------------------
static char outBuf[32768];
static size_t outLen;

void UARTprintf(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = uvsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0 && outLen + (size_t)n < sizeof(outBuf)) {
        memcpy(outBuf + outLen, tmp, (size_t)n);
        outLen += (size_t)n;
        outBuf[outLen] = '\0';
    }
}

int UARTgets(char *buf, uint32_t len) { (void)buf; (void)len; return 0; }

// uptime 명령(FreeRTOS 버전 전용)의 대역
int Cmd_uptime(int argc, char *argv[]) { (void)argc; (void)argv; UARTprintf("uptime-stub\n"); return 0; }

// ---- 헬퍼 -------------------------------------------------------------------------------------------------------
static int run(const char *line) {
    char cmd[128];
    strncpy(cmd, line, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    outLen = 0;
    outBuf[0] = '\0';
    return CmdLineProcess(cmd);
}

static int has(const char *needle) { return strstr(outBuf, needle) != NULL; }

static void show_on_failure(const char *needle) {
    if (!has(needle)) {
        printf("---- expected to contain: [%s]\n---- actual output:\n%s\n----\n", needle, outBuf);
    }
}
#define EXPECT(needle) do { show_on_failure(needle); assert(has(needle)); } while (0)
#define EXPECT_NOT(needle) do { if (has(needle)) { printf("unexpected: [%s]\n%s\n", needle, outBuf); } assert(!has(needle)); } while (0)

// ---- 테스트 -----------------------------------------------------------------------------------------------------
static void test_help_lists_all_commands(void) {
    chip_reset();
    assert(run("help") == 0);
    EXPECT("status");
    EXPECT("read");
    EXPECT("write");
    EXPECT("fill");
    EXPECT("test");
    EXPECT("uptime");
}

static void test_unknown_command(void) {
    chip_reset();
    assert(run("bogus") == CMDLINE_BAD_CMD);
}

static void test_status_fresh_and_protected(void) {
    chip_reset();
    run("status");
    EXPECT("Status register = 0x00");
    EXPECT("WIP=0 WEL=0 BP=0 SRWD=0");
    EXPECT("none");

    chip.bp = 3;
    run("status");
    EXPECT("BP=3");
    EXPECT("whole memory is write-protected");
    chip.bp = 1;
    run("status");
    EXPECT("upper 1/4");
    chip.bp = 2;
    run("status");
    EXPECT("upper 1/2");
}

static void test_status_warns_when_chip_does_not_answer(void) {
    chip_reset();
    chip.srForce = 0xFF;
    run("status");
    EXPECT("0xff");
    EXPECT("may not be answering");
}

static void test_write_and_read_back_text(void) {
    chip_reset();
    run("write 0x100 hello world");
    EXPECT("Wrote 11 bytes at 0x00100, verify OK");
    assert(memcmp(&chip.mem[0x100], "hello world", 11) == 0);

    run("read 0x100 16");
    EXPECT("00100: 68 65 6c 6c 6f 20 77 6f 72 6c 64 ff ff ff ff ff");
    EXPECT("|hello world.....|");
}

static void test_read_accepts_decimal_and_defaults_to_64_bytes(void) {
    chip_reset();
    memcpy(&chip.mem[256], "ABCD", 4);
    run("read 256 4");
    EXPECT("00100: 41 42 43 44");
    EXPECT("|ABCD|");

    run("read 0");
    EXPECT("00000:");
    EXPECT("00030:");          // 64바이트 = 4줄(0x00, 0x10, 0x20, 0x30)
    EXPECT_NOT("00040:");
}

static void test_usage_and_argument_errors(void) {
    chip_reset();
    run("read");
    EXPECT("Usage: read");
    run("read zzz");
    EXPECT("Usage: read");
    run("write 0x10");
    EXPECT("Usage: write");
    run("fill 0 10");
    EXPECT("Usage: fill");
    run("test 0");
    EXPECT("Usage: test");
    run("read 0x10 0");
    EXPECT("at least 1");
    assert(chip.writeTransactions == 0);
}

static void test_range_errors(void) {
    chip_reset();
    run("read 0x7fff8 16");
    EXPECT("out of range");
    run("read 0x80000 1");
    EXPECT("out of range");
    run("write 0x80000 a");
    EXPECT("out of range");
    run("test 0x7ffff 2");
    EXPECT("out of range");
    run("fill 0x7fffe 4 0");
    EXPECT("out of range");
    assert(chip.writeTransactions == 0);
}

static void test_write_to_protected_chip_reports_verify_failure_and_hint(void) {
    chip_reset();
    chip.bp = 3;
    run("write 0x100 text");
    EXPECT("Verify FAILED at 0x00100");
    EXPECT("block-protect bits are set");
}

static void test_write_timeout_message(void) {
    chip_reset();
    chip.neverReady = true;
    run("write 0 a");
    EXPECT("did not finish in time");
}

static void test_fill_across_a_page_boundary(void) {
    chip_reset();
    run("fill 0x1f0 40 0xAA");
    EXPECT("Filling 40 bytes at 0x001f0 with 0xaa");
    EXPECT("verify OK");
    assert(chip.mem[0x1ef] == 0xFF && chip.mem[0x1f0] == 0xAA && chip.mem[0x217] == 0xAA && chip.mem[0x218] == 0xFF);
    run("fill 0 4 300");
    EXPECT("value 0..255");
}

static void test_test_command_passes_across_pages_and_detects_protection(void) {
    chip_reset();
    run("test 0x1ff 100");
    EXPECT("Test PASS: 100 bytes at 0x001ff (2 page(s))");

    chip_reset();
    chip.bp = 3;
    run("test 0x10 64");
    EXPECT("Test FAIL");
    EXPECT("block-protect bits are set");
}

static void test_test_command_longer_than_a_chunk_and_pattern_varies(void) {
    chip_reset();
    run("test 0 1500");
    EXPECT("Test PASS: 1500 bytes at 0x00000 (3 page(s))");
    // 패턴이 주기적으로 같은 값만 반복되지 않는지(기본적인 건전성): 서로 다른 값이 충분히 있어야 한다
    int seen[256] = {0}, distinct = 0;
    for (int i = 0; i < 1500; i++) { if (!seen[chip.mem[i]]) { seen[chip.mem[i]] = 1; distinct++; } }
    assert(distinct > 100);
}

static void test_banner_mentions_variant(void) {
    chip_reset();
    outLen = 0; outBuf[0] = '\0';
    EepromConsole_banner("FreeRTOS");
    EXPECT("Serial EEPROM (M95M04)");
    EXPECT("FreeRTOS version");
    EXPECT("EEPROM answered, status register = 0x00");

    chip.srForce = 0xFF;    // 응답 없는 칩
    outLen = 0; outBuf[0] = '\0';
    EepromConsole_banner("DriverLib");
    EXPECT("EEPROM did not answer");
}

int main(void) {
    test_help_lists_all_commands();
    test_unknown_command();
    test_status_fresh_and_protected();
    test_status_warns_when_chip_does_not_answer();
    test_write_and_read_back_text();
    test_read_accepts_decimal_and_defaults_to_64_bytes();
    test_usage_and_argument_errors();
    test_range_errors();
    test_write_to_protected_chip_reports_verify_failure_and_hint();
    test_write_timeout_message();
    test_fill_across_a_page_boundary();
    test_test_command_passes_across_pages_and_detects_protection();
    test_test_command_longer_than_a_chunk_and_pattern_varies();
    test_banner_mentions_variant();
    printf("test_console: all tests passed\n");
    return 0;
}
