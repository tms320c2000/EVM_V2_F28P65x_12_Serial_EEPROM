//#############################################################################
//
// FILE:   m95m04_hal.c  (Bitfield 버전)
//
// TITLE:  M95M04 SPI HAL — F28P65x SPI-A 비트필드 레지스터 (DriverLib 없음)
//
// 배선 (개발보드 (12) 회로 핀-헤더 CN9412 <-> F28P65x 모듈 A-Side CN9000):
//   - DI  (MOSI): GPIO16 (SPIA_PICO) -> A-Side 97번
//   - DO  (MISO): GPIO17 (SPIA_POCI) -> A-Side 99번
//   - CLK (SCLK): GPIO18 (SPIA_CLK)  -> A-Side 101번
//   - CS2 (EEPROM): GPIO19 (GPIO 출력, Low 활성, 소프트웨어 제어) -> A-Side 103번
//   - CS1(시리얼 FLASH)은 연결하지 않는다: 보드의 4.7kΩ 풀업으로 High(비선택)가 유지된다.
//
// SPI 설정: 마스터, 8비트, 표준 SPI 모드 0 (CLKPOLARITY=0, CLK_PHASE=1), BRR=9 -> 50MHz/(9+1) = 5MHz
//   (칩 최대 10MHz). 송신 버퍼(SPITXBUF)는 좌측 정렬(<<8), 수신 버퍼(SPIRXBUF)는 우측 정렬(& 0xFF).
//   FIFO를 쓰지 않고 SPISTS.INT_FLAG(수신 완료)를 폴링한다.
//
//#############################################################################

#include "f28x_project.h"
#include "m95m04.h"

#define EE_SPI_BRR          9U          // LSPCLK(50MHz) / (BRR + 1) = 5 MHz
#define EE_WAIT_STEP_US     100U        // 쓰기 완료를 기다리는 한 단계의 쉬는 시간(µs)

// SPI-A와 CS2(GPIO19)를 초기화한다
void M95M04_hal_init(void)
{
    // SPI-A 클럭은 InitSysCtrl()이 켜 둔다(CpuSysRegs.PCLKCR8.bit.SPI_A)

    // DI/DO/CLK: GPIO16/17/18 -> SPI-A (MUX 1)
    GPIO_SetupPinMux(16, GPIO_MUX_CPU1, 1);
    GPIO_SetupPinMux(17, GPIO_MUX_CPU1, 1);
    GPIO_SetupPinMux(18, GPIO_MUX_CPU1, 1);
    GPIO_SetupPinOptions(16, GPIO_OUTPUT, GPIO_PUSHPULL | GPIO_ASYNC);
    GPIO_SetupPinOptions(17, GPIO_INPUT,  GPIO_PULLUP   | GPIO_ASYNC);
    GPIO_SetupPinOptions(18, GPIO_OUTPUT, GPIO_PUSHPULL | GPIO_ASYNC);

    // CS2: GPIO19를 소프트웨어 제어 출력으로 (High로 먼저 만든 뒤 출력으로 바꿔 글리치를 막는다)
    GPIO_SetupPinMux(19, GPIO_MUX_CPU1, 0);
    GpioDataRegs.GPASET.bit.GPIO19 = 1U;
    GPIO_SetupPinOptions(19, GPIO_OUTPUT, GPIO_PUSHPULL);

    SpiaRegs.SPICCR.bit.SPISWRESET = 0U;            // 설정을 바꾸는 동안 리셋 상태로 둔다
    SpiaRegs.SPICCR.bit.SPICHAR = 7U;               // 8비트 문자
    SpiaRegs.SPICCR.bit.CLKPOLARITY = 0U;           // 클럭 idle Low
    SpiaRegs.SPICCR.bit.SPILBK = 0U;                // 루프백 끔
    SpiaRegs.SPICTL.bit.CLK_PHASE = 1U;             // CPOL=0 + 이 값 1 => 표준 SPI 모드 0 (TI 표기 POL0PHA1)
    SpiaRegs.SPICTL.bit.CONTROLLER_PERIPHERAL = 1U; // 마스터(Controller)
    SpiaRegs.SPICTL.bit.TALK = 1U;                  // 송신 활성화
    SpiaRegs.SPICTL.bit.SPIINTENA = 0U;             // 인터럽트 없이 폴링
    SpiaRegs.SPIBRR.bit.SPI_BIT_RATE = EE_SPI_BRR;
    SpiaRegs.SPIFFTX.bit.SPIFFENA = 0U;             // FIFO를 쓰지 않는다(INT_FLAG 폴링과 맞추기 위해)
    SpiaRegs.SPIPRI.bit.FREE = 1U;                  // 디버거 정지 중에도 계속 동작
    SpiaRegs.SPICCR.bit.SPISWRESET = 1U;            // 동작 시작
}

// CS2를 Low로 내려 EEPROM을 선택한다
void M95M04_hal_select(void)
{
    GpioDataRegs.GPACLEAR.bit.GPIO19 = 1U;
}

// CS2를 High로 올려 EEPROM 선택을 해제한다
void M95M04_hal_deselect(void)
{
    GpioDataRegs.GPASET.bit.GPIO19 = 1U;
}

// SPI로 한 바이트를 보내고, 같은 클럭에 들어온 바이트를 돌려준다
m95_byte M95M04_hal_transfer(m95_byte out)
{
    SpiaRegs.SPITXBUF = ((Uint16)out) << 8;         // 좌측 정렬

    while(SpiaRegs.SPISTS.bit.INT_FLAG != 1U)       // 클럭이 다 나가 수신 완료 플래그가 설 때까지
    {
    }
    return (m95_byte)(SpiaRegs.SPIRXBUF & 0xFFU);   // 우측 정렬
}

// 쓰기 완료 대기의 한 단계: EE_WAIT_STEP_US만큼 쉬고, 쉰 시간(µs)을 돌려준다
uint32_t M95M04_hal_waitStepUs(void)
{
    DELAY_US(EE_WAIT_STEP_US);
    return EE_WAIT_STEP_US;
}
