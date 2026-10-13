//#############################################################################
//
// FILE:   12_Serial_EEPROM_Bitfield.c
//
// TITLE:  시리얼 EEPROM(M95M04, SPI) 대화형 콘솔 — TMS320F28P65x 비트필드 레지스터 버전
//
// 대상 하드웨어: SyncWorks TMS320F28X EVM V2 + TMS320F28P650DK9 / F28P659DK8-Q1
//
// 하드웨어 연결:
//   1) (12) 시리얼 FLASH / EEPROM 회로 (헤더 CN9412) <-> A-Side CN9000
//      - DI  (MOSI): A-Side 97번  (GPIO16, SPIA_PICO)
//      - DO  (MISO): A-Side 99번  (GPIO17, SPIA_POCI)
//      - CLK (SCLK): A-Side 101번 (GPIO18, SPIA_CLK)
//      - CS2 (EEPROM): A-Side 103번 (GPIO19, 일반 GPIO 출력, Low 활성)
//      (CS1 = 시리얼 FLASH는 연결하지 않는다 - 보드 풀업으로 비선택 상태 유지)
//   2) (11) 2채널 SCI-to-USB 회로 <-> A-Side CN9000 (11_SCI_to_USB 예제와 같은 핀)
//      - RX1 : A-Side 17번 (GPIO43, SCIA_RX)
//      - TX1 : A-Side 15번 (GPIO42, SCIA_TX)
//
// 터미널 설정: 115200 보레이트, 8N1
//
// 이 버전은 DriverLib을 전혀 쓰지 않고 TI의 Bit-Field 레지스터 구조체(SpiaRegs, SciaRegs, GpioDataRegs 등)를
// 직접 조작합니다. m95m04.c와 eeprom_cmds.c는 DriverLib / FreeRTOS 버전과 같은 파일입니다.
//
//#############################################################################

#include <stdint.h>
#include <stdbool.h>

#include "f28x_project.h"       // TI 제공 칩-지원 헤더 통합 Include 용 헤더파일 (bit-field)
#include "m95m04.h"
#include "uartstdio.h"
#include "eeprom_cmds.h"

#define SYSCLK_HZ           200000000UL                 // SYSCLK = 200MHz (InitSysCtrl이 PLL로 설정)
#define LSPCLK_HZ           (SYSCLK_HZ / 4UL)           // LSPCLK = SYSCLK/4 = 50MHz (InitSysCtrl 기본 분주비)
#define CONSOLE_BAUDRATE    115200UL

//*****************************************************************************
// ConfigureUART - SCI-A(GPIO43 RX, GPIO42 TX)를 115200 8N1 콘솔로 설정
//   GPIO42/43의 SCIA 기능은 GMUX=3, MUX=3 (GPIO_SetupPinMux의 muxPosition 15).
//   DriverLib의 GPIO_42_SCIA_TX(0x...0F), GPIO_43_SCIA_RX(0x...0F)와 같다.
//   11_SCI_to_USB 예제와 같은 핀(A-Side 15번/17번)이라 (11) 회로 점퍼선을 옮기지 않아도 된다.
//*****************************************************************************
static void ConfigureUART(void)
{
    // GPIO42/43은 USB0DM/DP와 같은 핀이라 아날로그 모드가 켜져 있을 수 있다. SCI로 쓰려면 반드시 끈다.
    EALLOW;
    GpioCtrlRegs.GPBAMSEL.bit.GPIO42 = 0U;
    GpioCtrlRegs.GPBAMSEL.bit.GPIO43 = 0U;
    EDIS;

    GPIO_SetupPinMux(43, GPIO_MUX_CPU1, 15);
    GPIO_SetupPinOptions(43, GPIO_INPUT, GPIO_PUSHPULL | GPIO_ASYNC);     // SCIA_RX: 비동기 입력
    GPIO_SetupPinMux(42, GPIO_MUX_CPU1, 15);
    GPIO_SetupPinOptions(42, GPIO_OUTPUT, GPIO_PUSHPULL);                 // SCIA_TX

    // SCI-A 클럭은 InitSysCtrl()이 켜 둔다(CpuSysRegs.PCLKCR7.bit.SCI_A)
    UARTStdioConfig(0, CONSOLE_BAUDRATE, LSPCLK_HZ);
}

//*****************************************************************************
// main - 프로그램 시작점
//*****************************************************************************
int main(void)
{
    //
    // 시스템 클럭(PLL 200MHz)과 주변장치 클럭을 초기화한다
    //
    InitSysCtrl();
    InitGpio();

    //
    // PIE와 벡터 테이블을 초기화한다 (이 예제는 인터럽트를 쓰지 않는다)
    //
    DINT;
    InitPieCtrl();
    IER = 0x0000U;
    IFR = 0x0000U;
    InitPieVectTable();
    EINT;
    ERTM;

    ConfigureUART();        // SCI-A 콘솔
    M95M04_init();          // SPI-A + CS2 (GPIO19)

    EepromConsole_banner("Bitfield");
    EepromConsole_run();    // 돌아오지 않는다
    return 0;
}

// 파일 끝.
