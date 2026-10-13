# TMS320F28X 개발보드 V2 — F28P65x 시리얼 EEPROM(M95M04, SPI) 예제

[TMS320F28X 범용 개발보드 V2](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903127)의
회로블록 **(12) 시리얼 FLASH & EEPROM** 에 있는 **EEPROM(ST M95M04-DR, 4 Mbit = 512 KB)** 을 F28P65x 모듈이
SPI-A 마스터로 읽고 쓰고 검증하는 예제입니다. **(11) SCI-to-USB** 로 PC 터미널을 연결해 `status`, `read`,
`write`, `fill`, `test` 같은 명령으로 EEPROM을 다룹니다. 같은 동작을 세 가지 방식으로 각각 구현해서 코드량·메모리
사용량을 비교할 수 있게 만들었습니다.

## 포함된 프로젝트

| 프로젝트 | 설명 |
|---|---|
| [12_Serial_EEPROM_Bitfield](12_Serial_EEPROM_Bitfield/) | 레지스터 비트필드 직접 제어(DriverLib 미사용) |
| [12_Serial_EEPROM_Driverlib](12_Serial_EEPROM_Driverlib/) | TI DriverLib 사용. PC에서 돌리는 호스트 테스트(`test/host`) 포함 |
| [12_Serial_EEPROM_Freertos](12_Serial_EEPROM_Freertos/) | FreeRTOS 태스크 2개(콘솔/하트비트), 정적 할당(힙 미사용), 쓰기 대기 중 CPU 양보, `uptime` 명령 |

각 폴더는 자기완결형(self-contained) CCS 프로젝트입니다 — 폴더 하나만 받아도 필요한 파일이 전부
로컬에 포함되어 있어 Import → Build가 됩니다. EEPROM 프로토콜(`m95m04.c`)과 명령어 쉘(`eeprom_cmds.c`)은
세 버전에서 같은 파일이고, SPI 바이트 교환·칩 선택·대기만 버전별 HAL(`m95m04_hal.c`)이 다릅니다.

## 배선

**SPI(EEPROM)와 SCI(콘솔) 모두 A-Side** 핀-헤더에 점퍼선으로 연결합니다. (12) 회로의 EEPROM은 **CS2**를
쓰고, 시리얼 FLASH용 CS1은 연결하지 않습니다(보드 풀업으로 FLASH는 비선택 상태).

| 번호 | 신호 | 개발보드 핀 | 모듈 핀 | GPIO / 기능 |
|---|---|---|---|---|
| 1 | RX1 (PC → DSP) | (11) SCI-to-USB RX1 | A-Side 17번 (CN9000) | GPIO43, SCIA_RX |
| 2 | TX1 (DSP → PC) | (11) SCI-to-USB TX1 | A-Side 15번 (CN9000) | GPIO42, SCIA_TX |
| 3 | DI (MOSI) | (12) 헤더 DI | A-Side 97번 (CN9000) | GPIO16, SPIA_PICO |
| 4 | DO (MISO) | (12) 헤더 DO | A-Side 99번 (CN9000) | GPIO17, SPIA_POCI |
| 5 | CLK (SCLK) | (12) 헤더 CLK | A-Side 101번 (CN9000) | GPIO18, SPIA_CLK |
| 6 | CS2 (EEPROM) | (12) 헤더 CS2 | A-Side 103번 (CN9000) | GPIO19, 일반 GPIO 출력(Low 활성) |

![TMS320F28X EVM V2 — (12) Serial FLASH/EEPROM과 (11) SCI-to-USB 결선](12_Serial_EEPROM_Bitfield/f28xevm_v2_eeprom.png)

그림의 빨간 박스는 (11) 회로의 Mini-B USB 커넥터로, PC에 연결하면 가상 COM 포트가 생깁니다. 빨간 점퍼선이 두 개이니
출발하는 핀 번호(15번 → TX1, 97번 → DI)로 구분하세요. GPIO42/43은 USB D-/D+와 같은 핀이라 코드에서 아날로그
모드를 끄며, USB 예제와 동시에 쓸 수 없습니다.

## 명령

터미널은 115200 baud, 8N1, 흐름 제어 없음입니다. 주소·길이·값은 10진수 또는 `0x` 16진수이고, 칩 범위는
`0x00000`~`0x7FFFF`입니다.

| 명령 | 설명 |
|---|---|
| `help`, `h`, `?` | 명령어 목록 |
| `status` | 상태 레지스터(WIP/WEL/블록 보호) |
| `read <주소> [길이]` | hex + ASCII 덤프(기본 64바이트, 최대 1024) |
| `write <주소> <문자열>` | 문자열을 쓰고 읽어서 검증 |
| `fill <주소> <길이> <값>` | 범위를 한 바이트 값으로 채우고 검증 |
| `test <주소> <길이>` | 패턴 쓰기 → 읽기 → 비교(범위를 덮어씀) |
| `uptime` | RTOS 틱 시간과 하트비트 카운터 (FreeRTOS 버전만) |

## 실행 화면

CCS v20의 Serial Console에 나온 화면입니다(FreeRTOS 버전에서 캡처. 배너의 버전 이름만 다르고 명령 출력은 같습니다).

![EEPROM 콘솔 실행 화면 — 부팅 배너, read, write, read](12_Serial_EEPROM_Bitfield/ccs_serial_console_eeprom.png)

`EEPROM answered, status register = 0x00`은 EEPROM이 응답했다는 뜻입니다. `write 0 test`는 4바이트를 쓰고 바로
읽어서 검증하고(`verify OK`), 다시 `read 0`을 하면 `test`로 바뀌어 있습니다. ASCII 열에는 출력 가능한 글자만 나오고
나머지는 `.`입니다. 자세한 설명은 각 프로젝트의 README를 참고하세요.

## 메모리 사용량 비교

링커 맵의 영역별 사용량 합계(워드, 16비트)입니다. `--opt_level=off`, CGT 25.11.1.LTS로 측정했습니다.

| | Bitfield | DriverLib | FreeRTOS |
|---|---|---|---|
| CPU1_RAM 구성 합계 | 11,248 | 12,833 | 16,252 |
| CPU1_FLASH 구성 — Flash | 9,626 | 11,210 | 13,862 |
| CPU1_FLASH 구성 — RAM | 2,167 | 2,369 | 3,138 |
| `FLASH_BANK0`(131,070워드) 대비 Flash | 약 7.3% | 약 8.6% | 약 10.6% |

RAM 구성은 코드까지 RAM에 올라가므로 크고, 스택이 포함됩니다.

## 검증 상태

- **보드에서 확인함(2026-10-13)**: 비트필드 버전과 FreeRTOS 버전 — 위 배선으로 부팅 배너, EEPROM 응답, `read`/`write`(검증) 동작과 ASCII 덤프.
- **빌드·PC 테스트로 확인함**: 세 버전 모두 CPU1_RAM, CPU1_FLASH 구성이 오류·경고 없이 컴파일·링크됩니다. 공통 파일(`m95m04.c`, `eeprom_cmds.c`)은 가짜 EEPROM 칩을 쓰는 PC 호스트 테스트(프로토콜 13개, 쉘 14개)를 통과합니다. DriverLib 버전은 보드에서 아직 확인하지 않았습니다.
- **확인 전**: `fill`, `test`, `status` 명령과 블록 보호 동작, FreeRTOS의 `uptime` 출력, SPI 10 MHz 동작(기본은 5 MHz).

## 관련 링크
- 상품 페이지: https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903127

## 프로세서 모듈

- [TMS320F28P650DK9 모듈(산업용)](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903200)
- [TMS320F28P659DK8-Q1 모듈(차량 전장용)](https://tms320f28x.co.kr/goods/goods_view.php?goodsNo=200903201)

## 개발 환경

CCS 21.x(Theia 기반) / C2000Ware 26.00.00.00

## Import 방법 (zip 직접 import도 지원)

압축을 미리 풀어서 "Select search-directory"로 지정하거나, GitHub에서 받은 zip 파일을
그대로 CCS의 "Select archive file"로 지정해도 됩니다.
