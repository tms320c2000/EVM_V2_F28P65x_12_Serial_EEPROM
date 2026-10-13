//#############################################################################
//
// FILE:   12_Serial_EEPROM_Driverlib.c
//
// TITLE:  시리얼 EEPROM(M95M04, SPI) 대화형 콘솔 — TMS320F28P65x DriverLib 버전
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
// 구성: m95m04.c(EEPROM 프로토콜) + m95m04_hal.c(DriverLib SPI-A) + eeprom_cmds.c(명령어 쉘).
//       m95m04.c와 eeprom_cmds.c는 세 버전(DriverLib / 비트필드 / FreeRTOS)에서 같은 파일입니다.
//
//#############################################################################

#include <stdint.h>
#include <stdbool.h>

#include "driverlib.h"
#include "device.h"
#include "m95m04.h"
#include "uartstdio.h"
#include "eeprom_cmds.h"

//*****************************************************************************
// ConfigureUART - SCI-A(GPIO43 RX, GPIO42 TX)를 115200 8N1 콘솔로 설정
//   11_SCI_to_USB 예제와 같은 핀(A-Side 15번/17번)이라 (11) 회로 점퍼선을 옮기지 않아도 된다.
//*****************************************************************************
static void ConfigureUART(void)
{
    // GPIO42/43은 USB0DM/DP와 같은 핀이라 아날로그 모드가 켜져 있을 수 있다. SCI로 쓰려면 반드시 끈다.
    GPIO_setAnalogMode(42U, GPIO_ANALOG_DISABLED);
    GPIO_setAnalogMode(43U, GPIO_ANALOG_DISABLED);

    GPIO_setPinConfig(GPIO_42_SCIA_TX);
    GPIO_setPinConfig(GPIO_43_SCIA_RX);

    GPIO_setPadConfig(42, GPIO_PIN_TYPE_STD);
    GPIO_setPadConfig(43, GPIO_PIN_TYPE_STD);

    GPIO_setQualificationMode(42, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(43, GPIO_QUAL_ASYNC);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SCIA);

    UARTStdioConfig(0, 115200, DEVICE_LSPCLK_FREQ);
}

//*****************************************************************************
// main - 프로그램 시작점
//*****************************************************************************
int main(void)
{
    //
    // 시스템 클럭과 디바이스 주변장치를 초기화한다
    //
    Device_init();
    Device_initGPIO();

    //
    // PIE와 벡터 테이블을 초기화한다 (이 예제는 인터럽트를 쓰지 않는다)
    //
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Interrupt_enableGlobal();

    ConfigureUART();        // SCI-A 콘솔
    M95M04_init();          // SPI-A + CS2 (GPIO19)

    EepromConsole_banner("DriverLib");
    EepromConsole_run();    // 돌아오지 않는다
    return 0;
}

// 파일 끝.
