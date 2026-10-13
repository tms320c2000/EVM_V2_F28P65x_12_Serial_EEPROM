//#############################################################################
//
// FILE:   12_Serial_EEPROM_Freertos.c
//
// TITLE:  시리얼 EEPROM(M95M04, SPI) 대화형 콘솔 — TMS320F28P65x DriverLib + FreeRTOS 버전
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
// FreeRTOS 구성(정적 할당만 사용, 힙 없음):
//   - ConsoleTask   (우선순위 1): 명령어 쉘. 입력을 기다리는 동안 vTaskDelay로 CPU를 양보합니다.
//   - HeartbeatTask (우선순위 2): 500ms마다 heartbeatCount를 올립니다(CCS Expressions로 관찰, 'uptime' 명령).
//   EEPROM 접근은 ConsoleTask 하나만 하므로 별도 잠금은 없습니다. 페이지 쓰기 완료를 기다리는 동안
//   (최대 5ms)에는 vTaskDelay(1)로 CPU를 양보해, 우선순위 높은 HeartbeatTask가 계속 돕니다.
//   FreeRTOS 틱은 CPU Timer2(1ms)를 씁니다.
//
//#############################################################################

#include <stdint.h>
#include <stdbool.h>

#include "driverlib.h"
#include "device.h"
#include "FreeRTOS.h"
#include "task.h"
#include "m95m04.h"
#include "uartstdio.h"
#include "eeprom_cmds.h"

#define CONSOLE_STACK_WORDS         768U    // 쉘 + UARTprintf(256워드 버퍼)가 쓰는 스택, 워드(16비트) 단위
#define HEARTBEAT_STACK_WORDS       128U
#define IDLE_STACK_WORDS            128U

//
// CCS Expressions 창에 등록해 두면 동작을 실시간으로 볼 수 있는 전역 변수
//
volatile uint32_t heartbeatCount = 0UL;     // HeartbeatTask가 500ms마다 +1 (콘솔이 멈춰 있어도 계속 올라감)

//
// FreeRTOS 태스크용 정적 메모리 (힙 미사용, 스택은 .freertosStaticStack 섹션에만 둔다)
//
static StaticTask_t g_consoleTaskBuffer;
static StackType_t  g_consoleTaskStack[CONSOLE_STACK_WORDS];
#pragma DATA_SECTION(g_consoleTaskStack, ".freertosStaticStack")
#pragma DATA_ALIGN(g_consoleTaskStack, portBYTE_ALIGNMENT)

static StaticTask_t g_heartbeatTaskBuffer;
static StackType_t  g_heartbeatTaskStack[HEARTBEAT_STACK_WORDS];
#pragma DATA_SECTION(g_heartbeatTaskStack, ".freertosStaticStack")
#pragma DATA_ALIGN(g_heartbeatTaskStack, portBYTE_ALIGNMENT)

static StaticTask_t g_idleTaskBuffer;
static StackType_t  g_idleTaskStack[IDLE_STACK_WORDS];
#pragma DATA_SECTION(g_idleTaskStack, ".freertosStaticStack")
#pragma DATA_ALIGN(g_idleTaskStack, portBYTE_ALIGNMENT)

void ConsoleTask(void *pvParameters);
void HeartbeatTask(void *pvParameters);

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
// Cmd_uptime - FreeRTOS 틱 시간과 HeartbeatTask 카운터 출력 (eeprom_cmds.c의 명령 표에 등록됨)
//*****************************************************************************
int Cmd_uptime(int argc, char *argv[])
{
    uint32_t ulTickMs = (uint32_t)xTaskGetTickCount() * (1000UL / configTICK_RATE_HZ);

    (void)argc;
    (void)argv;
    UARTprintf("uptime: %u ms, heartbeat: %u (500ms unit)\n", ulTickMs, (uint32_t)heartbeatCount);
    return 0;
}

//*****************************************************************************
// HeartbeatTask - 500ms마다 카운터를 올린다 (우선순위 2). 콘솔이 입력을 기다리거나 EEPROM을 쓰는 중에도 돈다.
//*****************************************************************************
void HeartbeatTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    (void)pvParameters;

    for(;;)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
        heartbeatCount++;
    }
}

//*****************************************************************************
// ConsoleTask - 환영 메시지와 명령어 쉘 (우선순위 1)
//*****************************************************************************
void ConsoleTask(void *pvParameters)
{
    (void)pvParameters;

    EepromConsole_banner("FreeRTOS");
    EepromConsole_run();    // 돌아오지 않는다
}

//*****************************************************************************
// main - 하드웨어 초기화 후 태스크 2개를 만들고 스케줄러를 시작한다
//*****************************************************************************
int main(void)
{
    //
    // 시스템 클럭과 디바이스 주변장치를 초기화한다
    //
    Device_init();
    Device_initGPIO();

    //
    // PIE와 벡터 테이블을 초기화한다 (FreeRTOS 틱용 CPU Timer2 인터럽트는 포트가 등록한다)
    //
    Interrupt_initModule();
    Interrupt_initVectorTable();

    ConfigureUART();        // SCI-A 콘솔 (이후 UARTprintf/UARTgets는 ConsoleTask에서만 부른다)
    M95M04_init();          // SPI-A + CS2 (GPIO19)

    //
    // 디버그 실시간 모드(ERTM)와 전역 인터럽트(EINT)를 켠다
    //
    ERTM;
    EINT;

    //
    // 태스크 생성 (정적 할당): 우선순위가 높을수록 먼저 실행된다
    //
    xTaskCreateStatic(HeartbeatTask, "Heartbeat", HEARTBEAT_STACK_WORDS, NULL,
                      tskIDLE_PRIORITY + 2, g_heartbeatTaskStack, &g_heartbeatTaskBuffer);
    xTaskCreateStatic(ConsoleTask, "Console", CONSOLE_STACK_WORDS, NULL,
                      tskIDLE_PRIORITY + 1, g_consoleTaskStack, &g_consoleTaskBuffer);

    vTaskStartScheduler();      // 이 아래로는 돌아오지 않는다

    for(;;)
    {
        // 여기 도달하면 스케줄러 시작 실패
    }
}

//*****************************************************************************
// vApplicationStackOverflowHook - FreeRTOS가 스택 오버플로를 감지하면 호출 (configCHECK_FOR_STACK_OVERFLOW=2)
//   CCS에서 이 함수에 브레이크포인트를 걸어 두면 어느 태스크(pcTaskName)가 넘쳤는지 알 수 있다.
//*****************************************************************************
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    for(;;)
    {
    }
}

//*****************************************************************************
// vApplicationGetIdleTaskMemory - configSUPPORT_STATIC_ALLOCATION=1 이면 Idle 태스크 메모리도
//   애플리케이션이 직접 제공해야 한다(힙을 안 쓰므로).
//*****************************************************************************
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &g_idleTaskBuffer;
    *ppxIdleTaskStackBuffer = g_idleTaskStack;
    *pulIdleTaskStackSize = IDLE_STACK_WORDS;
}

// 파일 끝.
