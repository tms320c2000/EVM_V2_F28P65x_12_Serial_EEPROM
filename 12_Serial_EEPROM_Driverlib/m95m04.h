//#############################################################################
//
// FILE:   m95m04.h
//
// TITLE:  ST M95M04-DR (4 Mbit = 512 KB, SPI EEPROM) 프로토콜 계층
//
// 이 파일과 m95m04.c는 세 버전(DriverLib / 비트필드 / FreeRTOS)에서 같습니다. SPI 바이트 교환, 칩 선택(CS),
// 대기 지연만 버전별 HAL(m95m04_hal.c)이 구현합니다. 그래서 PC에서 가짜 칩으로 테스트할 수 있습니다
// (test/host/test_m95m04.c).
//
// 칩 요약 (M95M04-DR): 512KB, 페이지 512바이트(1024페이지), 3바이트 주소(19비트 사용), SPI 모드 0/3,
// 클럭 최대 10MHz, 쓰기 사이클 5ms 이내. 명령: WREN 0x06, WRITE 0x02, READ 0x03, RDSR 0x05.
//
//#############################################################################

#ifndef M95M04_H
#define M95M04_H

#include <stdint.h>

// 8비트 값. C28x에서는 char가 16비트라 하나의 char에 0..255 값을 한 개씩 담는다(FatFs의 BYTE와 같은 방식).
typedef unsigned char m95_byte;

#define M95M04_SIZE_BYTES   524288UL    // 4 Mbit
#define M95M04_PAGE_SIZE    512U        // 1024 pages

// 상태 레지스터(RDSR) 비트
#define M95M04_SR_WIP       0x01U       // 쓰기 진행 중
#define M95M04_SR_WEL       0x02U       // 쓰기 허용 래치
#define M95M04_SR_BP_MASK   0x0CU       // 블록 보호 비트(BP1:BP0). 0이 아니면 일부/전체가 쓰기 금지
#define M95M04_SR_SRWD      0x80U       // 상태 레지스터 쓰기 금지 비트

typedef enum
{
    M95_OK = 0,
    M95_ERR_RANGE,      // 주소/길이가 칩 용량을 벗어남(아무것도 쓰지 않음)
    M95_ERR_TIMEOUT,    // 쓰기 완료(WIP 해제)를 한계 시간 안에 못 봄
    M95_ERR_VERIFY,     // 읽어 비교한 값이 다름
    M95_ERR_PARAM       // NULL 버퍼 등 잘못된 인자
} m95_status_t;

//
// HAL(하드웨어 추상 계층): 각 예제의 m95m04_hal.c가 구현한다.
//
void     M95M04_hal_init(void);                 // SPI/GPIO 초기화(CS는 High=비선택으로 시작)
void     M95M04_hal_select(void);               // CS2를 Low로
void     M95M04_hal_deselect(void);             // CS2를 High로
m95_byte M95M04_hal_transfer(m95_byte out);     // SPI 한 바이트를 보내고 동시에 받은 바이트를 돌려준다
uint32_t M95M04_hal_waitStepUs(void);           // 쓰기 완료를 기다리는 한 단계: 잠깐 쉬고, 쉰 시간(µs)을 돌려준다

//
// 공개 함수(API)
//
void         M95M04_init(void);
m95_status_t M95M04_readStatus(m95_byte *sr);
m95_status_t M95M04_read(uint32_t addr, m95_byte *buf, uint32_t len);
m95_status_t M95M04_write(uint32_t addr, const m95_byte *buf, uint32_t len);   // 페이지 경계에서 나눠 쓴다
m95_status_t M95M04_fill(uint32_t addr, m95_byte value, uint32_t len);          // 정적 512바이트 버퍼 사용(재진입 불가)
m95_status_t M95M04_verify(uint32_t addr, const m95_byte *expected, uint32_t len, uint32_t *firstMismatch);

#endif // M95M04_H
