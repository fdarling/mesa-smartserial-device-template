/*
* Mesa SmartSerial (SSLBP) device template project
*
* Copyright (C) 2020 Forest Darling <fdarling@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "LBP.h"

#include <stdint.h>

// this code was based on the user fupeama's attachments on the following LinuxCNC forum post:
// https://forum.linuxcnc.org/27-driver-boards/34445-custom-board-for-smart-serial-interface?start=10#110007
// https://forum.linuxcnc.org/media/kunena/attachments/16679/sserial.h
// https://forum.linuxcnc.org/media/kunena/attachments/16679/sserial.c

#pragma pack(push,1)

static struct ProcessDataOut
{
  uint32_t input;
} pdata_out = {0x00000000};

static struct ProcessDataIn
{
  uint32_t output;
} pdata_in = {0x00000000};

static const char CARD_NAME[] = "cust"; // NOTE: LinuxCNC will force the
                                        // second character lowercase
                                        // stylize Mesa cards as 7i84
                                        // instead of 7I84, it's best
                                        // to use lowercase in the
                                        // first place...
static const uint32_t UNIT_NUMBER = 0x04030201;
static const uint16_t  GTOC_BASE_ADDRESS = 0x1000; // arbitrary, not real location in memory
static const uint16_t  PTOC_BASE_ADDRESS = 0x2000; // arbitrary, not real location in memory
static const uint16_t   PDD_BASE_ADDRESS = 0x3000; // arbitrary, not real location in memory
static const uint16_t PARAM_BASE_ADDRESS = 0x4000; // arbitrary, not real location in memory
static const LBP_Discovery_Data DISCOVERY_DATA =
{
  .RxSize = sizeof(ProcessDataOut)+1, // +1 for the fault status, remote transmits
  .TxSize = sizeof(ProcessDataIn), // remote receives
  .ptoc   = PTOC_BASE_ADDRESS,
  .gtoc   = GTOC_BASE_ADDRESS
};
static const LBP_PDD PDD[] =
{
  {
    .md = {
      .RecordType = LBP_PDD_RECORD_TYPE_MODE_DESCRIPTOR,
      .ModeIndex  = 0,
      .ModeType   = LBP_PDD_MODE_TYPE_HWMODE,
      ._unused    = 0,
      "Standard"
    }
  },
  {
    .md = {
      .RecordType = LBP_PDD_RECORD_TYPE_MODE_DESCRIPTOR,
      .ModeIndex  = 0,
      .ModeType   = LBP_PDD_MODE_TYPE_SWMODE,
      ._unused    = 0,
      "Input_Output"
    }
  },
  {
    .pdd = {
      .RecordType    = LBP_PDD_RECORD_TYPE_NORMAL,
      .DataSize      = 32,
      .DataType      = LBP_PDD_DATA_TYPE_BITS,
      .DataDirection = LBP_PDD_DIRECTION_OUTPUT,
      .ParamMin      = 0.0,
      .ParamMax      = 0.0,
      .ParamAddress  = PARAM_BASE_ADDRESS,
      "None\0Output"
    }
  },
  {
    .pdd = {
      .RecordType    = LBP_PDD_RECORD_TYPE_NORMAL,
      .DataSize      = 32,
      .DataType      = LBP_PDD_DATA_TYPE_BITS,
      .DataDirection = LBP_PDD_DIRECTION_INPUT,
      .ParamMin      = 0.0,
      .ParamMax      = 0.0,
      .ParamAddress  = PARAM_BASE_ADDRESS+4,
      "None\0Input"
    }
  }
};
static const uint16_t PTOC[] =
{
  PDD_BASE_ADDRESS+2*sizeof(LBP_PDD),
  PDD_BASE_ADDRESS+3*sizeof(LBP_PDD),
  0x0000
};
static const uint16_t GTOC[] =
{
  PDD_BASE_ADDRESS,
  PDD_BASE_ADDRESS+1*sizeof(LBP_PDD),
  PDD_BASE_ADDRESS+2*sizeof(LBP_PDD),
  PDD_BASE_ADDRESS+3*sizeof(LBP_PDD),
  0x0000
};
static const struct
{
  uint16_t base;
  uint16_t size;
  const void *data;
} VIRTUAL_MEMORY_MAP[] =
{
  {.base = GTOC_BASE_ADDRESS, .size = sizeof(GTOC), .data = GTOC},
  {.base = PTOC_BASE_ADDRESS, .size = sizeof(PTOC), .data = PTOC},
  {.base =  PDD_BASE_ADDRESS, .size = sizeof( PDD), .data = PDD}
};

#pragma pack(pop)

struct LBP_State
{
  uint16_t address;
} lbp_state =
{
  .address = 0x0000
};

//#define SHOW_DEBUG
//#define SHOW_VERBOSE
//#define SHOW_PDATA_IN

#ifdef SHOW_DEBUG
  #define DEBUG_PRINTF Serial.printf
  //#define DEBUG_PRINTF(f_, ...) do {Serial1.printf((f_), ##__VA_ARGS__);} while (0)
#else
  #define DEBUG_PRINTF(...)
#endif

#ifdef SHOW_VERBOSE
  #define VERB_PRINTF Serial.printf
#else
  #define VERB_PRINTF(...)
#endif

#if 0
#define SERIAL1_FLUSH Serial1.flush
#else
#define SERIAL1_FLUSH(x) do {} while (0)
#endif

void SERIAL1_WRITE(const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    VERB_PRINTF("Sending: 0x%02X\r\n", static_cast<uint32_t>(data[i]));
  }
  Serial1.write(data, len);
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600); // baudrate doesn't matter, full speed USB always
  Serial1.begin(2500000); // 2.5MBps for Mesa Smart Serial
}

void loop()
{
  if (Serial1.available())
  {
    LBP_Command cmd = {.value = static_cast<uint8_t>(Serial1.read())};
    VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
    uint8_t crc = LBP_CalcNextCRC(cmd.value);
    if (cmd.Generic.CommandType == LBP_COMMAND_TYPE_READ_WRITE)
    {
      VERB_PRINTF("GOT %s COMMAND! (DataSize = %i, AddressSize = %i, AutoInc = %i, RPCIncludesData = %i)\r\n",
                   cmd.ReadWrite.Write ? "WRITE" : "READ",
                   static_cast<uint32_t>(1 << cmd.ReadWrite.DataSize),
                   static_cast<uint32_t>(cmd.ReadWrite.AddressSize),
                   static_cast<uint32_t>(cmd.ReadWrite.AutoInc),
                   static_cast<uint32_t>(cmd.ReadWrite.RPCIncludesData));

      // possibly read 2-byte address
      if (cmd.ReadWrite.AddressSize)
      {
        union
        {
          uint16_t address;
          uint8_t bytes[2];
        } addr;
        
        // read LSB
        while (!Serial1.available()) {yield();}
        addr.bytes[0] = Serial1.read();
        crc = LBP_CalcNextCRC(addr.bytes[0], crc);
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(addr.bytes[0]));

        // read MSB
        while (!Serial1.available()) {yield();}
        addr.bytes[1] = Serial1.read();
        crc = LBP_CalcNextCRC(addr.bytes[1], crc);
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(addr.bytes[1]));

        lbp_state.address = addr.address;
      }
      
      if (cmd.ReadWrite.Write)
      {
        DEBUG_PRINTF("   ***UNHANDLED*** WRITE COMMAND: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        while (!Serial1.available()) {yield();}
        const uint8_t lastByte = Serial1.read();
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(lastByte));
        if (lastByte != crc)
        {
          DEBUG_PRINTF("<bad CRC>\r\n");
          return;
        }
      }
      else // (!cmd.ReadWrite.Write)
      {
        //DEBUG_PRINTF("specifically READ COMMAND: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        while (!Serial1.available()) {yield();}
        const uint8_t lastByte = Serial1.read();
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(lastByte));
        if (lastByte != crc)
        {
          DEBUG_PRINTF("<bad CRC>\r\n");
          return;
        }

        const uint8_t readLength = 1 << cmd.ReadWrite.DataSize;
        const void *src = NULL;
        for (size_t i = 0; i < sizeof(VIRTUAL_MEMORY_MAP)/sizeof(VIRTUAL_MEMORY_MAP[0]); i++)
        {
          if (lbp_state.address >= VIRTUAL_MEMORY_MAP[i].base && (lbp_state.address + readLength) <= (VIRTUAL_MEMORY_MAP[i].base + VIRTUAL_MEMORY_MAP[i].size))
          {
            src = reinterpret_cast<const uint8_t*>(VIRTUAL_MEMORY_MAP[i].data) + (lbp_state.address - VIRTUAL_MEMORY_MAP[i].base);
            break;
          }
        }
        if (!src)
        {
          DEBUG_PRINTF("<invalid read address 0x%04X>\r\n", static_cast<uint32_t>(lbp_state.address));
          return;
        }
        uint8_t RESPONSE[sizeof(uint64_t)+1];
        //VERB_PRINTF("<sending %i bytes as response>\r\n", readLength);
        memcpy(RESPONSE, src, readLength);
        RESPONSE[readLength] = LBP_CalcCRC(RESPONSE, readLength);
        SERIAL1_WRITE(RESPONSE, readLength+1);
        SERIAL1_FLUSH();
      }
      
    }
    else if (cmd.Generic.CommandType == LBP_COMMAND_TYPE_RPC)
    {
      uint8_t pdata_in_next[sizeof(pdata_in)];
      if (cmd.value == LBP_COMMAND_RPC_SMARTSERIAL_PROCESS_DATA)
      {
        for (size_t i = 0; i < sizeof(pdata_in); i++) // sizeof(pdata_in) == DISCOVERY_DATA.TxSize
        {
          while (!Serial1.available()) {yield();}
          const uint8_t c = Serial1.read();
          crc = LBP_CalcNextCRC(c, crc);
          VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(c));
          pdata_in_next[i] = c;
        }
      }
      while (!Serial1.available()) {yield();}
      const uint8_t lastByte = Serial1.read();
      //VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(lastByte));
      if (lastByte != crc)
      {
        DEBUG_PRINTF("<CRC bad>\r\n");
        return;
      }
      switch (cmd.value)
      {
        case LBP_COMMAND_RPC_SMARTSERIAL_RPC_DISCOVERY:
        {
          VERB_PRINTF("got LBP_COMMAND_RPC_SMARTSERIAL_RPC_DISCOVERY\r\n");
          uint8_t RESPONSE[sizeof(DISCOVERY_DATA)+1];
          memcpy(RESPONSE, &DISCOVERY_DATA, sizeof(DISCOVERY_DATA));
          RESPONSE[sizeof(RESPONSE)-1] = LBP_CalcCRC(RESPONSE, sizeof(RESPONSE)-1);
          SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
          SERIAL1_FLUSH();
        }
        break;
        
        case LBP_COMMAND_RPC_SMARTSERIAL_UNIT_NUMBER:
        {
          VERB_PRINTF("got LBP_COMMAND_RPC_SMARTSERIAL_UNIT_NUMBER\r\n");
          uint8_t RESPONSE[sizeof(UNIT_NUMBER)+1];
          memcpy(RESPONSE, &UNIT_NUMBER, sizeof(UNIT_NUMBER));
          RESPONSE[sizeof(RESPONSE)-1] = LBP_CalcCRC(RESPONSE, sizeof(RESPONSE)-1);
          SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
          SERIAL1_FLUSH();
        }
        break;

        case LBP_COMMAND_RPC_SMARTSERIAL_PROCESS_DATA:
        {
          VERB_PRINTF("got LBP_COMMAND_RPC_SMARTSERIAL_PROCESS_DATA\r\n");
          uint8_t RESPONSE[DISCOVERY_DATA.RxSize+1]; // +1 for CRC
          RESPONSE[0] = 0x00; // fault status
          pdata_out.input = millis();
          memcpy(RESPONSE+1, &pdata_out, sizeof(pdata_out)); // +1 for skipping fault status
          RESPONSE[sizeof(RESPONSE)-1] = LBP_CalcCRC(RESPONSE, sizeof(RESPONSE)-1);
          SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
          SERIAL1_FLUSH();

          memcpy(&pdata_in, pdata_in_next, sizeof(pdata_in));
#ifdef SHOW_PDATA_IN
          Serial.printf("P: 0x%08X, %i\r\n", pdata_in.output, Serial.available());
#endif
          // TODO actually write outputs!

          // show the process data activity
          static uint8_t cnt = 0;
          cnt++;
          digitalWriteFast(LED_BUILTIN, (millis() & 0x100) ? HIGH : LOW);
        }
        break;
        
        default:
        DEBUG_PRINTF("   ***UNHANDLED*** LBP_COMMAND_TYPE_RPC: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
      }
    }
    else if (cmd.Generic.CommandType == LBP_COMMAND_TYPE_LOCAL_READ_WRITE)
    {
      if (cmd.value >= 0xE0) // HACK check if it's a write command
      {
        //VERB_PRINTF("GOT LOCAL LBP WRITE COMMAND! 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        uint8_t param = 0;
        if (cmd.value != LBP_COMMAND_LOCAL_WRITE_RESET_LBP_PARSE)
        {
          // skip parameter byte for now
          while (!Serial1.available()) {yield();}
          param = static_cast<uint8_t>(Serial1.read());
          VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(param));
          crc = LBP_CalcNextCRC(param, crc);
        }
  
        while (!Serial1.available()) {yield();}
        const uint8_t lastByte = Serial1.read();
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(lastByte));
        if (lastByte != crc)
        {
          DEBUG_PRINTF("<CRC bad>\r\n");
          return;
        }
  
        // act
        switch (cmd.value)
        {
          case LBP_COMMAND_LOCAL_WRITE_STATUS:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_WRITE_STATUS: 0x%02X\r\n", static_cast<uint32_t>(param));
            const uint8_t RESPONSE[] = {LBP_CalcNextCRC(0x00)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;

          case LBP_COMMAND_LOCAL_WRITE_SW_MODE:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_WRITE_SW_MODE: 0x%02X\r\n", static_cast<uint32_t>(param));
            const uint8_t RESPONSE[] = {LBP_CalcNextCRC(0x00)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;

          case LBP_COMMAND_LOCAL_WRITE_CLEAR_FAULTS:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_WRITE_CLEAR_FAULTS: 0x%02X\r\n", static_cast<uint32_t>(param));
            const uint8_t RESPONSE[] = {LBP_CalcNextCRC(0x00)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;
          
          case LBP_COMMAND_LOCAL_WRITE_COMMAND_TIMEOUT:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_WRITE_COMMAND_TIMEOUT: 0x%02X\r\n", static_cast<uint32_t>(param));
            const uint8_t RESPONSE[] = {LBP_CalcNextCRC(0x00)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;
    
          default:
          DEBUG_PRINTF("   ***UNHANDLED*** LOCAL LBP WRITE COMMAND: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        }
      }
      else // if (cmd.value < 0xE0)
      {
        //VERB_PRINTF("GOT LOCAL LBP READ COMMAND! 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        while (!Serial1.available()) {yield();}
        const uint8_t lastByte = Serial1.read();
        VERB_PRINTF("Received: 0x%02X\r\n", static_cast<uint32_t>(lastByte));
        if (lastByte != crc)
        {
          DEBUG_PRINTF("<CRC bad>\r\n");
          return;
        }
  
        // respond
        switch (cmd.value)
        {
          case LBP_COMMAND_LOCAL_READ_LBP_STATUS:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_READ_LBP_STATUS\r\n");
            const uint8_t lbp_status = 0x00;
            const uint8_t RESPONSE[] = {lbp_status, LBP_CalcNextCRC(lbp_status)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;

          case LBP_COMMAND_LOCAL_READ_CLEAR_FAULT_FLAG:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_READ_CLEAR_FAULT_FLAG\r\n");
            const uint8_t fault_flag = 0x00;
            const uint8_t RESPONSE[] = {fault_flag, LBP_CalcNextCRC(fault_flag)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;

          case LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR0:
          case LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR1:
          case LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR2:
          case LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR3:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR%i\r\n", static_cast<uint32_t>(cmd.value - LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR0));
            uint8_t RESPONSE[] = {CARD_NAME[cmd.value - LBP_COMMAND_LOCAL_READ_CARD_NAME_CHAR0], 0x00};
            RESPONSE[sizeof(RESPONSE)-1] = LBP_CalcCRC(RESPONSE, sizeof(RESPONSE)-1);
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;

          case LBP_COMMAND_LOCAL_READ_FAULT_DATA:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_READ_FAULT_DATA\r\n");
            const uint8_t fault_data = 0x00;
            const uint8_t RESPONSE[] = {fault_data, LBP_CalcNextCRC(fault_data)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;
          
          case LBP_COMMAND_LOCAL_READ_COOKIE:
          {
            VERB_PRINTF("got LBP_COMMAND_LOCAL_READ_COOKIE\r\n");
            const uint8_t RESPONSE[] = {LBP_COOKIE, LBP_CalcNextCRC(LBP_COOKIE)};
            SERIAL1_WRITE(RESPONSE, sizeof(RESPONSE));
            SERIAL1_FLUSH();
          }
          break;
    
          default:
          DEBUG_PRINTF("   ***UNHANDLED*** LOCAL LBP READ COMMAND: 0x%02X\r\n", static_cast<uint32_t>(cmd.value));
        }
      }
    }
    else
    {
      DEBUG_PRINTF("unknown command %02X\r\n", static_cast<uint32_t>(cmd.value));
    }
  }
}
