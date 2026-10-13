//#############################################################################
//
// FILE:   eeprom_cmds.c
//
// TITLE:  EEPROM 대화형 콘솔 명령어 — 세 버전에서 같은 파일
//
// 명령: help, status, read, write, fill, test  (FreeRTOS 버전은 EEPROM_WITH_RTOS를 정의해 uptime 추가)
//
//#############################################################################

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "m95m04.h"
#include "cmdline.h"
#include "uartstdio.h"
#include "ustdlib.h"
#include "eeprom_cmds.h"

#define CMD_BUF_SIZE        64          // 명령어 한 줄 입력 버퍼 크기(글자 수)
#define READ_DEFAULT_LEN    64UL        // read 명령의 길이 기본값
#define READ_MAX_LEN        1024UL      // read 명령 한 번에 읽을 수 있는 최대 길이
#define CHUNK_LEN           64U         // fill/test에서 한 번에 처리하는 바이트 수
#define DUMP_BYTES_PER_LINE 16U         // hex 덤프 한 줄에 출력하는 바이트 수

//
// 명령 함수 선언
//
int Cmd_help(int argc, char *argv[]);
int Cmd_status(int argc, char *argv[]);
int Cmd_read(int argc, char *argv[]);
int Cmd_write(int argc, char *argv[]);
int Cmd_fill(int argc, char *argv[]);
int Cmd_test(int argc, char *argv[]);
#ifdef EEPROM_WITH_RTOS
extern int Cmd_uptime(int argc, char *argv[]);
#endif

//
// 명령어 표 (cmdline.c가 이 표에서 입력한 명령을 찾아 실행한다)
//
tCmdLineEntry g_psCmdTable[] =
{
    { "help",   Cmd_help,   " : Display list of commands" },
    { "h",      Cmd_help,   "    : alias for help" },
    { "?",      Cmd_help,   "    : alias for help" },
    { "status", Cmd_status, ": Show the status register (WIP/WEL/block protect)" },
    { "read",   Cmd_read,   "  : read <addr> [len]  - hex dump (default 64 bytes)" },
    { "write",  Cmd_write,  " : write <addr> <text>  - write text, then read back and verify" },
    { "fill",   Cmd_fill,   "  : fill <addr> <len> <value>  - fill a range with one byte value" },
    { "test",   Cmd_test,   "  : test <addr> <len>  - pattern write/read/verify (OVERWRITES the range)" },
#ifdef EEPROM_WITH_RTOS
    { "uptime", Cmd_uptime, ": Show RTOS tick time and heartbeat counter" },
#endif
    { 0, 0, 0 }
};

//*****************************************************************************
// 보조 함수
//*****************************************************************************

// 상태 코드를 사람이 읽을 수 있는 문자열로 바꾼다
static const char *StringFromStatus(m95_status_t st)
{
    switch(st)
    {
    case M95_OK:          return "OK";
    case M95_ERR_RANGE:   return "address/length out of range (valid 0x00000..0x7FFFF)";
    case M95_ERR_TIMEOUT: return "write did not finish in time (is the chip connected? check wiring)";
    case M95_ERR_VERIFY:  return "verify mismatch";
    case M95_ERR_PARAM:   return "invalid argument";
    default:              return "unknown error";
    }
}

// "0x1F", "31" 같은 숫자 전체를 읽는다. 일부만 숫자거나 비어 있으면 false.
static bool ParseNumber(const char *s, uint32_t *out)
{
    const char *end;
    unsigned long v;

    if((s == 0) || (*s == '\0'))
    {
        return false;
    }
    v = ustrtoul(s, &end, 0);
    if(*end != '\0')
    {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

// 범위 [addr, addr+len)가 칩 안에 있는지
static bool RangeInChip(uint32_t addr, uint32_t len)
{
    return (addr < M95M04_SIZE_BYTES) && (len <= (M95M04_SIZE_BYTES - addr));
}

// 테스트 패턴: 위치와 시작 주소(salt)로 정해지는 바이트. 같은 값이 주기적으로 반복되지 않게 상위 비트를 섞는다.
static m95_byte PatternByte(uint32_t index, uint32_t salt)
{
    return (m95_byte)(((index * 31UL) + (index >> 8) + salt) & 0xFFUL);
}

// "<명령>: <오류 내용>" 형태로 오류를 출력한다
static void PrintError(const char *what, m95_status_t st)
{
    UARTprintf("%s: %s\n", what, StringFromStatus(st));
}

// 쓰기 직후 검증이 실패했을 때 흔한 원인(블록 보호)을 알려 준다
static void PrintProtectHint(void)
{
    m95_byte sr;

    if((M95M04_readStatus(&sr) == M95_OK) && ((sr & M95M04_SR_BP_MASK) != 0U))
    {
        UARTprintf("Hint: block-protect bits are set (SR=0x%02x); writes to the protected area are ignored. See 'status'.\n",
                   (uint32_t)sr);
    }
}

//*****************************************************************************
// Cmd_help - 명령어 목록 출력
//*****************************************************************************
int Cmd_help(int argc, char *argv[])
{
    tCmdLineEntry *pEntry = &g_psCmdTable[0];
    size_t pad;

    UARTprintf("\nAvailable Commands:\n");
    UARTprintf("------------------------------------------------------\n");
    while(pEntry->pcCmd)
    {
        // 이 경량 printf는 '-' 플래그(%-7s)를 지원하지 않아 ERROR로 출력된다.
        // 그래서 명령 이름을 7칸으로 맞추는 공백은 직접 출력한다.
        UARTprintf("  %s", pEntry->pcCmd);
        for(pad = strlen(pEntry->pcCmd); pad < 7U; pad++)
        {
            UARTprintf(" ");
        }
        UARTprintf("%s\n", pEntry->pcHelp);
        pEntry++;
    }
    UARTprintf("------------------------------------------------------\n");
    UARTprintf("Addresses accept decimal or 0x hex. Chip: 0x00000..0x7FFFF (512 KB), 512-byte pages.\n");
    return 0;
}

//*****************************************************************************
// Cmd_status - 상태 레지스터(RDSR) 출력
//*****************************************************************************
int Cmd_status(int argc, char *argv[])
{
    m95_byte sr;
    m95_status_t st = M95M04_readStatus(&sr);
    uint32_t bp;

    if(st != M95_OK)
    {
        PrintError("status", st);
        return 0;
    }

    bp = (uint32_t)((sr & M95M04_SR_BP_MASK) >> 2);
    UARTprintf("Status register = 0x%02x  (WIP=%u WEL=%u BP=%u SRWD=%u)\n",
               (uint32_t)sr,
               (uint32_t)((sr & M95M04_SR_WIP) ? 1 : 0),
               (uint32_t)((sr & M95M04_SR_WEL) ? 1 : 0),
               bp,
               (uint32_t)((sr & M95M04_SR_SRWD) ? 1 : 0));

    switch(bp)
    {
    case 0:  UARTprintf("Block protect: none (whole chip writable)\n"); break;
    case 1:  UARTprintf("Block protect: upper 1/4 (0x60000..0x7FFFF) is write-protected\n"); break;
    case 2:  UARTprintf("Block protect: upper 1/2 (0x40000..0x7FFFF) is write-protected\n"); break;
    default: UARTprintf("Block protect: whole memory is write-protected\n"); break;
    }

    // 0xFF만 읽히면 칩이 응답하지 않는 것일 수 있다(MISO가 풀업)
    if(sr == 0xFFU)
    {
        UARTprintf("Warning: status is 0xFF - the chip may not be answering (check DI/DO/CLK/CS2 wiring).\n");
    }
    return 0;
}

//*****************************************************************************
// Cmd_read - read <addr> [len]  : 16바이트씩 hex + ASCII로 출력
//*****************************************************************************
int Cmd_read(int argc, char *argv[])
{
    uint32_t addr, len = READ_DEFAULT_LEN, done = 0UL;
    m95_byte line[DUMP_BYTES_PER_LINE];
    m95_status_t st;
    uint32_t i, n;

    if((argc < 2) || !ParseNumber(argv[1], &addr) || ((argc >= 3) && !ParseNumber(argv[2], &len)))
    {
        UARTprintf("Usage: read <addr> [len]\n");
        return 0;
    }
    if(len == 0UL)
    {
        UARTprintf("length must be at least 1\n");
        return 0;
    }
    if(len > READ_MAX_LEN)
    {
        UARTprintf("length limited to %u bytes per command\n", (uint32_t)READ_MAX_LEN);
        len = READ_MAX_LEN;
    }
    if(!RangeInChip(addr, len))
    {
        PrintError("read", M95_ERR_RANGE);
        return 0;
    }

    while(done < len)
    {
        n = ((len - done) > DUMP_BYTES_PER_LINE) ? DUMP_BYTES_PER_LINE : (len - done);
        st = M95M04_read(addr + done, line, n);
        if(st != M95_OK)
        {
            PrintError("read", st);
            return 0;
        }

        UARTprintf("%05x: ", addr + done);
        for(i = 0U; i < DUMP_BYTES_PER_LINE; i++)
        {
            if(i < n)
            {
                UARTprintf("%02x ", (uint32_t)line[i]);
            }
            else
            {
                UARTprintf("   ");
            }
        }
        UARTprintf(" |");
        for(i = 0U; i < n; i++)
        {
            // 이 경량 printf는 %c도 32비트(unsigned long)로 읽는다. 16비트 char를 그대로 넘기면 스택의 이전 값이
            // 섞여 글자가 깨지므로, %x/%u와 같이 uint32_t로 넘긴다.
            UARTprintf("%c", (uint32_t)(((line[i] >= 0x20U) && (line[i] < 0x7FU)) ? line[i] : (m95_byte)'.'));
        }
        UARTprintf("|\n");
        done += n;
    }
    return 0;
}

//*****************************************************************************
// Cmd_write - write <addr> <text...> : 공백으로 이어 붙인 글자를 쓰고 읽어서 검증
//*****************************************************************************
int Cmd_write(int argc, char *argv[])
{
    uint32_t addr, pos = 0UL, len = 0UL;
    m95_byte data[CMD_BUF_SIZE];
    m95_status_t st;
    int i;
    const char *p;

    if((argc < 3) || !ParseNumber(argv[1], &addr))
    {
        UARTprintf("Usage: write <addr> <text...>\n");
        return 0;
    }

    for(i = 2; i < argc; i++)
    {
        for(p = argv[i]; *p != '\0'; p++)
        {
            data[len++] = (m95_byte)*p;
        }
        if(i < (argc - 1))
        {
            data[len++] = (m95_byte)' ';
        }
    }

    st = M95M04_write(addr, data, len);
    if(st != M95_OK)
    {
        PrintError("write", st);
        return 0;
    }
    st = M95M04_verify(addr, data, len, &pos);
    if(st != M95_OK)
    {
        UARTprintf("Verify FAILED at 0x%05x (offset %u)\n", addr + pos, pos);
        PrintProtectHint();
        return 0;
    }
    UARTprintf("Wrote %u bytes at 0x%05x, verify OK\n", len, addr);
    return 0;
}

//*****************************************************************************
// Cmd_fill - fill <addr> <len> <value> : 범위를 한 바이트 값으로 채우고 읽어서 검증
//*****************************************************************************
int Cmd_fill(int argc, char *argv[])
{
    uint32_t addr, len, value, done = 0UL, n, i, pos = 0UL;
    m95_byte expect[CHUNK_LEN];
    m95_status_t st;

    if((argc < 4) || !ParseNumber(argv[1], &addr) || !ParseNumber(argv[2], &len) || !ParseNumber(argv[3], &value))
    {
        UARTprintf("Usage: fill <addr> <len> <value 0..255>\n");
        return 0;
    }
    if((len == 0UL) || (value > 255UL))
    {
        UARTprintf("len must be >= 1 and value 0..255\n");
        return 0;
    }
    if(!RangeInChip(addr, len))
    {
        PrintError("fill", M95_ERR_RANGE);
        return 0;
    }

    UARTprintf("Filling %u bytes at 0x%05x with 0x%02x ...\n", len, addr, value);
    st = M95M04_fill(addr, (m95_byte)value, len);
    if(st != M95_OK)
    {
        PrintError("fill", st);
        return 0;
    }

    for(i = 0U; i < CHUNK_LEN; i++)
    {
        expect[i] = (m95_byte)value;
    }
    while(done < len)
    {
        n = ((len - done) > CHUNK_LEN) ? CHUNK_LEN : (len - done);
        st = M95M04_verify(addr + done, expect, n, &pos);
        if(st != M95_OK)
        {
            UARTprintf("Verify FAILED at 0x%05x\n", addr + done + pos);
            PrintProtectHint();
            return 0;
        }
        done += n;
    }
    UARTprintf("Fill done, verify OK\n");
    return 0;
}

//*****************************************************************************
// Cmd_test - test <addr> <len> : 패턴을 쓰고 읽어 비교 (범위를 덮어쓴다)
//*****************************************************************************
int Cmd_test(int argc, char *argv[])
{
    uint32_t addr, len, done = 0UL, n, i, pos = 0UL, pages;
    m95_byte buf[CHUNK_LEN];
    m95_status_t st;

    if((argc < 3) || !ParseNumber(argv[1], &addr) || !ParseNumber(argv[2], &len))
    {
        UARTprintf("Usage: test <addr> <len>   (overwrites 0x<addr>..<addr>+<len>-1)\n");
        return 0;
    }
    if(len == 0UL)
    {
        UARTprintf("len must be at least 1\n");
        return 0;
    }
    if(!RangeInChip(addr, len))
    {
        PrintError("test", M95_ERR_RANGE);
        return 0;
    }

    pages = ((addr + len - 1UL) >> 9) - (addr >> 9) + 1UL;
    UARTprintf("WARNING: overwriting 0x%05x..0x%05x (%u bytes, %u page(s))\n", addr, addr + len - 1UL, len, pages);

    while(done < len)
    {
        n = ((len - done) > CHUNK_LEN) ? CHUNK_LEN : (len - done);
        for(i = 0U; i < n; i++)
        {
            buf[i] = PatternByte(done + i, addr);
        }
        st = M95M04_write(addr + done, buf, n);
        if(st != M95_OK)
        {
            PrintError("test write", st);
            return 0;
        }
        done += n;
    }

    for(done = 0UL; done < len; done += n)
    {
        n = ((len - done) > CHUNK_LEN) ? CHUNK_LEN : (len - done);
        for(i = 0U; i < n; i++)
        {
            buf[i] = PatternByte(done + i, addr);
        }
        st = M95M04_verify(addr + done, buf, n, &pos);
        if(st != M95_OK)
        {
            UARTprintf("Test FAIL: first mismatch at 0x%05x\n", addr + done + pos);
            PrintProtectHint();
            return 0;
        }
    }
    UARTprintf("Test PASS: %u bytes at 0x%05x (%u page(s))\n", len, addr, pages);
    return 0;
}

//*****************************************************************************
// 환영 메시지와 명령어 입력 루프
//*****************************************************************************
void EepromConsole_banner(const char *variant)
{
    m95_byte sr;

    UARTprintf("\n\n");
    UARTprintf("======================================================\n");
    UARTprintf(" TMS320F28P65x Serial EEPROM (M95M04) SPI Console\n");
    UARTprintf(" %s version, EVM V2 Area (12) + Area (11) SCI\n", variant);
    UARTprintf("======================================================\n");

    // 칩이 응답하는지 바로 알 수 있게 상태 레지스터를 한 번 읽어 보여 준다
    if((M95M04_readStatus(&sr) == M95_OK) && (sr != 0xFFU))
    {
        UARTprintf("EEPROM answered, status register = 0x%02x\n", (uint32_t)sr);
    }
    else
    {
        UARTprintf("WARNING: EEPROM did not answer (status 0xFF). Check DI/DO/CLK/CS2 wiring.\n");
    }
    UARTprintf("Type 'help' or '?' for command list.\n");
}

void EepromConsole_run(void)
{
    char cmd[CMD_BUF_SIZE];
    int nStatus;

    for(;;)
    {
        UARTprintf("\neeprom> ");
        UARTgets(cmd, sizeof(cmd));

        if(cmd[0] == '\0')
        {
            continue;
        }

        nStatus = CmdLineProcess(cmd);
        if(nStatus == CMDLINE_BAD_CMD)
        {
            UARTprintf("Bad command! Type 'help' for available commands.\n");
        }
        else if(nStatus == CMDLINE_TOO_MANY_ARGS)
        {
            UARTprintf("Too many arguments for command processor!\n");
        }
    }
}
