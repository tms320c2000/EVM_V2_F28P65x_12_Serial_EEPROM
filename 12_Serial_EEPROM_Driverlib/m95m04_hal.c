//#############################################################################
//
// FILE:   m95m04_hal.c  (DriverLib 버전)
//
// TITLE:  M95M04 SPI HAL — F28P65x DriverLib SPI-A
//
// 배선 (개발보드 (12) 회로 핀-헤더 CN9412 <-> F28P65x 모듈 A-Side CN9000):
//   - DI  (MOSI): GPIO16 (SPIA_PICO) -> A-Side 97번
//   - DO  (MISO): GPIO17 (SPIA_POCI) -> A-Side 99번
//   - CLK (SCLK): GPIO18 (SPIA_CLK)  -> A-Side 101번
//   - CS2 (EEPROM): GPIO19 (GPIO 출력, Low 활성, 소프트웨어 제어) -> A-Side 103번
//   - CS1(시리얼 FLASH)은 연결하지 않는다: 보드의 4.7kΩ 풀업으로 High(비선택)가 유지된다.
//
// SPI 설정: 마스터, 8비트, 표준 SPI 모드 0 (TI 표기 SPI_PROT_POL0PHA1), 5 MHz(칩 최대 10 MHz).
//   송신 버퍼는 좌측 정렬(<<8), 수신 버퍼(SPIRXBUF)는 우측 정렬(& 0xFF). FIFO는 쓰지 않는다.
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "m95m04.h"

#define EE_CS_GPIO          19U             // CS2로 쓰는 GPIO 번호
#define EE_SPI_BITRATE      5000000UL       // SPI 클럭 5 MHz (칩 최대 10 MHz)
#define EE_WAIT_STEP_US     100U            // 쓰기 완료를 기다리는 한 단계의 쉬는 시간(µs)

// SPI-A와 CS2(GPIO19)를 초기화한다
void M95M04_hal_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SPIA);

    GPIO_setPinConfig(GPIO_16_SPIA_PICO);
    GPIO_setPinConfig(GPIO_17_SPIA_POCI);
    GPIO_setPinConfig(GPIO_18_SPIA_CLK);
    GPIO_setPadConfig(16U, GPIO_PIN_TYPE_STD);
    GPIO_setPadConfig(17U, GPIO_PIN_TYPE_PULLUP);
    GPIO_setPadConfig(18U, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(16U, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(17U, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(18U, GPIO_QUAL_ASYNC);

    // CS는 High로 먼저 만든 뒤 출력으로 바꿔 글리치를 막는다
    GPIO_setPinConfig(GPIO_19_GPIO19);
    GPIO_writePin(EE_CS_GPIO, 1U);
    GPIO_setDirectionMode(EE_CS_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(EE_CS_GPIO, GPIO_PIN_TYPE_STD);

    SPI_disableModule(SPIA_BASE);
    SPI_disableFIFO(SPIA_BASE);
    SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ, SPI_PROT_POL0PHA1,
                  SPI_MODE_CONTROLLER, EE_SPI_BITRATE, 8U);
    SPI_enableTalk(SPIA_BASE);
    SPI_setEmulationMode(SPIA_BASE, SPI_EMULATION_FREE_RUN);
    SPI_enableModule(SPIA_BASE);
}

// CS2를 Low로 내려 EEPROM을 선택한다
void M95M04_hal_select(void)
{
    GPIO_writePin(EE_CS_GPIO, 0U);
}

// CS2를 High로 올려 EEPROM 선택을 해제한다
void M95M04_hal_deselect(void)
{
    GPIO_writePin(EE_CS_GPIO, 1U);
}

// SPI로 한 바이트를 보내고, 같은 클럭에 들어온 바이트를 돌려준다(FIFO 없이 완료될 때까지 대기)
m95_byte M95M04_hal_transfer(m95_byte out)
{
    SPI_writeDataBlockingNonFIFO(SPIA_BASE, ((uint16_t)out) << 8U);
    return (m95_byte)(SPI_readDataBlockingNonFIFO(SPIA_BASE) & 0xFFU);
}

// 쓰기 완료 대기의 한 단계: EE_WAIT_STEP_US만큼 쉬고, 쉰 시간(µs)을 돌려준다
uint32_t M95M04_hal_waitStepUs(void)
{
    DEVICE_DELAY_US(EE_WAIT_STEP_US);
    return EE_WAIT_STEP_US;
}
