// CAN MITM module between RD45 and the rest of the Peugeot 308 (2007) car
// 125kbps CAN-INFO no termination resistor on MCP2515 SPI modules => no jumpers
// pinout https://frenchcarforum.co.uk/forum/viewtopic.php?t=65388
// CAN0 connected to RD45
// CAN1 connected to rest of CAN-INFO

#define SEND_FAKE_BSI_TO_RADIO
// #define SEND_FAKE_BSI_TO_EMF

// decimation settings
// skip COUNT of messages each time and then print only
#define SKIP_0E6_COUNT  1
// #define SKIP_036_COUNT 50 // not used
#define SKIP_0F6_COUNT  1


#include <mcp_can.h>

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];

// RD45 radio
#define CAN0_CS  10 // Set CS  to pin 10
#define CAN0_INT  2 // Set INT to pin  2

// rest of the car
#define CAN1_CS   9 // Set CS  to pin  9
#define CAN1_INT  3 // Set INT to pin  3

MCP_CAN CAN0(CAN0_CS);
MCP_CAN CAN1(CAN1_CS);

uint8_t data[8];
uint8_t ambient_temperature = 0x63; // 10 degrees of Celsius init value

uint8_t longRDS[256]; // CDC "0 whatever string \n" -> " whatever string " in 1st line
uint8_t longRDSpointer = 0; // text rotation

uint8_t longRDTXT[256]; // CDC "1 whatever string \n" -> " whatever string " in 2nd line
uint8_t longRDTXTpointer = 0; // text rotation

uint8_t fakeSCR[256]; // CDC "21" -> enable fake strings on screen, CDC "20" -> disable

uint8_t str_time_sync[256]; // CDC "31" -> starts time sync sequence

char MsgString[256]; // serial string
char str_tmp0[12]; // used for dtostr float

uint32_t now_millis; // to do not ask millis()too often

uint32_t Timer100_every_ms =  150; // how often the Timer is triggered
uint32_t next_Timer100_check; // computed value when the Timer is triggered next time

uint32_t Timer500_every_ms =  500;
uint32_t next_Timer500_check;

uint32_t Timer1000_every_ms = 1000;
uint32_t next_Timer1000_check;

uint32_t Timer600_every_ms = 1000; // RDS text field scrooling
uint32_t next_Timer600_check;

uint32_t Timer300_every_ms = 500; // RDTXT text field scrooling
uint32_t next_Timer300_check;



void setup() {
  pinMode(CAN0_INT, INPUT);
  pinMode(CAN1_INT, INPUT);
  while (CAN0.begin(MCP_STDEXT, CAN_125KBPS, MCP_8MHZ) != CAN_OK) { // blink TX LED until succesfull 1st CAN module init
    TXLED1;
    delay(300);
    TXLED0;
    delay(300);
  }
  while (CAN1.begin(MCP_STDEXT, CAN_125KBPS, MCP_8MHZ) != CAN_OK) { // blink RX LED until succesfull 2nd CAN module init
    RXLED1;
    delay(300);
    RXLED0;
    delay(300);
  }
  Serial.begin(230400); // be a bit faster than CAN 125kbps...

  /*
    CAN0.init_Mask(0, 0, 0x7FF0000);
    CAN0.init_Mask(1, 0, 0x7FF0000);
    CAN0.init_Filt(0, 0, 0x21F0000); // RC under seering wheel
    CAN0.init_Filt(1, 0, 0x1310000); // CDC command
    CAN0.init_Filt(2, 0, 0x0E60000); // Voltage
    CAN0.init_Filt(3, 0, 0x0F60000); // Temperature
  */
  CAN0.setMode(MCP_NORMAL);
  CAN1.setMode(MCP_NORMAL);
  sprintf(longRDS, "CDC MSG");
  sprintf(longRDTXT, " CDC TXT CDC TXT CDC TXT CDC TXT ");
  sprintf(fakeSCR, "0");

  now_millis = millis();
  next_Timer100_check  = now_millis +  0;
  next_Timer500_check  = now_millis + 20;
  next_Timer1000_check = now_millis + 40;
  next_Timer600_check  = now_millis + 60;
  next_Timer300_check  = now_millis + 80;
}

uint8_t MITM = 0; // wheter we are doing MITM fake messages
uint8_t CDCRxMsg = 0; // type of message: 1st string, 2nd string, enable/disable "fake" screen, time sync
uint8_t CDCRxPointer = 0; // pointer of incoming message


uint8_t counter0F6 = 0; // not every received message should be printed
uint8_t counter036 = 0;
uint8_t counter0E6 = 0;

int  clock_sequence = 0;


void BTN_NONE(void) {
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;
  CAN1.sendMsgBuf(0x3E5, 0, 6, data);
}
void BTN_MENU(void) {
  data[0] = 0x40;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;
  CAN1.sendMsgBuf(0x3E5, 0, 6, data);
}
void BTN_OK(void) {
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x40;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;
  CAN1.sendMsgBuf(0x3E5, 0, 6, data);
}
void BTN_UP(void) {
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x40;
  CAN1.sendMsgBuf(0x3E5, 0, 6, data);
}
void BTN_DOWN(void) {
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x10;
  CAN1.sendMsgBuf(0x3E5, 0, 6, data);
}

void loop() {

  now_millis = millis();

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  while (Serial.available()) {
    uint8_t ch = Serial.read();

    switch (ch) {
      case '0':
        if (CDCRxPointer == 0) { // RDS
          CDCRxMsg = 0;
        } else {
          if (CDCRxMsg == 0) {
            longRDS[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 1) {
            longRDTXT[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 2) {
            fakeSCR[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 3) {
            str_time_sync[CDCRxPointer - 1] = ch; // character
          }
        }
        if (CDCRxPointer < 254) {
          CDCRxPointer ++;
        }
        break;
      case '1':
        if (CDCRxPointer == 0) { // RDTXT
          CDCRxMsg = 1;
        } else {
          if (CDCRxMsg == 0) {
            longRDS[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 1) {
            longRDTXT[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 2) {
            fakeSCR[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 3) {
            str_time_sync[CDCRxPointer - 1] = ch; // character
          }
        }
        if (CDCRxPointer < 254) {
          CDCRxPointer ++;
        }
        break;
      case '2':
        if (CDCRxPointer == 0) { // fake screen #
          CDCRxMsg = 2;
        } else {
          if (CDCRxMsg == 0) {
            longRDS[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 1) {
            longRDTXT[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 2) {
            fakeSCR[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 3) {
            str_time_sync[CDCRxPointer - 1] = ch; // character
          }
        }
        if (CDCRxPointer < 254) {
          CDCRxPointer ++;
        }
        break;
      case '3':
        if (CDCRxPointer == 0) { // time sync
          CDCRxMsg = 3;
        } else {
          if (CDCRxMsg == 0) {
            longRDS[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 1) {
            longRDTXT[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 2) {
            fakeSCR[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 3) {
            str_time_sync[CDCRxPointer - 1] = ch; // character
          }
        }
        if (CDCRxPointer < 254) {
          CDCRxPointer ++;
        }
        break;
      case '\n': // 0x0A
        if (CDCRxMsg == 0) {
          longRDS[CDCRxPointer - 1] = 0x00; // string 0
        }
        if (CDCRxMsg == 1) {
          longRDTXT[CDCRxPointer - 1] = 0x00; // string 0
        }
        if (CDCRxMsg == 2) {
          fakeSCR[CDCRxPointer - 1] = 0x00; // string 0
        }
        if (CDCRxMsg == 3) {
          str_time_sync[CDCRxPointer - 1] = 0x00; // string 0
          clock_sequence = 1;
        }
        CDCRxPointer = 0;
        break;
      default:
        if ( (CDCRxPointer > 0) && ( ch != '\r' ) ) {
          if (CDCRxMsg == 0) {
            longRDS[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 1) {
            longRDTXT[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 2) {
            fakeSCR[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxMsg == 3) {
            str_time_sync[CDCRxPointer - 1] = ch; // character
          }
          if (CDCRxPointer < 254) {
            CDCRxPointer ++;
          }
        }
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (now_millis >= next_Timer100_check) {
    next_Timer100_check += Timer100_every_ms;

    // BSI 036 morcibacsi uint8_t data1[] = { 0x0E, 0x00, 0x05, 0x2F, 0x21, 0x80, 0x00, 0xA0 };
    // BSI 036 ignition
    data[0] = 0x0E; // const
    data[1] = 0x00; // const
    data[2] = 0x00; // 7 - economy mode
    data[3] = 0b00001111; // 5 dashboard backlight, 3-0 EMF backlight
    data[4] = 0b00000001; // ignition: 0b001 radio + EMF, 0b100 radio only
    data[5] = 0x00; // const
    data[6] = 0x00; // const
    data[7] = 0xA0; // const
#ifdef SEND_FAKE_BSI_TO_RADIO
    CAN0.sendMsgBuf(0x036, 0, 8, data);
#endif // #ifdef SEND_FAKE_BSI_TO_RADIO
#ifdef SEND_FAKE_BSI_TO_EMF
    CAN1.sendMsgBuf(0x036, 0, 8, data);
#endif // #ifdef SEND_FAKE_BSI_TO_EMF
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (now_millis >= next_Timer500_check) {
    next_Timer500_check += Timer500_every_ms;

    // BSI 0F6 morcibacsi uint8_t data2[] = { 0x08, 0x32, 0x00, 0x1F, 0x00, 0x0D, 0x40, 0x01 };
    // BSI 0F6 ignition, coolant temperature, odometer, unknown ambient temperature, ambient temperature on EMF, direction lights
    data[0] = 0x08; // ignition
    data[1] = 0x76; // coolant temp
    data[2] = 0x16; // odom
    data[3] = 0xA6; // odom
    data[4] = 0xEC; // odom
    data[5] = 0x63; // ??????? temperature 10 degrees of Celsius
    data[6] = ambient_temperature; // ambient temperature 10 degrees of Celsius on EMF
    data[7] = 0x00; // directions light
#ifdef SEND_FAKE_BSI_TO_RADIO
    CAN0.sendMsgBuf(0x0F6, 0, 8, data);
#endif // #ifdef SEND_FAKE_BSI_TO_RADIO
#ifdef SEND_FAKE_BSI_TO_EMF
    CAN1.sendMsgBuf(0x0F6, 0, 8, data);
#endif // #ifdef SEND_FAKE_BSI_TO_EMF

    // sequence for button pressing EMF's time and date sync
    if (now_millis > 4000) {
      if (clock_sequence > 0) {
        switch (clock_sequence) {
          case 1:
            BTN_MENU();
            break;
          case 2:
            BTN_NONE();
            break;
          case 3:
            BTN_OK();
            break;
          case 4:
            BTN_NONE();
            break;
          case 5:
            BTN_OK();
            break;
          case 6:
            BTN_NONE();
            break;
          case 7:
            BTN_OK();
            break;
          case 8:
            BTN_NONE();
            break;

          case 9:
            BTN_DOWN();
            break;
          case 10:
            BTN_NONE();
            break;
          case 11:
            BTN_DOWN();
            break;
          case 12:
            BTN_NONE();
            break;
          case 13:
            BTN_DOWN();
            break;
          case 14:
            BTN_NONE();
            break;
          case 15:
            BTN_DOWN();
            break;
          case 16:
            BTN_NONE();
            break;


          case 17:
            BTN_OK();
            break;
          case 18:
            BTN_NONE();
            break;

          case 19:
            BTN_UP();
            break;
          case 20:
            BTN_NONE();
            break;

          case 21:
            BTN_OK();
            break;
          case 22:
            BTN_NONE();
            break;

          case 23:
            BTN_DOWN();
            break;
          case 24:
            BTN_NONE();
            break;
          case 25:
            BTN_DOWN();
            break;
          case 26:
            BTN_NONE();
            break;

          case 27:
            BTN_OK();
            break;
          case 28:
            BTN_NONE();
            break;

        }
        if (clock_sequence >= 28) {
          clock_sequence = 0;
        } else {
          clock_sequence ++;
        }
      }
    }
  }


  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (now_millis >= next_Timer1000_check) {
    next_Timer1000_check += Timer1000_every_ms;

    // 55094175
    // BSI 2B6 10 - 17 ASCII VIN // anti theft // 35 35 30 39 34 31 37 35
    data[0] = 0x35;
    data[1] = 0x35;
    data[2] = 0x30;
    data[3] = 0x39;
    data[4] = 0x34;
    data[5] = 0x31;
    data[6] = 0x37;
    data[7] = 0x35;
#ifdef SEND_FAKE_BSI_TO_RADIO
    CAN0.sendMsgBuf(0x2B6, 0, 8, data);
#endif // #ifdef SEND_FAKE_BSI_TO_RADIO
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (now_millis >= next_Timer600_check) {
    next_Timer600_check += Timer600_every_ms;

    if (MITM == 1) {
      int longRDSlength = 0;
      while ( (longRDS[longRDSlength] != 0) && (longRDSlength < 255) ) {
        longRDSlength ++;
      }

      uint8_t endfound = 0;
      for (uint8_t i = 0; i < 8; i ++) {
        if ( (longRDS[longRDSpointer + i] != 0) && (endfound == 0) ) {
          data[i] = longRDS[longRDSpointer + i];
        } else {
          data[i] = ' ';
          endfound = 1;
        }
      }

      CAN1.sendMsgBuf(0x2A5, 0, 8, data);
      if (longRDSlength > 8) {
        longRDSpointer = (longRDSpointer + 1) % (longRDSlength - 7);
      } else {
        longRDSpointer = 0;
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (now_millis >= next_Timer300_check) {
    next_Timer300_check += Timer300_every_ms;

    if (MITM == 1) {
      int longRDTXTlength = 0;
      while ( (longRDTXT[longRDTXTlength] != 0) && (longRDTXTlength < 255) ) {
        longRDTXTlength ++;
      }

      data[0] = 0x10; // header CAN TP First frame 6 bytes
      data[1] = 27; // header # bytes in payload
      data[2] = 0x10; // 0x10 RDTXT, 0x20 CDTXT
      data[3] = 0x00;
      data[4] = 0x58;
      data[5] = 0x00;
      data[6] = longRDTXT[longRDTXTpointer +  0];
      data[7] = longRDTXT[longRDTXTpointer +  1];
      CAN1.sendMsgBuf(0x0A4, 0, 8, data);
      delay(5);

      data[0] = 0x21; // CAN TP Consecutive frame 7 bytes
      data[1] = longRDTXT[longRDTXTpointer +  2];
      data[2] = longRDTXT[longRDTXTpointer +  3];
      data[3] = longRDTXT[longRDTXTpointer +  4];
      data[4] = longRDTXT[longRDTXTpointer +  5];
      data[5] = longRDTXT[longRDTXTpointer +  6];
      data[6] = longRDTXT[longRDTXTpointer +  7];
      data[7] = longRDTXT[longRDTXTpointer +  8];
      CAN1.sendMsgBuf(0x0A4, 0, 8, data);
      delay(5);

      data[0] = 0x22; // CAN TP Consecutive frame 7 bytes
      data[1] = longRDTXT[longRDTXTpointer +  9];
      data[2] = longRDTXT[longRDTXTpointer + 10];
      data[3] = longRDTXT[longRDTXTpointer + 11];
      data[4] = longRDTXT[longRDTXTpointer + 12];
      data[5] = longRDTXT[longRDTXTpointer + 13];
      data[6] = longRDTXT[longRDTXTpointer + 14];
      data[7] = longRDTXT[longRDTXTpointer + 15];
      CAN1.sendMsgBuf(0x0A4, 0, 8, data);
      delay(5);

      data[0] = 0x23; // CAN TP Consecutive frame 7 bytes
      data[1] = longRDTXT[longRDTXTpointer + 16];
      data[2] = longRDTXT[longRDTXTpointer + 17];
      data[3] = longRDTXT[longRDTXTpointer + 18];
      data[4] = ' ';//longRDTXT[longRDTXTpointer + 19];
      data[5] = ' ';
      data[6] = ' ';
      data[7] = ' ';
      CAN1.sendMsgBuf(0x0A4, 0, 8, data);
      delay(5);
      if (longRDTXTlength > 18) {
        longRDTXTpointer = (longRDTXTpointer + 1) % (longRDTXTlength - 17);
      } else {
        longRDTXTpointer = 0;
      }
    }
  }



  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


  if (!digitalRead(CAN0_INT)) {               // If CAN0_INT pin is low, read receive buffer
    CAN0.readMsgBuf(&rxId, &len, rxBuf);      // Read data: len = data length, buf = data byte(s)
    switch (rxId) {
      case 0x000:
        break;

      case 0x0A4: // CAN-TP
        if (MITM == 0) {
          CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        }
        break;

      case 0x165: // EMF main screen state
        if ((fakeSCR[0] - 0x30) > 0) { // ASCII conversion "0" -> no MITM, whatever > 0 -> MITM
          if (MITM == 0) {
            longRDSpointer = 0;
            longRDTXTpointer = 0;
          }
          MITM = 1;
        } else {
          MITM = 0;
        }
        if (MITM == 1) { // faking/abusing Tuner screen
          rxBuf[0] = 0xC0;
          rxBuf[1] = 0xC8;
          rxBuf[2] = 0x10;
          rxBuf[3] = 0x00;
        }
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        break;

      case 0x1E0:
        if (MITM == 1) {
          rxBuf[4] &= 0b11011111;
        }
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        break;

      case 0x225:
        if (MITM == 1) { // no BAND icon and no frequency string
          rxBuf[0] = 0x20;
          rxBuf[1] = 0x00;
          rxBuf[2] = 0x00;
          rxBuf[3] = 0x00;
          rxBuf[4] = 0x00;
        }
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        break;

      case 0x265:
        if (MITM == 1) { // no RDS, no PTY strings
          rxBuf[0] = 0x00;
          rxBuf[1] = 0x00;
          rxBuf[2] = 0x00;
          rxBuf[3] = 0x00;
        }
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        break;

      case 0x2A5:
        if (MITM == 0) {
          CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        }
        break;

      case 0x420: // Radio power button don't send
        //CAN1.sendMsgBuf(rxId, 0, len, rxBuf);
        break;

      // by-pass the rest
      case 0x123: // RD45 CAN-TP flow ctrl 0x29F "BT Phone" "0x20 0xC0" ->"Orange SK" 0x41 "phone BT name"
      case 0x125:
      case 0x131:
      case 0x1A3: // RD45 200ms
      case 0x1A5:
      case 0x1E5:
      case 0x2E3: // RD45 CAN-TP flow ctrl 0x15F "Bluetooth"
      case 0x323: // RD45 1000ms
      case 0x325:
      case 0x363: // RD45 1000ms
      case 0x365:
      case 0x3A5:
      case 0x3E5:
      case 0x4A0:
      case 0x520:
      case 0x5E0:
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        break;

      default:
        CAN1.sendMsgBuf(rxId, 0, len, rxBuf); // from RADIO to ALL
        dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
        sprintf(MsgString, "%s RAD --> %.3lX %d", str_tmp0, rxId, len);
        Serial.print(MsgString);
        for (byte i = 0; i < len; i ++) {
          Serial.print(", ");
          sprintf(MsgString, "%.2X", rxBuf[i]);
          Serial.print(MsgString);
        }
        Serial.println();
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  if (!digitalRead(CAN1_INT)) {               // If CAN1_INT pin is low, read receive buffer
    CAN1.readMsgBuf(&rxId, &len, rxBuf);      // Read data: len = data length, buf = data byte(s)
    if ( (now_millis  < (next_Timer100_check - 100)) || (rxId == 0x21F) ) { // allowing critical Timer100 and drop ALL to RADIO ... or making the necessary...
      switch (rxId) {
        case 0x000:
          break;

        case 0x0E6: // receive only
          counter0E6 ++;
          if (counter0E6 >= SKIP_0E6_COUNT) {
            counter0E6 = 0;
            dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
            sprintf(MsgString, "%s %.3lX %d", str_tmp0, rxId, len);
            //sprintf(MsgString, "%s RAD %s EMF %.3lX %d", str_tmp0, str_tmp1, rxId, len);
            float voltage = 0.05 * ((uint16_t)rxBuf[5] + 144);
            dtostrf(voltage, 5, 2, str_tmp0);
            sprintf(MsgString, "%s, BatV = %s", MsgString, str_tmp0);
            Serial.println(MsgString);
          }
          break;

        case 0x0F6: // receive only
          counter0F6 ++;
          if (counter0F6 >= SKIP_0F6_COUNT) {
            counter0F6 = 0;
            dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
            sprintf(MsgString, "%s %.3lX %d", str_tmp0, rxId, len);
            //sprintf(MsgString, "%s RAD %s EMF %.3lX %d", str_tmp0, str_tmp1, rxId, len);
            if (rxBuf[0] & 0b10001000) {
              sprintf(MsgString, "%s, IG_1", MsgString);
            } else {
              sprintf(MsgString, "%s, IG_0", MsgString);
            }
            if (rxBuf[7] & 0b10000000) {
              sprintf(MsgString, "%s, RV_1", MsgString);
            } else {
              sprintf(MsgString, "%s, RV_0", MsgString);
            }
            if (rxBuf[7] & 0b00000001) {
              sprintf(MsgString, "%s, TR_1", MsgString);
            } else {
              sprintf(MsgString, "%s, TR_0", MsgString);
            }
            if (rxBuf[7] & 0b00000010) {
              sprintf(MsgString, "%s, TL_1", MsgString);
            } else {
              sprintf(MsgString, "%s, TL_0", MsgString);
            }

            int8_t coolant = (int8_t)((uint8_t)rxBuf[1] - 39);
            sprintf(MsgString, "%s, Cool = %d", MsgString, coolant);

            uint32_t odometer = (uint32_t)((uint32_t)rxBuf[2] << 16 | (uint32_t)rxBuf[3] << 8 | (uint32_t)rxBuf[4]);
            dtostrf(0.1 * odometer, 8, 1, str_tmp0);
            sprintf(MsgString, "%s, Odom = %s", MsgString, str_tmp0);

            ambient_temperature = rxBuf[6];

            float temperature = 0.5 * ((uint8_t)rxBuf[6] - 79);
            dtostrf(temperature, 5, 1, str_tmp0);
            sprintf(MsgString, "%s, Temp = %s", MsgString, str_tmp0);

            Serial.println(MsgString);
          }
          break;

        case 0x21F: // RC under steering wheel
          CAN0.sendMsgBuf(rxId, 0, len, rxBuf); // from ALL to RADIO
          dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
          sprintf(MsgString, "%s %.3lX %d", str_tmp0, rxId, len);
          if (rxBuf[0] & 0b11101110) {
            if (rxBuf[0] & 0b10000000) {
              sprintf(MsgString, "%s, Forward", MsgString);
            }
            if (rxBuf[0] & 0b01000000) {
              sprintf(MsgString, "%s, Backward", MsgString);
            }
            if (rxBuf[0] & 0b00100000) {
              sprintf(MsgString, "%s, Unknown", MsgString);
            }
            // 0
            if (rxBuf[0] & 0b00001000) {
              sprintf(MsgString, "%s, Volume up", MsgString);
            }
            if (rxBuf[0] & 0b00000100) {
              sprintf(MsgString, "%s, Volume down", MsgString);
            }
            if (rxBuf[0] & 0b00000010) {
              sprintf(MsgString, "%s, Source", MsgString);
            }
            // 0
          }
          Serial.println(MsgString);
          break;
        /*
              case 0x036: // receive only, print and drop, we are master here
                counter036 ++;
                if (counter036 >= SKIP_036_COUNT) {
                  counter036 = 0;
                  dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
                  sprintf(MsgString, "%s %.3lX %d", str_tmp0, rxId, len);
                  if (rxBuf[4] & 0b00000001) {
                    sprintf(MsgString, "%s, IGN_1", MsgString);
                  } else {
                    sprintf(MsgString, "%s, IGN_0", MsgString);
                  }
                  Serial.println(MsgString);
                }
                break;
        */

        // drop
        case 0x036: // drop Ignition state, we are master here
        case 0x2B6: // drop BSI last 8 VIN
          break;

        // by-pass the rest
        case 0x018:
        case 0x09F: // flow ctrl 0x0A4
        case 0x0B6:
        case 0x0DF: // autowp display menu
        // 3, 90, 00, 70 icon menu
        case 0x10C:
        case 0x110:
        case 0x11F:
        case 0x120:
        case 0x128:
        case 0x12D:
        case 0x15B: // autowp display???
        case 0x15F: // new EMF flow ctrl for 0x2E3 "Bluetooth"
        case 0x161:
        case 0x167: // autowp display status
        case 0x168:
        case 0x18C:
        case 0x190:
        case 0x1A1:
        case 0x1A8:
        case 0x1CC:
        case 0x1D0:
        case 0x1DF: // new EMF 200ms
        case 0x217:
        //case 0x21F: // RC under steering wheel
        case 0x220:
        case 0x221:
        case 0x227:
        case 0x24C:
        case 0x257:
        case 0x260:
        case 0x261:
        case 0x28C:
        case 0x295:
        case 0x29F: // new EMF flow ctrl for 0x123 "BT Phone"
        case 0x2A0:
        case 0x2A1:
        case 0x2E1:
        case 0x317:
        case 0x336:
        case 0x361:
        case 0x3A7:
        case 0x3B6:
        case 0x3F6: // autowp Date (possible) source EMF 100%
        case 0x412:
        case 0x4A5: // EMF -> Radio 8, 50, F8, EB, 80, 03, 04, 05, 06
        case 0x50B:
        case 0x512:
        case 0x51F:
        case 0x525: // autowp display???
        case 0x52D:
        case 0x5CB:
        case 0x5D2:
        case 0x5DF:
        case 0x5E5:
        case 0x5ED:
          CAN0.sendMsgBuf(rxId, 0, len, rxBuf); // from ALL to RADIO
          break;

        default:
          CAN0.sendMsgBuf(rxId, 0, len, rxBuf); // from ALL to RADIO
          dtostrf(0.001 * now_millis, 8, 3, str_tmp0);
          sprintf(MsgString, "%s ALL --> %.3lX %d", str_tmp0, rxId, len);
          Serial.print(MsgString);
          for (byte i = 0; i < len; i ++) {
            Serial.print(", ");
            sprintf(MsgString, "%.2X", rxBuf[i]);
            Serial.print(MsgString);
          }
          Serial.println();
      }
    }
  }

  delay(1); // main loop delay
}
