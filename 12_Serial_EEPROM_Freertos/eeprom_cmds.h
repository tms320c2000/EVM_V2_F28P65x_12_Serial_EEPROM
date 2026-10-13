//#############################################################################
//
// FILE:   eeprom_cmds.h
//
// TITLE:  EEPROM 대화형 콘솔 명령어 — 세 버전(DriverLib / 비트필드 / FreeRTOS)에서 같은 파일
//
//#############################################################################

#ifndef EEPROM_CMDS_H
#define EEPROM_CMDS_H

// 환영 메시지를 출력한다(variant: "DriverLib" / "Bitfield" / "FreeRTOS").
void EepromConsole_banner(const char *variant);

// 명령어 입력 루프(돌아오지 않는다). UARTgets로 한 줄을 받아 CmdLineProcess로 실행한다.
void EepromConsole_run(void);

#endif // EEPROM_CMDS_H
