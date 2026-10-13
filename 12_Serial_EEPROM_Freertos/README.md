# 시리얼 EEPROM (M95M04, SPI) 대화형 콘솔 — TMS320F28P65x DriverLib + FreeRTOS 버전

**요약**
- 개발보드 V2의 **(12) 시리얼 FLASH & EEPROM** 회로에서 **EEPROM(ST M95M04-DR, 4 Mbit = 512 KB)** 을 SPI-A로 읽고 쓰고 검증합니다. **(11) SCI-to-USB** 터미널에서 `status`, `read`, `write`, `fill`, `test` 명령으로 조작합니다.
- DriverLib 기반에 **FreeRTOS(정적 할당, 힙 없음)** 를 얹었습니다. 페이지 쓰기 완료를 기다리는 동안(최대 5 ms) `vTaskDelay(1)`로 CPU를 양보하고, 콘솔 입력 대기도 CPU를 양보합니다.
- EEPROM 프로토콜(`m95m04.c`)과 명령어 쉘(`eeprom_cmds.c`)은 [DriverLib 버전](../12_Serial_EEPROM_Driverlib/)과 같은 파일입니다. 명령어 사용법, 출력 예, 문제 해결, 호스트 테스트는 DriverLib 버전 README에 자세히 있습니다.

## 1. 하드웨어 배선

### 1.1 (12) 회로 핀-헤더 CN9412 ↔ F28P65x 모듈 (A-Side CN9000)

EEPROM은 **CS2**를 씁니다. 시리얼 FLASH(M25P40)와 DI/CLK/DO를 공유하지만 CS1을 연결하지 않으면 보드 풀업으로 FLASH가 비선택 상태입니다.

| (12) 헤더 핀 | 신호 | F28P65x 모듈 핀 (A-Side) | GPIO | DriverLib 핀 설정 |
|---|---|---|---|---|
| **DI** | MOSI | **A-Side 97번** | GPIO16 | `GPIO_16_SPIA_PICO` |
| **DO** | MISO | **A-Side 99번** | GPIO17 | `GPIO_17_SPIA_POCI` |
| **CLK** | SPICLK | **A-Side 101번** | GPIO18 | `GPIO_18_SPIA_CLK` |
| **CS2** | EEPROM 칩 선택 | **A-Side 103번** | GPIO19 | 일반 GPIO 출력(Low 활성) |
| CS1 | FLASH 칩 선택 | 연결하지 않음 | — | — |

이 SPI 핀(GPIO16~19)은 같은 핀을 쓰는 다른 SPI 예제와 겹칠 수 있으니 동시에 연결하지 마세요.

### 1.2 (11) SCI-to-USB ↔ F28P65x 모듈 (A-Side CN9000)

| (11) 핀 | 방향 | F28P65x 모듈 핀 (A-Side) | GPIO |
|---|---|---|---|
| **RX1** | PC → DSP | **A-Side 17번** | GPIO43 (SCIA_RX) |
| **TX1** | DSP → PC | **A-Side 15번** | GPIO42 (SCIA_TX) |

[11_SCI_to_USB 예제](../11_SCI_to_USB_Driverlib/)와 같은 핀이라 점퍼선을 옮기지 않아도 됩니다. GPIO42/43은 USB0DM/DP와 같은 핀이라 코드에서 아날로그 모드를 끄며, 5번 USB 예제와 동시에 쓸 수 없습니다.

터미널은 **115200 8N1, 흐름 제어 없음**.

### 1.3 연결도

![TMS320F28X EVM V2 — (12) Serial FLASH/EEPROM과 (11) SCI-to-USB 결선](f28xevm_v2_eeprom.png)

| 선 색 | 모듈 핀 (A-Side) | 연결 대상 |
|---|---|---|
| 노랑 | 17번 (GPIO43, SCIA_RX) | (11) **RX1** |
| 빨강 (맨 위 줄에서 출발) | 15번 (GPIO42, SCIA_TX) | (11) **TX1** |
| 빨강 (아래쪽 줄에서 출발) | 97번 (GPIO16, SPIA_PICO) | (12) **DI** |
| 파랑 | 101번 (GPIO18, SPIA_CLK) | (12) **CLK** |
| 주황 | 99번 (GPIO17, SPIA_POCI) | (12) **DO** |
| 초록 | 103번 (GPIO19, CS2) | (12) **CS2** |

- 점퍼선은 6개이고, 빨간 선이 두 개이니 출발하는 핀 번호로 구분하세요. CS1은 연결하지 않습니다.
- 그림의 빨간 박스는 (11) 회로의 Mini-B USB 커넥터입니다. 여기를 PC에 연결하면 가상 COM 포트가 생기고, 이 포트로 콘솔을 씁니다.
- (11) 헤더 위의 `G28 G29` 같은 실크 표기와 상관없이, 이 예제는 점퍼선으로 A-Side 15번/17번(GPIO42/43)에 직접 연결하므로 위 표를 따르세요.
- 이 연결도는 DriverLib, 비트필드, FreeRTOS 세 버전에서 같습니다.

## 2. 명령어

| 명령 | 설명 | 예 |
|---|---|---|
| `help` | 명령어 목록 | `help` |
| `status` | 상태 레지스터와 블록 보호 상태 | `status` |
| `read <주소> [길이]` | hex + ASCII 덤프(기본 64바이트, 최대 1024) | `read 0x1f0 48` |
| `write <주소> <문자열>` | 쓰고 읽어서 검증 | `write 0x100 hello` |
| `fill <주소> <길이> <값>` | 범위를 한 바이트 값으로 채우고 검증 | `fill 0 512 0xff` |
| `test <주소> <길이>` | 패턴 쓰기 → 읽기 → 비교(범위를 덮어씀) | `test 0x1ff 100` |
| `uptime` | **(FreeRTOS 전용)** RTOS 틱 시간과 하트비트 카운터 | `uptime` |

주소·길이·값은 10진수 또는 `0x` 16진수, 칩 범위는 `0x00000`~`0x7FFFF`입니다.

## 3. FreeRTOS 구성

| 태스크 | 우선순위 | 스택(워드) | 하는 일 |
|---|---|---|---|
| `HeartbeatTask` | 2 | 128 | 500ms마다 `heartbeatCount++` (CCS Expressions로 관찰) |
| `ConsoleTask` | 1 | 768 | 배너, 명령어 쉘. 입력 대기 중 `vTaskDelay(1ms)`로 CPU 양보 |
| Idle | 0 | 128 | FreeRTOS 기본 |

- **정적 할당만** 씁니다(`configSUPPORT_STATIC_ALLOCATION=1`, 동적 할당 0). 태스크 스택은 `.freertosStaticStack` 섹션(RAMD0/RAMD1)입니다. 틱은 CPU Timer2(1 kHz).
- EEPROM 접근은 ConsoleTask 하나만 하므로 별도 잠금(뮤텍스)이 없습니다. 다른 태스크에서 EEPROM을 쓰게 만들 때는 `m95m04.c` 호출을 뮤텍스로 보호해야 합니다(`fill`이 정적 버퍼를 쓰므로 재진입 불가).
- **HAL의 대기**: `m95m04_hal.c`의 `M95M04_hal_waitStepUs()`가 busy-wait 대신 `vTaskDelay(1)`을 쓰고 1000 µs를 돌려줍니다. 드라이버는 이 시간을 합산해 20 ms를 넘으면 쓰기 타임아웃으로 처리합니다.
- **ConsoleTask 스택 추정**: 컴파일된 어셈블리의 함수별 스택 프레임으로 구한 최악 경로(`Cmd_test` 프레임 92 + `UARTprintf`→`UARTvprintf`(256워드 버퍼)→`uvsnprintf` 288 + 쉘 루프)가 약 470워드이고, 컨텍스트 저장·인터럽트 여유를 더해도 약 600워드입니다. 768워드는 여유가 있습니다. `configCHECK_FOR_STACK_OVERFLOW=2`이므로 넘치면 `vApplicationStackOverflowHook`에 걸립니다(CCS에서 브레이크포인트를 걸어 두세요).
- **링커**: RAM 구성은 `.text`를 RAMLS0~3→RAMD0 순으로 채워 RAMD1을 정적 스택에 남기고, 문자열 상수(`.const`, C28x는 char가 1워드라 약 4K워드)는 8K워드 GS RAM(RAMGS3~4)에 둡니다.

## 4. 소프트웨어 구조

| 파일 | 역할 |
|---|---|
| `12_Serial_EEPROM_Freertos.c` | 태스크 2개, `uptime` 명령, FreeRTOS 훅(스택 오버플로, Idle 메모리) |
| `m95m04_hal.c` | DriverLib SPI-A HAL(표준 SPI 모드 0, 5 MHz, CS2=GPIO19). 대기를 `vTaskDelay`로 구현 |
| `m95m04.c` / `.h`, `eeprom_cmds.c` / `.h` | EEPROM 프로토콜과 명령어 쉘(세 버전 공통, `EEPROM_WITH_RTOS` 정의 시 `uptime` 등록) |
| `utils/uartstdio.c` | SCI-A 콘솔. `UARTgetc`가 입력이 없으면 CPU를 양보하도록 수정됨 |
| `FreeRTOS/`, `FreeRTOSConfig.h`, `driverlib.lib`, `device/` | FreeRTOS 커널과 C28x 포트, DriverLib (`11_SCI_to_USB_Freertos`와 같은 구성) |

## 5. 빌드·실행

1. CCS에서 `File > Import > CCS Projects`로 이 폴더의 `CCS/`를 지정해 `12_Serial_EEPROM_Freertos`를 가져옵니다.
2. 구성 선택: **CPU1_RAM** 또는 **CPU1_FLASH**.
3. Build → Debug → Run, 터미널에서 `help`, `uptime`을 입력합니다. `test 0 65536`(128페이지, 약 1초)처럼 쓰기가 긴 명령을 실행하는 동안에도 Expressions의 `heartbeatCount`가 계속 오르는지 보면 멀티태스킹이 보입니다.

### 실행 화면 (시리얼 콘솔)

![CCS Serial Console(COM35)에서 본 EEPROM 콘솔 — 배너, read, write, read](ccs_serial_console_eeprom.png)

- 배너 다음 줄 `EEPROM answered, status register = 0x00`은 EEPROM이 응답했다는 뜻입니다. `0xFF`이거나 경고가 나오면 배선을 확인하세요.
- `read 0`은 0번지부터 64바이트를 16바이트씩 hex와 ASCII로 보여 줍니다. ASCII 열에는 출력 가능한 글자(0x20~0x7E)만 나오고 나머지는 `.`입니다. 첫 줄의 `74 74 70 6d`는 이전에 써 둔 글자(`ttpm`)이고, `fc fb fa …`로 이어지는 값은 이전 테스트 패턴이 남아 있는 것입니다.
- `write 0 test`는 4바이트를 쓰고 바로 읽어서 검증합니다(`verify OK`). 다시 `read 0`을 하면 `74 65 73 74`, 즉 `test`로 바뀌어 있습니다.
- COM 번호(그림은 COM35)는 PC마다 다릅니다. 터미널 설정은 115200 8N1입니다. CCS의 Serial Console 설정은 [11_SCI_to_USB 예제](../11_SCI_to_USB_Bitfield/)를 참고하세요.
- 이 화면은 FreeRTOS 버전에서 캡처한 것이라 배너에 `FreeRTOS version`이 나옵니다.

## 6. 검증 상태

- **확인함(2026-10-06, 수정 후 2026-10-13 다시 확인)**: CCS 임포트·빌드를 흉내 낸 빌드로 **CPU1_RAM, CPU1_FLASH 모두 경고 없이 링크**. 공유 파일 4개가 다른 두 버전과 바이트 단위로 같음을 확인했고, 그 파일들의 PC 호스트 테스트(프로토콜 13개, 쉘 14개)는 DriverLib 버전 폴더에서 통과합니다. ConsoleTask 스택은 위와 같이 추정으로 확인했습니다.
- **보드 확인함(2026-10-13)**: SCI 콘솔(GPIO42/43, 115200 8N1), EEPROM 응답, `read`/`write` 검증, ASCII 덤프(위 실행 화면).
- **확인 전**: `uptime` 출력, 긴 쓰기 중 `heartbeatCount` 증가 같은 FreeRTOS 스케줄링 동작, `status`/`fill`/`test` 명령.

## 7. 세 버전 비교

| 버전 | 폴더 | 핵심 차이 | RAM 구성 사용량 | FLASH 구성 사용량 |
|---|---|---|---|---|
| DriverLib | [12_Serial_EEPROM_Driverlib](../12_Serial_EEPROM_Driverlib/) | TI 표준 HAL 함수 | 12,833 워드 | Flash 11,210 / RAM 2,369 |
| 비트필드 | [12_Serial_EEPROM_Bitfield](../12_Serial_EEPROM_Bitfield/) | 레지스터 직접 조작, DriverLib 없음 | 11,248 워드 | Flash 9,626 / RAM 2,167 |
| **FreeRTOS(이 폴더)** | 12_Serial_EEPROM_Freertos | 태스크 2개, 쓰기 대기 중 CPU 양보, `uptime` | 16,252 워드 | Flash 13,862 / RAM 3,138 |

링크 맵의 영역별 사용량 합계(워드, 2026-10-13 CGT 25.11.1.LTS로 측정)입니다. RAM 구성은 코드까지 RAM에 올라가므로 크고, 정적 스택이 포함됩니다.
