//#############################################################################
//
// FILE:   m95m04.c
//
// TITLE:  ST M95M04-DR (SPI EEPROM) 프로토콜 계층 — 세 버전에서 같은 파일
//
//#############################################################################

#include "m95m04.h"

// M95M04 명령 코드(Instruction)
#define OP_WREN             0x06U       // 쓰기 허용(Write Enable)
#define OP_RDSR             0x05U       // 상태 레지스터 읽기(Read Status Register)
#define OP_READ             0x03U       // 메모리 읽기
#define OP_WRITE            0x02U       // 메모리 쓰기(페이지 단위)

#define WRITE_TIMEOUT_US    20000UL     // 쓰기 사이클 최대 5ms의 4배 여유
#define VERIFY_CHUNK        64U         // 검증할 때 한 번에 읽어 비교하는 바이트 수

static m95_byte s_fillBuf[M95M04_PAGE_SIZE];    // fill용 한 페이지 크기의 임시 버퍼

// 24비트 주소(상위 바이트부터)를 SPI로 보낸다
static void sendAddress(uint32_t addr)
{
    (void)M95M04_hal_transfer((m95_byte)((addr >> 16) & 0xFFU));
    (void)M95M04_hal_transfer((m95_byte)((addr >> 8) & 0xFFU));
    (void)M95M04_hal_transfer((m95_byte)(addr & 0xFFU));
}

// [addr, addr+len) 범위가 칩 용량 안에 있는지 검사한다(덧셈 오버플로를 피하려고 뺄셈으로 비교)
static int rangeOk(uint32_t addr, uint32_t len)
{
    return (addr < M95M04_SIZE_BYTES) && (len <= (M95M04_SIZE_BYTES - addr));
}

void M95M04_init(void)
{
    M95M04_hal_init();
}

// 상태 레지스터(RDSR)를 읽는다
m95_status_t M95M04_readStatus(m95_byte *sr)
{
    if (sr == 0)
    {
        return M95_ERR_PARAM;
    }
    M95M04_hal_select();
    (void)M95M04_hal_transfer(OP_RDSR);
    *sr = M95M04_hal_transfer(0x00U);
    M95M04_hal_deselect();
    return M95_OK;
}

// WIP가 내려갈 때까지 기다린다. 한 단계마다 HAL이 쉬는 시간을 합산해 한계를 넘으면 타임아웃.
static m95_status_t waitWriteDone(void)
{
    uint32_t waited = 0UL;
    m95_byte sr;

    for (;;)
    {
        (void)M95M04_readStatus(&sr);
        if ((sr & M95M04_SR_WIP) == 0U)
        {
            return M95_OK;
        }
        if (waited >= WRITE_TIMEOUT_US)
        {
            return M95_ERR_TIMEOUT;
        }
        waited += M95M04_hal_waitStepUs();
    }
}

// 연속 읽기: 주소를 한 번 보내면 이후 주소가 자동으로 증가한다
m95_status_t M95M04_read(uint32_t addr, m95_byte *buf, uint32_t len)
{
    uint32_t i;

    if (len == 0UL)
    {
        return M95_OK;
    }
    if (buf == 0)
    {
        return M95_ERR_PARAM;
    }
    if (!rangeOk(addr, len))
    {
        return M95_ERR_RANGE;
    }

    M95M04_hal_select();
    (void)M95M04_hal_transfer(OP_READ);
    sendAddress(addr);
    for (i = 0UL; i < len; i++)
    {
        buf[i] = M95M04_hal_transfer(0x00U);    // 주소가 자동으로 증가하며 계속 나온다
    }
    M95M04_hal_deselect();
    return M95_OK;
}

// 쓰기: 페이지 경계에서 나눠 쓰고, 조각마다 쓰기 완료(WIP 해제)를 기다린다
m95_status_t M95M04_write(uint32_t addr, const m95_byte *buf, uint32_t len)
{
    uint32_t i, chunk;
    m95_status_t st;

    if (len == 0UL)
    {
        return M95_OK;
    }
    if (buf == 0)
    {
        return M95_ERR_PARAM;
    }
    if (!rangeOk(addr, len))
    {
        return M95_ERR_RANGE;   // 일부만 쓰지 않도록 시작 전에 전체를 검사한다
    }

    while (len > 0UL)
    {
        //
        // 한 번의 WRITE는 페이지(512바이트) 안에서만 유효하다. 경계를 넘는 바이트는 같은 페이지의 앞으로
        // 되감겨 이미 있던 데이터를 덮어쓰므로, 페이지 끝에서 잘라 다음 페이지는 새 WRITE로 보낸다.
        //
        chunk = M95M04_PAGE_SIZE - (addr & (M95M04_PAGE_SIZE - 1U));
        if (chunk > len)
        {
            chunk = len;
        }

        M95M04_hal_select();
        (void)M95M04_hal_transfer(OP_WREN);     // 쓰기 허용 래치(CS가 올라갈 때 적용)
        M95M04_hal_deselect();

        M95M04_hal_select();
        (void)M95M04_hal_transfer(OP_WRITE);
        sendAddress(addr);
        for (i = 0UL; i < chunk; i++)
        {
            (void)M95M04_hal_transfer(buf[i]);
        }
        M95M04_hal_deselect();                  // CS가 올라가면 내부 쓰기 사이클 시작

        st = waitWriteDone();
        if (st != M95_OK)
        {
            return st;
        }

        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return M95_OK;
}

// 채우기: 같은 값 한 페이지를 만들어 두고 M95M04_write로 반복해서 쓴다
m95_status_t M95M04_fill(uint32_t addr, m95_byte value, uint32_t len)
{
    uint32_t i, chunk;
    m95_status_t st;

    if (len == 0UL)
    {
        return M95_OK;
    }
    if (!rangeOk(addr, len))
    {
        return M95_ERR_RANGE;
    }

    for (i = 0U; i < M95M04_PAGE_SIZE; i++)
    {
        s_fillBuf[i] = value;
    }

    while (len > 0UL)
    {
        chunk = (len > M95M04_PAGE_SIZE) ? M95M04_PAGE_SIZE : len;
        st = M95M04_write(addr, s_fillBuf, chunk);
        if (st != M95_OK)
        {
            return st;
        }
        addr += chunk;
        len -= chunk;
    }
    return M95_OK;
}

// 검증: 칩에서 읽은 값을 expected와 비교한다. 다르면 첫 불일치 위치를 *firstMismatch에 돌려준다
m95_status_t M95M04_verify(uint32_t addr, const m95_byte *expected, uint32_t len, uint32_t *firstMismatch)
{
    m95_byte buf[VERIFY_CHUNK];
    uint32_t done = 0UL, chunk, i;
    m95_status_t st;

    if (len == 0UL)
    {
        return M95_OK;
    }
    if (expected == 0)
    {
        return M95_ERR_PARAM;
    }
    if (!rangeOk(addr, len))
    {
        return M95_ERR_RANGE;
    }

    while (done < len)
    {
        chunk = ((len - done) > VERIFY_CHUNK) ? VERIFY_CHUNK : (len - done);
        st = M95M04_read(addr + done, buf, chunk);
        if (st != M95_OK)
        {
            return st;
        }
        for (i = 0UL; i < chunk; i++)
        {
            if (buf[i] != expected[done + i])
            {
                if (firstMismatch != 0)
                {
                    *firstMismatch = done + i;
                }
                return M95_ERR_VERIFY;
            }
        }
        done += chunk;
    }
    return M95_OK;
}
