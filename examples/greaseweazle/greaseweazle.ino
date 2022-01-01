#include <Adafruit_Floppy.h>

// Only tested on SAMD51 chipsets. TURN ON 180MHZ OVERCLOCK AND FASTEST OPTIMIZE!

#define DENSITY_PIN  5     // IDC 2
// IDC 4 no connect
// IDC 6 no connect
#define INDEX_PIN    6     // IDC 8
// IDC 10 no connect
#define SELECT_PIN   A5    // IDC 12
// IDC 14 no connect
#define MOTOR_PIN    9     // IDC 16
#define DIR_PIN     10     // IDC 18
#define STEP_PIN    11     // IDC 20
#define READY_PIN   A0     // IDC 22
#define SIDE_PIN    A1     // IDC 
#define READ_PIN    12
#define PROT_PIN    A3
#define TRK0_PIN    A4

#define WRDATA_PIN -1
#define WRGATE_PIN -1

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

// WARNING! there are 100K max flux pulses per track!
uint8_t flux_transitions[MAX_FLUX_PULSE_PER_TRACK];

uint32_t time_stamp = 0;

uint8_t cmd_buffer[32], reply_buffer[128];
uint8_t cmd_buff_idx = 0;

#define GW_FIRMVER_MAJOR 1
#define GW_FIRMVER_MINOR 0
#define GW_MAXCMD      21
#define GW_SAMPLEFREQ  20000000UL // 20mhz for samd51
#define GW_HW_MODEL    8  // Adafruity
#define GW_HW_SUBMODEL 0  // Feather Samd51
#define GW_USB_SPEED   0  // Full Speed

#define GW_CMD_GETINFO    0
#define GW_CMD_GETINFO_FIRMWARE 0
#define GW_CMD_GETINFO_BANDWIDTH 1
#define GW_CMD_GETPARAMS  5
#define GW_CMD_GETPARAMS_DELAYS 0
#define GW_CMD_SELECT    12
#define GW_CMD_DESELECT  13
#define GW_CMD_RESET     16
#define GW_CMD_SOURCEBYTES 18
#define GW_CMD_SINKBYTES 19

#define GW_ACK_OK (byte)0
#define GW_ACK_BADCMD (byte)1

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200); 
  //while (!Serial) delay(100);
  Serial1.println("GrizzlyWizzly");
}

uint8_t get_cmd(uint8_t *buff, uint8_t maxbuff) {
  int i=0;
  
  if (Serial.available() < 2) return 0;
  buff[i++] = Serial.read();
  buff[i++] = Serial.read();
  // wait for remaining data
  while (Serial.available() < (buff[1] - 2)) {
    delay(1);
    yield();
  }
  for (; i<buff[1]; i++) {
    buff[i] = Serial.read();
  }
  return i;
}

uint32_t bandwidth_timer;
float bytes_per_sec;
uint32_t transfered_bytes;

void loop() {
  uint8_t cmd_len = get_cmd(cmd_buffer, sizeof(cmd_buffer));
  if (!cmd_len) return;
  
  int i = 0;
  uint8_t cmd = cmd_buffer[0];
  memset(reply_buffer, 0, sizeof(reply_buffer));
  reply_buffer[i++] = cmd;  // echo back the cmd itself
  
  Serial1.printf("Got command 0x%02x\n\r", cmd);
  
  if (cmd == GW_CMD_GETINFO) {
    uint8_t sub_cmd = cmd_buffer[2];
    if (sub_cmd == GW_CMD_GETINFO_FIRMWARE) {
      reply_buffer[i++] = GW_ACK_OK;
      reply_buffer[i++] = GW_FIRMVER_MAJOR; // 1 byte
      reply_buffer[i++] = GW_FIRMVER_MINOR; // 1 byte
      reply_buffer[i++] = 1; // is main firm
      reply_buffer[i++] = GW_MAXCMD;
      reply_buffer[i++] = GW_SAMPLEFREQ & 0xFF;
      reply_buffer[i++] = GW_SAMPLEFREQ >> 8;
      reply_buffer[i++] = GW_SAMPLEFREQ >> 16;
      reply_buffer[i++] = GW_SAMPLEFREQ >> 24;
      reply_buffer[i++] = GW_HW_MODEL;
      reply_buffer[i++] = GW_HW_SUBMODEL;
      reply_buffer[i++] = GW_USB_SPEED;
      Serial.write(reply_buffer, 34);
    }
    else if (sub_cmd == GW_CMD_GETINFO_BANDWIDTH) {
      reply_buffer[i++] = GW_ACK_OK;
      uint32_t min_bytes = transfered_bytes;
      uint32_t max_bytes = transfered_bytes;
      uint32_t min_usec =  bandwidth_timer * 1000;
      uint32_t max_usec =  bandwidth_timer * 1000;
      // TODO What is this math supposed to be??
            
      reply_buffer[i++] = min_bytes & 0xFF;
      reply_buffer[i++] = min_bytes >> 8;
      reply_buffer[i++] = min_bytes >> 16;
      reply_buffer[i++] = min_bytes >> 24;
      reply_buffer[i++] = min_usec & 0xFF;
      reply_buffer[i++] = min_usec >> 8;
      reply_buffer[i++] = min_usec >> 16;
      reply_buffer[i++] = min_usec >> 24;
      reply_buffer[i++] = max_bytes & 0xFF;
      reply_buffer[i++] = max_bytes >> 8;
      reply_buffer[i++] = max_bytes >> 16;
      reply_buffer[i++] = max_bytes >> 24;
      reply_buffer[i++] = max_usec & 0xFF;
      reply_buffer[i++] = max_usec >> 8;
      reply_buffer[i++] = max_usec >> 16;
      reply_buffer[i++] = max_usec >> 24;
      
      // TODO more?
      Serial.write(reply_buffer, 34);
    }
  }

  else if (cmd == GW_CMD_GETPARAMS) {
    uint8_t sub_cmd = cmd_buffer[2];
    if (sub_cmd == GW_CMD_GETPARAMS_DELAYS) {
      reply_buffer[i++] = GW_ACK_OK;
      reply_buffer[i++] = floppy.select_delay_us & 0xFF;
      reply_buffer[i++] = floppy.select_delay_us >> 8;
      reply_buffer[i++] = floppy.step_delay_us & 0xFF;
      reply_buffer[i++] = floppy.step_delay_us >> 8;
      reply_buffer[i++] = floppy.settle_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.settle_delay_ms >> 8;
      reply_buffer[i++] = floppy.motor_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.motor_delay_ms >> 8;
      reply_buffer[i++] = floppy.watchdog_delay_ms & 0xFF;
      reply_buffer[i++] = floppy.watchdog_delay_ms >> 8;
      Serial.write(reply_buffer, 12);
    }
  }

  else if (cmd == GW_CMD_RESET) {
    floppy.soft_reset();
    reply_buffer[i++] = GW_ACK_OK;
    Serial.write(reply_buffer, 2);
  }

  else if (cmd == GW_CMD_SINKBYTES) {
     uint32_t numbytes = 0;
     uint32_t seed = 0;
     numbytes |= cmd_buffer[5];
     numbytes <<= 8;
     numbytes |= cmd_buffer[4];
     numbytes <<= 8;
     numbytes |= cmd_buffer[3];
     numbytes <<= 8;
     numbytes |= cmd_buffer[2];
     Serial1.printf("sink numbytes %d\n\r", numbytes);

     seed |= cmd_buffer[9];
     seed <<= 8;
     seed |= cmd_buffer[8];
     seed <<= 8;
     seed |= cmd_buffer[7];
     seed <<= 8;
     seed |= cmd_buffer[6];
     reply_buffer[i++] = GW_ACK_OK;
     Serial.write(reply_buffer, 2);
     yield();
     bandwidth_timer = millis();
     transfered_bytes = numbytes;
     bytes_per_sec = numbytes;
     
     while (numbytes != 0) {
       int avail = Serial.available();
       if (avail == 0) {
         //Serial1.print("-");
         yield();
         continue;
       }
       //Serial1.printf("%lu avail, ", avail);
       int to_read = min(numbytes, min(sizeof(reply_buffer), avail));
       //Serial1.printf("%lu to read, ", to_read);
       numbytes -= Serial.readBytes((char *)reply_buffer, to_read);
       //Serial1.printf("%lu remain\n\r", numbytes);
     }
     bandwidth_timer = millis() - bandwidth_timer;
     bytes_per_sec /= bandwidth_timer;
     bytes_per_sec *= 1000;
     Serial1.print("Done in ");
     Serial1.print(bandwidth_timer);
     Serial1.print(" ms, ");
     Serial1.print(bytes_per_sec);
     Serial1.println(" bytes per sec");
     Serial.write(GW_ACK_OK);
     yield();
  }
  else if (cmd == GW_CMD_SOURCEBYTES) {
     uint32_t numbytes = 0;
     uint32_t seed = 0;
     numbytes |= cmd_buffer[5];
     numbytes <<= 8;
     numbytes |= cmd_buffer[4];
     numbytes <<= 8;
     numbytes |= cmd_buffer[3];
     numbytes <<= 8;
     numbytes |= cmd_buffer[2];
     Serial1.printf("source numbytes %d\n\r", numbytes);

     seed |= cmd_buffer[9];
     seed <<= 8;
     seed |= cmd_buffer[8];
     seed <<= 8;
     seed |= cmd_buffer[7];
     seed <<= 8;
     seed |= cmd_buffer[6];
     reply_buffer[i++] = GW_ACK_OK;
     Serial.write(reply_buffer, 2);
     yield();
     bandwidth_timer = millis();
     bytes_per_sec = numbytes;
     transfered_bytes = numbytes;

     uint32_t randnum = seed;
     while (numbytes != 0) {
       uint32_t to_write = min(numbytes, sizeof(reply_buffer));
       // we dont write 'just anything'!
       for (int i=0; i<to_write; i++) {
        reply_buffer[i] = randnum;
        if (randnum & 0x01) {
          randnum = (randnum >> 1) ^ 0x80000062;
        } else {
          randnum >>= 1;
        }
       }
       numbytes -= Serial.write(reply_buffer, to_write);
     }
     bandwidth_timer = millis() - bandwidth_timer;
     bytes_per_sec /= bandwidth_timer;
     bytes_per_sec *= 1000;
     Serial1.print("Done in ");
     Serial1.print(bandwidth_timer);
     Serial1.print(" ms, ");
     Serial1.print(bytes_per_sec);
     Serial1.println(" bytes per sec");
  }
  /********** unknown ! ********/
   else {
    reply_buffer[i++] = GW_ACK_BADCMD;
    Serial.write(reply_buffer, 2);
  }
}
