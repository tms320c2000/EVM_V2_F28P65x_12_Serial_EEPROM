// 가짜 M95M04 칩 모델 (test_m95m04.c, test_console.c가 #include 한다).
// SPI 바이트 스트림을 해석한다: 명령 WREN/WRITE/READ/RDSR, 3바이트 주소, 페이지 512바이트(경계에서 되감김),
// WEL(쓰기 허용 래치), WIP(쓰기 진행 중), 블록 보호(BP) 비트.

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "m95m04.h"

// ---- 가짜 칩 ---------------------------------------------------------------------------------------------------
enum { ST_IDLE, ST_ADDR, ST_READ, ST_WRITE, ST_RDSR, ST_IGNORE };

static struct {
    m95_byte mem[M95M04_SIZE_BYTES];
    bool selected;
    int state;
    m95_byte opcode;
    int addrCount;
    uint32_t addr;
    uint32_t pageBase;
    uint32_t pageOffs;
    m95_byte newData[M95M04_PAGE_SIZE];
    uint32_t newOffs[M95M04_PAGE_SIZE];
    uint32_t newCount;
    bool wel;
    bool wip;
    int wipPolls;          // 쓰기 후 WIP가 유지되는 대기 단계 수
    int wipPollsLeft;
    bool neverReady;
    m95_byte bp;           // 블록 보호 비트(0이 아니면 쓰기 무시)
    int srForce;           // 0 이상이면 RDSR이 항상 이 값을 돌려준다(응답 없는 칩 흉내)
    // 기록
    int writeTransactions;
    uint32_t writeAddr[64];
    uint32_t writeLen[64];
    int wrenCount;
    int waitSteps;
} chip;

static void chip_reset(void) {
    memset(chip.mem, 0xFF, sizeof(chip.mem));   // 새 EEPROM은 0xFF
    chip.selected = false;
    chip.wel = false;
    chip.wip = false;
    chip.wipPolls = 3;
    chip.wipPollsLeft = 0;
    chip.neverReady = false;
    chip.bp = 0;
    chip.srForce = -1;
    chip.writeTransactions = 0;
    chip.wrenCount = 0;
    chip.waitSteps = 0;
}

static m95_byte status_reg(void) {
    if (chip.srForce >= 0) { return (m95_byte)chip.srForce; }
    return (m95_byte)((chip.wip ? 0x01 : 0x00) | (chip.wel ? 0x02 : 0x00) | (chip.bp << 2));
}

static int halInitCalls;

void M95M04_hal_init(void) {
    halInitCalls++;
}

void M95M04_hal_select(void) {
    assert(!chip.selected);
    chip.selected = true;
    chip.state = ST_IDLE;
    chip.addrCount = 0;
    chip.addr = 0;
    chip.newCount = 0;
}

void M95M04_hal_deselect(void) {
    assert(chip.selected);
    chip.selected = false;
    if (chip.opcode == 0x06 && chip.state != ST_IDLE) {
        // WREN은 명령 바이트 직후 CS가 올라가야 적용된다
    }
    if (chip.state == ST_IDLE && chip.opcode == 0x06) {
        chip.wel = true;
        chip.wrenCount++;
    } else if (chip.state == ST_WRITE && chip.opcode == 0x02) {
        if (chip.wel && chip.bp == 0 && !chip.wip) {
            for (uint32_t i = 0; i < chip.newCount; i++) {
                chip.mem[chip.pageBase + chip.newOffs[i]] = chip.newData[i];
            }
            chip.wip = true;
            chip.wipPollsLeft = chip.neverReady ? 1000000 : chip.wipPolls;
        }
        chip.wel = false;   // 쓰기 사이클이 끝나면 WEL은 자동으로 내려간다
        if (chip.writeTransactions < 64) {
            chip.writeAddr[chip.writeTransactions] = chip.addr;
            chip.writeLen[chip.writeTransactions] = chip.newCount;
        }
        chip.writeTransactions++;
    }
}

m95_byte M95M04_hal_transfer(m95_byte out) {
    assert(chip.selected);
    switch (chip.state) {
    case ST_IDLE:
        chip.opcode = out;
        if (out == 0x03 || out == 0x02) { chip.state = ST_ADDR; }
        else if (out == 0x05) { chip.state = ST_RDSR; }
        else if (out == 0x06) { /* CS 상승에서 처리 */ }
        else { chip.state = ST_IGNORE; }
        return 0xFF;
    case ST_ADDR:
        chip.addr = (chip.addr << 8) | out;
        if (++chip.addrCount == 3) {
            chip.addr &= 0x7FFFF;   // 19비트
            if (chip.opcode == 0x03) {
                chip.state = ST_READ;
            } else {
                chip.state = ST_WRITE;
                chip.pageBase = chip.addr & ~(uint32_t)(M95M04_PAGE_SIZE - 1);
                chip.pageOffs = chip.addr & (M95M04_PAGE_SIZE - 1);
            }
        }
        return 0xFF;
    case ST_READ: {
        m95_byte v = chip.mem[chip.addr];
        chip.addr = (chip.addr + 1) % M95M04_SIZE_BYTES;
        return v;
    }
    case ST_WRITE:
        if (chip.newCount < M95M04_PAGE_SIZE) {
            chip.newData[chip.newCount] = out;
            chip.newOffs[chip.newCount] = chip.pageOffs;
            chip.newCount++;
            chip.pageOffs = (chip.pageOffs + 1) & (M95M04_PAGE_SIZE - 1);   // 페이지 끝에서 앞으로 되감김
        }
        return 0xFF;
    case ST_RDSR:
        return status_reg();
    default:
        return 0xFF;
    }
}

uint32_t M95M04_hal_waitStepUs(void) {
    chip.waitSteps++;
    if (chip.wip && chip.wipPollsLeft > 0 && --chip.wipPollsLeft == 0) {
        chip.wip = false;
    }
    return 100;
}

