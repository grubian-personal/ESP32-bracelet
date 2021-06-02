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


  
uint8_t broadcastAddress[] = {0x24,0x0A,0xC4,0xF8,0xF9,0x08}; 

/****************** Definitions shared with display, make sure to keep in sync *****************/
// TODO - mode to a shared header file 

#define MAX_MESSAGE 250
unsigned char out_message[MAX_MESSAGE];

enum message_type_e {
    SCORE,
    SCREEN_CHANGE,
    STEPS,
    HEART_RATE,
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
/****************** Definitions shared with display, make sure to keep in sync *****************/
#define SERIAL_ON

#ifdef SERIAL_ON
#define TRACE_ON
#endif

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
    delay(1); 
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

static void wireless_link_init()
{
  wifi_init();
  esp_now_init();
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
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
    esp_now_send(broadcastAddress, (uint8_t*)out_message, len);
  } else
  if (SCREEN_CHANGE_BUTTON_IDX == button_index) {
    screen_change_data_t screen_change_packet;
    cur_screen = (cur_screen + 1) % NUM_SCREENS;
    trace("Reporting screen change, current screen %d\n", cur_screen);
    len = encode_screen_change_packet(cur_screen);
    esp_now_send(broadcastAddress, (uint8_t*)&screen_change_packet, sizeof(screen_change_data_t));    
  } else {
    trace("Non-existing button %d ?!!\n", button_index);
  }
}


void setup() {
  setCpuFrequencyMhz(80);  
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
    curr_button_state[cur_button_index] = 1;
    digitalWrite(LED_PIN, HIGH);
  } else {
    curr_button_state[cur_button_index] = 0;
    if (1 == prev_button_state[cur_button_index]) { // negative edge, putton depressed
      button_depressed_logic(cur_button_index);    
    }
    digitalWrite(LED_PIN, LOW);
  }
  prev_button_state[cur_button_index] = curr_button_state[cur_button_index];
}
