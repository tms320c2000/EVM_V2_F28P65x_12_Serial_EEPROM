# 시리얼 EEPROM (M95M04, SPI) 대화형 콘솔 — TMS320F28P65x DriverLib 버전

**요약**
- 개발보드 V2의 **(12) 시리얼 FLASH & EEPROM** 회로에서 **EEPROM(ST M95M04-DR, 4 Mbit = 512 KB)** 을 SPI-A 마스터로 읽고 쓰고 검증합니다. **(11) SCI-to-USB** 터미널에서 명령(`status`, `read`, `write`, `fill`, `test`)으로 조작합니다.
- 페이지(512바이트) 경계 처리, 쓰기 완료 대기(WIP), 쓰기 후 읽어 검증을 포함한 EEPROM 드라이버(`m95m04.c`)는 세 버전이 **같은 파일**이고, PC에서 가짜 칩으로 테스트할 수 있습니다.
- 같은 동작을 [비트필드 버전](../12_Serial_EEPROM_Bitfield/), [FreeRTOS 버전](../12_Serial_EEPROM_Freertos/)으로도 제공합니다(비교표는 아래).

## 1. 하드웨어 배선

### 1.1 (12) 회로 핀-헤더 CN9412 ↔ F28P65x 모듈 (A-Side CN9000)

(12)번 블록의 5핀 헤더(`CS1 DI CLK DO CS2`)에서 EEPROM은 **CS2**를 씁니다. 시리얼 FLASH(M25P40)와 DI/CLK/DO를 공유하지만 **CS1을 연결하지 않으면** 보드의 4.7kΩ 풀업으로 FLASH가 비선택 상태를 유지합니다.

| (12) 헤더 핀 | 신호 | F28P65x 모듈 핀 (A-Side) | GPIO | DriverLib 핀 설정 |
|---|---|---|---|---|
| **DI** | MOSI | **A-Side 97번** | GPIO16 | `GPIO_16_SPIA_PICO` |
| **DO** | MISO | **A-Side 99번** | GPIO17 | `GPIO_17_SPIA_POCI` |
| **CLK** | SPICLK | **A-Side 101번** | GPIO18 | `GPIO_18_SPIA_CLK` |
| **CS2** | EEPROM 칩 선택 | **A-Side 103번** | GPIO19 | 일반 GPIO 출력(Low 활성, 소프트웨어 제어) |
| CS1 | FLASH 칩 선택 | 연결하지 않음 | — | — |

- 핀 번호는 F28P65x 모듈 핀 데이터와 DriverLib `pin_map.h`를 대조해 확인했습니다.
- 헤더 옆 LED가 선택된 칩(CS1/CS2)을 보여 줍니다. 명령을 실행하면 CS2 쪽 LED가 켜지는지 확인할 수 있습니다.
- 이 SPI 핀(GPIO16~19)은 같은 핀을 쓰는 다른 SPI 예제와 겹칠 수 있으니 동시에 연결하지 마세요.

### 1.2 (11) SCI-to-USB ↔ F28P65x 모듈 (A-Side CN9000)

| (11) 핀 | 방향 | F28P65x 모듈 핀 (A-Side) | GPIO |
|---|---|---|---|
| **RX1** | PC → DSP | **A-Side 17번** | GPIO43 (SCIA_RX) |
| **TX1** | DSP → PC | **A-Side 15번** | GPIO42 (SCIA_TX) |

[11_SCI_to_USB 예제](../11_SCI_to_USB_Driverlib/)와 같은 핀이라 점퍼선을 옮기지 않아도 됩니다. GPIO42/43은 USB0DM/DP와 같은 핀이라 코드에서 아날로그 모드를 끄며, 5번 USB 예제와 동시에 쓸 수 없습니다.

(11) 블록의 Mini-B USB를 PC에 연결하면 가상 COM 포트가 생깁니다. 터미널은 **115200 8N1, 흐름 제어 없음**.

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

## 2. M95M04 요약과 이 예제의 SPI 설정

- 용량 512 KB(4 Mbit), **페이지 512바이트(1024페이지)**, 3바이트 주소(19비트 사용), 쓰기 사이클 5 ms 이내, 클럭 최대 10 MHz, SPI 모드 0/3.
- 사용 명령: `WREN 0x06`, `WRITE 0x02`, `READ 0x03`, `RDSR 0x05`. 상태 레지스터: WIP(bit0 쓰기 중), WEL(bit1 쓰기 허용), BP(bit2~3 블록 보호), SRWD(bit7).
- SPI: 마스터, 8비트, **표준 SPI 모드 0**(TI 표기 `SPI_PROT_POL0PHA1`), 기본 **5 MHz**(점퍼선 여유). 선이 짧고 안정적이면 `m95m04_hal.c`의 `EE_SPI_BITRATE`를 최대 10 MHz까지 올릴 수 있습니다.
- 송신 버퍼는 좌측 정렬(`<<8`), 수신 버퍼(`SPIRXBUF`)는 우측 정렬(`& 0xFF`), FIFO는 쓰지 않습니다.
- 쓰기는 **페이지 경계에서 나눠** 보냅니다. 한 번의 `WRITE`가 페이지 끝을 넘으면 칩이 같은 페이지의 앞으로 되감아 이미 있던 데이터를 덮어쓰기 때문입니다.

## 3. 명령어

부팅하면 배너와 함께 `EEPROM answered, status register = 0x00`, 프롬프트 `eeprom> `가 나옵니다(칩이 응답하지 않으면 `WARNING: EEPROM did not answer`).

| 명령 | 설명 | 예 |
|---|---|---|
| `help`, `h`, `?` | 명령어 목록 | `help` |
| `status` | 상태 레지스터와 블록 보호 상태 | `status` |
| `read <주소> [길이]` | hex + ASCII 덤프(기본 64바이트, 최대 1024) | `read 0x1f0 48` |
| `write <주소> <문자열>` | 문자열을 쓰고 **읽어서 검증** | `write 0x100 hello world` |
| `fill <주소> <길이> <값>` | 범위를 한 바이트 값으로 채우고 검증 | `fill 0 512 0xff` |
| `test <주소> <길이>` | 패턴 쓰기 → 읽기 → 비교(**범위를 덮어씀**) | `test 0x1ff 100` |

주소·길이·값은 10진수 또는 `0x` 16진수입니다. 칩 범위는 `0x00000`~`0x7FFFF`입니다.

```text
eeprom> write 0x1f8 EEPROM page boundary test
Wrote 25 bytes at 0x001f8, verify OK

eeprom> read 0x1f0 48
001f0: ff ff ff ff ff ff ff ff 45 45 50 52 4f 4d 20 70  |........EEPROM p|
00200: 61 67 65 20 62 6f 75 6e 64 61 72 79 20 74 65 73  |age boundary tes|
00210: 74 ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff  |t...............|

eeprom> status
Status register = 0x00  (WIP=0 WEL=0 BP=0 SRWD=0)
Block protect: none (whole chip writable)

eeprom> test 0x200 40
WARNING: overwriting 0x00200..0x00227 (40 bytes, 1 page(s))
Test PASS: 40 bytes at 0x00200 (1 page(s))
```

위 출력은 가짜 칩과 실제 대상용 `ustdlib`(경량 printf)로 만든 것입니다. 이 예에서 `0x1f8`에서 시작한 25바이트가 `0x200`(페이지 경계)을 넘어도 정상으로 이어집니다.

문제가 생기면:
- `status`가 `0xff`이거나 `did not answer`: DI/DO/CLK/CS2 배선과 접지를 확인하세요.
- 쓰기 후 `Verify FAILED`와 `block-protect bits are set`: 블록 보호(BP) 비트가 켜져 있어 해당 영역에 쓰기가 무시된 것입니다(`status`로 확인).
- `write did not finish in time`: 칩이 쓰기 완료(WIP 해제)를 알리지 않습니다. 배선이나 전원을 확인하세요.

## 4. 소프트웨어 구조

| 파일 | 역할 |
|---|---|
| `12_Serial_EEPROM_Driverlib.c` | 메인(DriverLib 초기화, SCI-A 콘솔) |
| `m95m04_hal.c` | **DriverLib SPI-A HAL**: SPI 바이트 교환, CS2(GPIO19), 대기 지연 |
| `m95m04.c` / `m95m04.h` | EEPROM 프로토콜(읽기, 페이지 분할 쓰기, WIP 대기, fill, verify). 세 버전 공통 |
| `eeprom_cmds.c` / `.h` | 대화형 명령어 쉘. 세 버전 공통 |
| `utils/` | 명령행 파서, 경량 printf, SCI-A 콘솔(DriverLib) |
| `device/`, `driverlib.lib` | F28P65x DriverLib 헤더와 사전 빌드 라이브러리 |
| `test/host/` | PC용 호스트 테스트(가짜 M95M04 칩). CCS 프로젝트에는 복사되지 않음 |

## 5. 빌드·실행

1. CCS에서 `File > Import > CCS Projects`로 이 폴더의 `CCS/`를 지정해 `12_Serial_EEPROM_Driverlib`을 가져옵니다.
2. 구성 선택: **CPU1_RAM**(디버깅용), **CPU1_FLASH**(전원을 껐다 켜도 실행).
3. Build → Debug → Run, 터미널에서 `help`를 입력합니다.

### 실행 화면 (시리얼 콘솔)

![CCS Serial Console(COM35)에서 본 EEPROM 콘솔 — 배너, read, write, read](ccs_serial_console_eeprom.png)

- 배너 다음 줄 `EEPROM answered, status register = 0x00`은 EEPROM이 응답했다는 뜻입니다. `0xFF`이거나 경고가 나오면 배선을 확인하세요.
- `read 0`은 0번지부터 64바이트를 16바이트씩 hex와 ASCII로 보여 줍니다. ASCII 열에는 출력 가능한 글자(0x20~0x7E)만 나오고 나머지는 `.`입니다. 첫 줄의 `74 74 70 6d`는 이전에 써 둔 글자(`ttpm`)이고, `fc fb fa …`로 이어지는 값은 이전 테스트 패턴이 남아 있는 것입니다.
- `write 0 test`는 4바이트를 쓰고 바로 읽어서 검증합니다(`verify OK`). 다시 `read 0`을 하면 `74 65 73 74`, 즉 `test`로 바뀌어 있습니다.
- COM 번호(그림은 COM35)는 PC마다 다릅니다. 터미널 설정은 115200 8N1입니다. CCS의 Serial Console 설정은 [11_SCI_to_USB 예제](../11_SCI_to_USB_Bitfield/)를 참고하세요.
- 이 화면은 FreeRTOS 버전에서 캡처한 것입니다. 이 버전의 배너는 `DriverLib version`으로 나오고, 명령 출력은 같은 `eeprom_cmds.c`가 만들어 같습니다.

## 6. 검증 상태

- **PC 호스트 테스트(2026-10-06, 수정 후 2026-10-13 다시 통과)**: 가짜 M95M04 칩(페이지 래핑, WEL/WIP, 블록 보호 모델)으로 프로토콜 13개, 명령어 쉘 14개 테스트 통과. 페이지 경계 분할을 일부러 제거한 변형으로 테스트가 실패하는 것도 확인했습니다. 실행(예제 폴더에서, gcc):
  ```
  gcc -Wall -Itest/host -I. -o t1 test/host/test_m95m04.c m95m04.c && ./t1
  gcc -Wall -Itest/host -Itest/host/shim -I. -Iutils -DEEPROM_WITH_RTOS -o t2 \
      test/host/test_console.c eeprom_cmds.c m95m04.c utils/cmdline.c utils/ustdlib.c && ./t2
  ```
  명령어 테스트는 대상과 같은 `ustdlib.c`(경량 printf)와 `cmdline.c`를 링크해서, 대상에서 나오는 출력 서식을 그대로 확인합니다.
- **CCS 방식 빌드(2026-10-06, 수정 후 2026-10-13 다시 확인)**: `.projectspec` 복사 규칙대로 새 폴더를 만들어 전체 소스를 컴파일·링크하는 흉내 빌드로 **CPU1_RAM, CPU1_FLASH 모두 경고 없이 링크**.
- **보드 확인(참고)**: 이 버전의 `m95m04.c`·`eeprom_cmds.c`와 DriverLib SPI HAL(대기 방식만 다름)은 [FreeRTOS 버전](../12_Serial_EEPROM_Freertos/)과 [비트필드 버전](../12_Serial_EEPROM_Bitfield/)에서 보드 동작을 확인했습니다(2026-10-13).
- **확인 전**: 이 DriverLib 버전 자체는 보드에서 아직 돌려 보지 않았습니다. SCI 핀 변경(GPIO42/43)과 `read`의 ASCII 출력 수정 후에는 컴파일만 확인했습니다.

## 7. 세 버전 비교

| 버전 | 폴더 | 핵심 차이 | RAM 구성 사용량 | FLASH 구성 사용량 |
|---|---|---|---|---|
| **DriverLib(이 폴더)** | 12_Serial_EEPROM_Driverlib | TI 표준 HAL 함수 | 12,833 워드 | Flash 11,210 / RAM 2,369 |
| 비트필드 | [12_Serial_EEPROM_Bitfield](../12_Serial_EEPROM_Bitfield/) | 레지스터 직접 조작, DriverLib 없음 | 11,248 워드 | Flash 9,626 / RAM 2,167 |
| FreeRTOS | [12_Serial_EEPROM_Freertos](../12_Serial_EEPROM_Freertos/) | 태스크 2개(콘솔/하트비트), 쓰기 대기 중 CPU 양보, `uptime` 명령 | 16,252 워드 | Flash 13,862 / RAM 3,138 |

링크 맵의 영역별 사용량 합계(워드, 2026-10-13 CGT 25.11.1.LTS로 측정)입니다. RAM 구성은 코드까지 RAM에 올라가므로 크고, 스택(0x600)이 포함됩니다.
