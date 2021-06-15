#include <stdio.h>
#include <stdarg.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiClient.h>
#include <WiFiGeneric.h>
#include <WiFiMulti.h>
#include <WiFiScan.h>
#include <WiFiServer.h>
#include <WiFiSTA.h>
#include <WiFiType.h>
#include <WiFiUdp.h>

#include <dummy.h>

#include <esp_now.h>
#include <esp_attr.h>


  
//uint8_t broadcastAddress[] = {0x24,0x0A,0xC4,0xF8,0xF9,0x08}; 

uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; 
/****************** Definitions shared with display, make sure to keep in sync *****************/
// TODO - mode to a shared header file 

#define MAX_MESSAGE 250
unsigned char in_message[MAX_MESSAGE];
unsigned char out_message[MAX_MESSAGE];

enum message_type_e {
    SCORE,
    SCREEN_CHANGE,
    STEPS,
    HEART_RATE,
    ACK,
    NACK,
    /* Add new message types at the end */
};

typedef struct score_data_s {
  unsigned char message_type;
  unsigned char player_index;
  unsigned int score; 
} score_data_t;

typedef struct screen_change_data_s {
  unsigned char message_type;
  unsigned char screen; // TBD
} screen_change_data_t;
/****************** iDefinitions shared with display, make sure to keep in sync *****************/
#define SERIAL_ON

#ifdef SERIAL_ON
#define TRACE_ON
#endif

#define NUM_RETRIES  3
#define ACK_TIMEOUT_MS 5

#define LED_PIN      12
#define BUTTON_1_PIN 13
#define BUTTON_2_PIN 27
#define BUTTON_3_PIN 25

enum button_index_e {
    PLAYER_1_BUTTON_IDX = 0,
    PLAYER_2_BUTTON_IDX = 1,
    NUM_PLAYERS  = 2, 
    SCREEN_CHANGE_BUTTON_IDX = 2,
    NUM_BUTTONS = 3
};

static bool ack_received = 0;

int cur_button_index;

int prev_button_state[NUM_BUTTONS] = {0};
int curr_button_state[NUM_BUTTONS] = {0};

enum player_index_e {
  PLAYER_1_INDEX,
  PLAYER_2_INDEX
};
char button_index_2_player_index[NUM_PLAYERS] = {PLAYER_1_INDEX, PLAYER_2_INDEX};

char button_pins[NUM_BUTTONS] = {BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN};

unsigned char cur_score[NUM_PLAYERS] = {0,0};
unsigned char cur_screen = 1;
#define NUM_SCREENS 2 /* Score, clock/heart rate/steps */

static void trace( const char* format, ... ) {
#ifdef TRACE_ON
    va_list args;
    va_start( args, format );
    vprintf( format, args );
    va_end( args );
#endif
}

static void serial_init()
{
#ifdef SERIAL_ON
  Serial.begin(115200);
  while(!Serial) {
    delay(1); //Wait for serial port to connect. Needed for native USB port only
  }
#endif
}

static void pin_mode_init()
{
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_1_PIN, INPUT);
  pinMode(BUTTON_2_PIN, INPUT);
  pinMode(BUTTON_3_PIN, INPUT);
}

static void wifi_init()
{
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm); //Minimum WiFi RF power output ~ 120mA
}

void IRAM_ATTR OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.printf("Received from %s: ", macStr);
  dump_message(incomingData, len);
  unsigned char message_type = (unsigned char)*incomingData;
  if (ACK == message_type) {
    ack_received = true;
  }
}

static void wireless_link_init()
{
  wifi_init();
  esp_now_init();
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  esp_now_register_recv_cb(OnDataRecv);
  trace("\tESP-NOW should be up and running\n");
}

static uint8_t encode_score_packet(unsigned char player_index, unsigned int score)
{
  uint8_t len = 0;
  out_message[0] = (unsigned char)SCORE;
  len += sizeof(unsigned char);
  out_message[1] = player_index;
  len += sizeof(unsigned char);
  unsigned int swapped_score = htonl(score);
  memcpy(&(out_message[2]), &swapped_score, sizeof(unsigned int));
  len += sizeof(unsigned int);
  return len;
}

static uint8_t encode_screen_change_packet(unsigned char screen)
{
  uint8_t len = 0;
  out_message[0] = (unsigned char)SCREEN_CHANGE;
  len += sizeof(unsigned char);
  out_message[1] = screen;
  len += sizeof(unsigned char);
  return len;
}

static void dump_message(const uint8_t *pData, int len)
{
  int i;
  for (i=0; i<len; i++) {
    Serial.printf("%02X ", pData[i]);   
  }
  Serial.printf("\n");
}

static void transmit_with_retries(uint8_t len)
{
  int i;
  unsigned long start_time = 0;
  ack_received = false;
  for (i = 0; i < NUM_RETRIES && !ack_received; i++ ) {
    esp_now_send(broadcastAddress, (uint8_t*)out_message, len);
    start_time = millis();
    while (millis() < start_time + ACK_TIMEOUT_MS) {
      if (true == ack_received)
        break;
    }
  }
}
  
static void button_depressed_logic (int button_index) {
  uint8_t len;
  /*
   * button_index 0, button 1, player 1
   * button index 1, button 2, player 2
   * button index 2, button 3, screen change
   */
 
  if ((PLAYER_1_BUTTON_IDX == button_index) || (PLAYER_2_BUTTON_IDX == button_index)) {
    int player_index = button_index_2_player_index[button_index];
    cur_score[player_index]++;
    trace("Reporting current score %d for player %d\n", cur_score[player_index], player_index);
    len = encode_score_packet(player_index, cur_score[player_index]);
    Serial.printf("Sending: ");
    dump_message(out_message, len);
    //esp_now_send(broadcastAddress, (uint8_t*)out_message, len);
    transmit_with_retries(len);
  } else
  if (SCREEN_CHANGE_BUTTON_IDX == button_index) {
    //screen_change_data_t screen_change_packet;
    cur_screen = (cur_screen + 1) % NUM_SCREENS;
    trace("Reporting screen change, current screen %d\n", cur_screen);
    len = encode_screen_change_packet(cur_screen);
    Serial.printf("Sending: ");
    dump_message(out_message, len);
    //esp_now_send(broadcastAddress, (uint8_t*)out_message, len);    
    transmit_with_retries(len);
  } else {
    trace("Non-existing button %d ?!!\n", button_index);
  }
}



void setup() {
  setCpuFrequencyMhz(80);  //Lower the processor speed from default 240MHz to 80MHz

  serial_init();
  
  pin_mode_init();

  trace("\tStarting up ESP-NOW\n");
  
  wireless_link_init();
}

void loop() {
  /* Sample one button per loop iteration */
  cur_button_index = (cur_button_index + 1) % (NUM_BUTTONS);
  int button_read = digitalRead(button_pins[cur_button_index]);
    
  if(HIGH == button_read) {
    //trace("Button %d (pin %d) pressed\n", cur_button_index, button_pins[cur_button_index]);
    curr_button_state[cur_button_index] = 1;
    digitalWrite(LED_PIN, HIGH);
  } else {
    //trace("Button %d (pin %d) released\n", cur_button_index, button_pins[cur_button_index]);
    curr_button_state[cur_button_index] = 0;
    if (1 == prev_button_state[cur_button_index]) { // negative edge, putton depressed
      //trace("Button %d (pin %d) depressed\n", cur_button_index, button_pins[cur_button_index]);
      button_depressed_logic(cur_button_index);    
    }
    digitalWrite(LED_PIN, LOW);
  }
  prev_button_state[cur_button_index] = curr_button_state[cur_button_index];
}
