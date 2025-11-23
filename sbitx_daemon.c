/*
The initial sync between the gui values, the core radio values, settings, et al are manually set.
*/

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <linux/types.h>
#include <math.h>
#include <fcntl.h>
#include <complex.h>
#include <fftw3.h>
#include <linux/fb.h>
#include <sys/types.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <ncurses.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/file.h>
#include <errno.h>
#include <sys/file.h>
#include <errno.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <signal.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>
#include "sdr.h"
#include "sound.h"
#include "sdr_ui.h"
#include "ini.h"
#include "hamlib.h"
#include "remote.h"
#include "modem_ft8.h"
#include "i2cbb.h"
#include "webserver.h"
#include "logbook.h"
#include "hist_disp.h"
#include "configure.h"

void change_band(char *request);

/* command  buffer for commands received from the remote */
struct Queue q_remote_commands;
struct Queue q_zbitx_console;

/* Front Panel controls */
char pins[15] = {0, 2, 3, 6, 7, 
								10, 11, 12, 13, 14, 
								21, 22, 23, 25, 27};

#define ENC1_A (13)
#define ENC1_B (12)
#define ENC1_SW (14)

#define ENC2_A (0)
#define ENC2_B (2)
#define ENC2_SW (3)

#define SW5 (22)
#define PTT (7)
#define DASH (21)

#define ENC_FAST 1
#define ENC_SLOW 5

#define DS3231_I2C_ADD 0x68
//time sync, when the NTP time is not synced, this tracks the number of seconds 
//between the system cloc and the actual time set by \utc command

//encoder state
struct encoder {
	int pin_a,  pin_b;
	int speed;
	int prev_state;
	int history;
};
void tuning_isr(void);

#define COLOR_SELECTED_TEXT 0
#define COLOR_TEXT 1
#define COLOR_TEXT_MUTED 2
#define COLOR_SELECTED_BOX 3 
#define COLOR_BACKGROUND 4
#define COLOR_FREQ 5
#define COLOR_LABEL 6
#define SPECTRUM_BACKGROUND 7
#define SPECTRUM_GRID 8
#define SPECTRUM_PLOT 9
#define SPECTRUM_NEEDLE 10
#define COLOR_CONTROL_BOX 11
#define SPECTRUM_BANDWIDTH 12
#define COLOR_RX_PITCH 13
#define SELECTED_LINE 14
#define COLOR_FIELD_SELECTED 15 
#define COLOR_TX_PITCH 16

// we just use a look-up table to define the fonts used
// the struct field indexes into this table
struct font_style {
	int index;
	float r, g, b;
	char name[32];
	int height;
	int weight;
	int type;
	
};

struct encoder enc_a, enc_b;

//keyer polling variables
//the PTT and DASH lines are pulled high
int ptt_state = HIGH, dash_state = HIGH;
struct field *cw_input = NULL;
struct field *f_mode = NULL;
struct field *f_text_in = NULL;
struct field *f_pitch = NULL;

#define MAX_FIELD_LENGTH 128

#define FIELD_NUMBER 0
#define FIELD_BUTTON 1
#define FIELD_TOGGLE 2
#define FIELD_SELECTION 3
#define FIELD_TEXT 4
#define FIELD_STATIC 5
#define FIELD_CONSOLE 6

// The console is a series of lines (the only text list so far)
// console_stream is used as a ring buffer (TODO fix bugs to make it true)
#define MAX_CONSOLE_BUFFER 10000
#define MAX_LINE_LENGTH 128
#define MAX_CONSOLE_LINES 500
static int 	console_cols = 50;
struct console_line {
	char text[MAX_LINE_LENGTH];
	text_span_semantic spans[MAX_CONSOLE_LINE_STYLES];
};
static struct console_line console_stream[MAX_CONSOLE_LINES];
int console_current_line = 0;
struct Queue q_web;

static uint8_t zbitx_available = 0;
int update_logs = 0;
#define ZBITX_I2C_ADDRESS 0xa
void zbitx_init();
void zbitx_poll(int all);
void zbitx_get_spectrum(char *buff);
void zbitx_write(int style, char *text);

// event ids, some of them are mapped from gtk itself
#define FIELD_DRAW 0
#define FIELD_UPDATE 1 
#define FIELD_EDIT 2
#define MIN_KEY_UP 0xFF52
#define MIN_KEY_DOWN	0xFF54
#define MIN_KEY_LEFT 0xFF51
#define MIN_KEY_RIGHT 0xFF53
#define MIN_KEY_ENTER 0xFF0D
#define MIN_KEY_ESC	0xFF1B
#define MIN_KEY_BACKSPACE 0xFF08
#define MIN_KEY_TAB 0xFF09
#define MIN_KEY_CONTROL 0xFFE3
#define MIN_KEY_F1 0xFFBE
#define MIN_KEY_F2 0xFFBF
#define MIN_KEY_F3 0xFFC0
#define MIN_KEY_F4 0xFFC1
#define MIN_KEY_F5 0xFFC2
#define MIN_KEY_F6 0xFFC3
#define MIN_KEY_F7 0xFFC4
#define MIN_KEY_F8 0xFFC5
#define MIN_KEY_F9 0xFFC6
#define MIN_KEY_F9 0xFFC6
#define MIN_KEY_F10 0xFFC7
#define MIN_KEY_F11 0xFFC8
#define MIN_KEY_F12 0xFFC9
#define COMMAND_ESCAPE '\\'

void set_ui(int id);
void set_bandwidth(int hz);

struct field {
	char	*cmd;
	int		(*fn)(struct field *f, int event, int param_a, int param_b, int param_c);
	int		x, y, width, height;
	char	label[30];
	int 	label_width;
	char	value[MAX_FIELD_LENGTH];
	char	value_type; //NUMBER, SELECTION, TEXT, TOGGLE, BUTTON
	char  selection[1000];
	long int	 	min, max;
  int step;
	int 	section;
	char is_dirty;
	char update_remote;
	unsigned int updated_at;
	void *data;
};

#define STACK_DEPTH 4

struct band {
	char name[10];
	int	start;
	int	stop;
	int index;
	int	freq[STACK_DEPTH];
	int mode[STACK_DEPTH];
};

struct cmd {
	char *cmd;
	int (*fn)(char *args[]);
};


static unsigned long focus_since = 0;
static struct field *f_focus = NULL;
static struct field *f_hover = NULL;
static struct field *f_last_text = NULL;

//variables to power up and down the tx

static int in_tx = TX_OFF;
static int tx_start_time = 0;

static int *tx_mod_buff = NULL;
static int tx_mod_index = 0;
static int tx_mod_max = 0;

static long int tuning_step = 1000;
static int tx_mode = MODE_USB;


#define BAND80M	0
#define BAND40M	1
#define BAND60M 2
#define BAND30M 3 	
#define BAND20M 4 	
#define BAND17M 5	
#define BAND15M 6 
#define BAND12M 7 
#define BAND10M 8  

struct band band_stack[] = {
	{"80M", 3500000, 4000000, 0, 
		{3500000,3574000,3600000,3700000},{MODE_CW, MODE_LSB, MODE_CW,MODE_LSB}},
	{"60M", 5250000, 5500000, 0, 
		{5251500, 5354000,5357000,5360000},{MODE_CW, MODE_USB, MODE_USB, MODE_USB}},
	{"40M", 7000000,7300000, 0,
		{7000000,7040000,7074000,7150000},{MODE_CW, MODE_CW, MODE_USB, MODE_LSB}},
	{"30M", 10100000, 10150000, 0,
		{10100000, 10100000, 10136000, 10150000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
	{"20M", 14000000, 14400000, 0,
		{14000000, 14400000, 14074000, 14200000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
	{"17M", 18068000, 18168000, 0,
		{18068000, 18100000, 18110000, 18160000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
	{"15M", 21000000, 21500000, 0,
		{21100000, 21500000, 21074000, 21250000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
	{"12M", 24890000, 24990000, 0,
		{24890000, 24910000, 24950000, 24990000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
	{"10M", 28000000, 29700000, 0,
		{28000000, 28000000, 28074000, 28250000}, {MODE_CW, MODE_CW, MODE_USB, MODE_USB}},
};


#define VFO_A 0 
#define VFO_B 1 
//int	vfo_a_freq = 7000000;
//int	vfo_b_freq = 14000000;
char vfo_a_mode[10];
char vfo_b_mode[10];

//recording duration in seconds
time_t record_start = 0;

#define MAX_RIT 25000

int spectrum_span = 48000;
extern int spectrum_plot[];
extern int fwdpower, vswr, sbitx_version, sbitx_versionn;

void do_control_action(char *cmd);
void cmd_exec(char *cmd);


int do_spectrum(struct field *f, int event, int a, int b, int c);
int do_waterfall(struct field *f, int event, int a, int b, int c);
int do_tuning(struct field *f, int event, int a, int b, int c);
int do_text(struct field *f, int event, int a, int b, int c);
int do_status(struct field *f, int event, int a, int b, int c);
int do_console(struct field *f, int event, int a, int b, int c);
int do_pitch(struct field *f, int event, int a, int b, int c);
int do_kbd(struct field *f, int event, int a, int b, int c);
int do_toggle_kbd(struct field *f, int event, int a, int b, int c);
int do_mouse_move(struct field *f, int event, int a, int b, int c);
int do_macro(struct field *f, int event, int a, int b, int c);
int do_record(struct field *f, int event, int a, int b, int c);
int do_bandwidth(struct field *f, int event, int a, int b, int c);
int do_tune_tx(struct field *f, int event, int a, int b, int c);

struct field *active_layout = NULL;
char settings_updated = 0;
#define LAYOUT_KBD 0
#define LAYOUT_MACROS 1
int current_layout = LAYOUT_KBD;

#define COMMON_CONTROL 1
#define FT8_CONTROL 2
#define CW_CONTROL 4
#define VOICE_CONTROL 8
#define DIGITAL_CONTROL 16


// the cmd fields that have '#' are not to be sent to the sdr
struct field main_controls[] = {
	/* band stack registers */
	{"#10m", NULL, 50, 5, 40, 40, "10M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#12m", NULL, 90, 5, 40, 40, "12M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#15m", NULL, 130, 5, 40, 40, "15M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#17m", NULL, 170, 5, 40, 40, "17M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#20m", NULL, 210, 5, 40, 40, "20M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#30m", NULL, 250, 5, 40, 40, "30M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#40m", NULL, 290, 5, 40, 40, "40M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#60m", NULL, 330, 5, 40, 40, "60M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#80m", NULL, 370, 5, 40, 40, "80M", 1, "1", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{ "#record", do_record, 420, 5, 40, 40, "REC", 40, "OFF", FIELD_TOGGLE, 
		"ON/OFF", 0,0, 0,COMMON_CONTROL},
	{"#set", NULL, 460, 5, 40, 40, "SET", 1, "", FIELD_BUTTON,"", 0,0,0,COMMON_CONTROL}, 
	{ "r1:gain", NULL, 375, 5, 40, 40, "IF", 40, "60", FIELD_NUMBER, 
		"", 0, 100, 1,COMMON_CONTROL},
	{ "r1:agc", NULL, 415, 5, 40, 40, "AGC", 40, "SLOW", FIELD_SELECTION, 
		"OFF/SLOW/MED/FAST", 0, 1024, 1,COMMON_CONTROL},
	{ "tx_power", NULL, 455, 5, 40, 40, "DRIVE", 40, "40", FIELD_NUMBER, 
		"", 0, 100, 5,COMMON_CONTROL},


	{ "r1:freq", do_tuning, 600, 0, 150, 49, "FREQ", 5, "14000000", FIELD_NUMBER, 
		"", 500000, 30000000, 100,COMMON_CONTROL},

	{ "r1:volume", NULL, 755, 5, 40, 40, "AUDIO", 40, "60", FIELD_NUMBER, 
		"", 0, 100, 1,COMMON_CONTROL},

	{"#step", NULL, 560, 5 ,40, 40, "STEP", 1, "10Hz", FIELD_SELECTION, 
		"10K/1K/500H/100H/10H", 0,0,0,COMMON_CONTROL},
	{"#span", NULL, 560, 50 , 40, 40, "SPAN", 1, "A", FIELD_SELECTION, 
		"25K/10K/6K/2.5K", 0,0,0,COMMON_CONTROL},

	{"#rit", NULL, 600, 50, 40, 40, "RIT", 40, "OFF", FIELD_TOGGLE, 
		"ON/OFF", 0,0,0,COMMON_CONTROL},
	{"#vfo", NULL, 640, 50 , 40, 40, "VFO", 1, "A", FIELD_SELECTION, 
		"A/B", 0,0,0,COMMON_CONTROL},
	{"#split", NULL, 680, 50, 40, 40, "SPLIT", 40, "OFF", FIELD_TOGGLE, 
		"ON/OFF", 0,0,0,COMMON_CONTROL},

	{ "r1:mode", NULL, 5, 5, 40, 40, "MODE", 40, "USB", FIELD_SELECTION, 
		"USB/LSB/CW/CWR/FT8/FT4/AM/DIGI/2TONE", 0,0,0, COMMON_CONTROL},
	{ "#bw", do_bandwidth, 495, 5, 40, 40, "BW", 40, "", FIELD_NUMBER, 
		"", 50, 5000, 50,COMMON_CONTROL},

	/* logger controls */

	{"#contact_callsign", do_text, 5, 50, 85, 20, "CALL", 70, "", FIELD_TEXT, 
		"", 0,11,0,COMMON_CONTROL},
	{"#rst_sent", do_text, 90, 50, 50, 20, "SENT", 70, "", FIELD_TEXT, 
		"", 0, 7,0,COMMON_CONTROL},
	{"#rst_received", do_text, 140, 50, 50, 20, "RECV", 70, "", FIELD_TEXT, 
		"", 0, 7,0,COMMON_CONTROL},
	{"#exchange_received", do_text, 190, 50, 50, 20, "EXCH", 70, "", FIELD_TEXT, 
		"", 0, 7,0,COMMON_CONTROL},
	{"#exchange_sent", do_text, 240, 50, 50, 20, "NR", 70, "", FIELD_TEXT, 
		"", 0, 7,0,COMMON_CONTROL},
	{"#enter_qso", NULL, 290, 50, 40, 40, "SAVE", 1, "", FIELD_BUTTON, 
		"", 0,0,0,COMMON_CONTROL},
	{"#wipe", NULL, 330, 50, 40, 40, "WIPE", 1, "", FIELD_BUTTON,"", 0,0,0,COMMON_CONTROL}, 
	{"#mfqrz", NULL, 370, 50, 40, 40, "QRZ", 1, "", FIELD_BUTTON,"", 0,0,0,COMMON_CONTROL}, 
	{"#logbook", NULL, 410, 50, 40, 40, "LOG", 1, "", FIELD_BUTTON,"", 0,0,0,COMMON_CONTROL}, 
	{"#tune", NULL, 450, 50, 40, 40, "TUNE", 1, "OFF", FIELD_TOGGLE,"ON/OFF", 0,0,0,COMMON_CONTROL}, 
	{"#text_in", do_text, 5, 70, 285, 20, "TEXT", 70, "text box", FIELD_TEXT, 
		"nothing valuable", 0,128,0,COMMON_CONTROL},

	{ "#toggle_kbd", do_toggle_kbd, 495, 50, 40, 40, "KBD", 40, "OFF", FIELD_TOGGLE, 
		"ON/OFF", 0,0, 0,COMMON_CONTROL},


/* end of common controls */

	//tx 
	{ "tx_gain", NULL, 550, -350, 50, 50, "MIC", 40, "50", FIELD_NUMBER, 
		"", 0, 100, 1, VOICE_CONTROL},

	{ "tx_compress", NULL, 600, -350, 50, 50, "COMP", 40, "0", FIELD_NUMBER, 
		"ON/OFF", 0,100,10, VOICE_CONTROL},
	{ "#tx_wpm", NULL, 650, -350, 50, 50, "WPM", 40, "12", FIELD_NUMBER, 
		"", 1, 50, 1, CW_CONTROL},
	{ "rx_pitch", do_pitch, 700, -350, 50, 50, "PITCH", 40, "600", FIELD_NUMBER, 
		"", 100, 3000, 10, FT8_CONTROL | DIGITAL_CONTROL},
	

	{ "#tx", NULL, 1000, -1000, 50, 50, "TX", 40, "", FIELD_BUTTON, 
		"RX/TX", 0,0, 0, VOICE_CONTROL},

	{ "#rx", NULL, 650, -400, 50, 50, "RX", 40, "", FIELD_BUTTON, 
		"RX/TX", 0,0, 0, VOICE_CONTROL | DIGITAL_CONTROL},
	

	{"r1:low", NULL, 660, -350, 50, 50, "LOW", 40, "300", FIELD_NUMBER, 
		"", 100,4000, 50, 0, DIGITAL_CONTROL},
	{"r1:high", NULL, 580, -350, 50, 50, "HIGH", 40, "3000", FIELD_NUMBER, 
		"", 100, 10000, 50, 0, DIGITAL_CONTROL},

	{"spectrum", do_spectrum, 400, 101, 400, 100, "SPECTRUM", 70, "7000 KHz", FIELD_STATIC, 
		"", 0,0,0, COMMON_CONTROL},  
	{"#status", do_status, -1000, -1000, 400, 29, "STATUS", 70, "7000 KHz", FIELD_STATIC, 
		"status", 0,0,0, 0},  

	{"waterfall", do_waterfall, 400, 201 , 400, 99, "WATERFALL", 70, "7000 KHz", FIELD_STATIC, 
		"", 0,0,0, COMMON_CONTROL},
	{"#console", do_console, 0, 100, 400, 200, "CONSOLE", 70, "", FIELD_CONSOLE, 
		"nothing valuable", 0,0,0, COMMON_CONTROL},

	{"#log_ed", NULL, 0, 480, 480, 20, "", 70, "", FIELD_STATIC, 
		"nothing valuable", 0,128,0, 0},
  // other settings - currently off screen
  { "reverse_scrolling", NULL, 1000, -1000, 50, 50, "RS", 40, "ON", FIELD_TOGGLE,
    "ON/OFF", 0,0,0, 0},
  { "tuning_acceleration", NULL, 1000, -1000, 50, 50, "TA", 40, "ON", FIELD_TOGGLE,
    "ON/OFF", 0,0,0, 0},
  { "tuning_accel_thresh1", NULL, 1000, -1000, 50, 50, "TAT1", 40, "10000", FIELD_NUMBER,
    "", 100,99999,100, 0},
  { "tuning_accel_thresh2", NULL, 1000, -1000, 50, 50, "TAT2", 40, "500", FIELD_NUMBER,
    "", 100,99999,100, 0},
  { "mouse_pointer", NULL, 1000, -1000, 50, 50, "MP", 40, "LEFT", FIELD_SELECTION,
    "BLANK/LEFT/RIGHT/CROSSHAIR", 0,0,0,0},
  { "#selband", NULL, 1000, -1000, 50, 50, "SELBAND", 40, "", FIELD_NUMBER,
    "", 0,73,1, 0},

	// Settings Panel
	{"#mycallsign", NULL, 1000, -1000, 400, 149, "MYCALLSIGN", 70, "CALL", FIELD_TEXT, 
		"", 3,10,1,0},
	{"#mygrid", NULL, 1000, -1000, 400, 149, "MYGRID", 70, "NOWHERE", FIELD_TEXT, 
		"", 4,6,1,0},
	{"#passkey", NULL, 1000, -1000, 400, 149, "PASSKEY", 70, "123", FIELD_TEXT, 
		"", 0,32,1,0},
	{"#xota_loc", NULL, 1000, -1000, 400, 149, "LOCATION", 70, "PEAK/PARK/ISLE", FIELD_TEXT,
		"", 0, 32, 1, 0},
	{"#xota", NULL, 1000, -1000, 400, 149, "xOTA", 40, "", FIELD_SELECTION,
		"NONE/IOTA/SOTA/POTA", 0, 0, 0, COMMON_CONTROL},

	//moving global variables into fields 	
  { "#vfo_a_freq", NULL, 1000, -1000, 50, 50, "VFOA", 40, "14000000", FIELD_NUMBER,
    "", 500000,30000000,1,0},
  {"#vfo_b_freq", NULL, 1000, -1000, 50, 50, "VFOB", 40, "7000000", FIELD_NUMBER,
    "", 500000,30000000,1,0},
  {"#rit_delta", NULL, 1000, -1000, 50, 50, "RIT_DELTA", 40, "000000", FIELD_NUMBER,
    "", -25000,25000,1,0},

  { "#cwinput", NULL, 1000, -1000, 50, 50, "CW_INPUT", 40, "KEYBOARD", FIELD_SELECTION,
		"IAMBIC/IAMBICB/STRAIGHT", 0,0,0, CW_CONTROL},
  { "#cwdelay", NULL, 1000, -1000, 50, 50, "CW_DELAY", 40, "300", FIELD_NUMBER,
    "", 50, 1000, 50, CW_CONTROL},
	{ "#tx_pitch", NULL, 400, -1000, 50, 50, "TX_PITCH", 40, "600", FIELD_NUMBER, 
    "", 300, 3000, 10, FT8_CONTROL},
	{ "sidetone", NULL, 1000, -1000, 50, 50, "SIDETONE", 40, "25", FIELD_NUMBER, 
    "", 0, 100, 5, CW_CONTROL},
	{"#sent_exchange", NULL, 1000, -1000, 400, 149, "SENT_EXCHANGE", 70, "", FIELD_TEXT, 
		"", 0,10,1, COMMON_CONTROL},
  { "#contest_serial", NULL, 1000, -1000, 50, 50, "CONTEST_SERIAL", 40, "0", FIELD_NUMBER,
    "", 0,1000000,1, COMMON_CONTROL},
	{"#current_macro", NULL, 1000, -1000, 400, 149, "MACRO", 70, "", FIELD_TEXT, 
		"", 0,32,1, COMMON_CONTROL},
  { "#fwdpower", NULL, 1000, -1000, 50, 50, "POWER", 40, "300", FIELD_NUMBER,
    "", 0,10000,1, COMMON_CONTROL},
  { "#vswr", NULL, 1000, -1000, 50, 50, "REF", 40, "300", FIELD_NUMBER,
    "", 0,10000, 1, COMMON_CONTROL},
  { "#batt", NULL, 1000, -1000, 50, 50, "VBATT", 40, "300", FIELD_NUMBER,
    "", 0,10000, 1, COMMON_CONTROL},
  { "bridge", NULL, 1000, -1000, 50, 50, "BRIDGE", 40, "100", FIELD_NUMBER,
    "", 10,100, 1, COMMON_CONTROL},
	//cw, ft8 and many digital modes need abort
	{"#abort", NULL, 370, 50, 40, 40, "ESC", 1, "", FIELD_BUTTON,"", 0,0,0,CW_CONTROL}, 

	//FT8 should be 4000 Hz
  {"#bw_voice", NULL, 1000, -1000, 50, 50, "BW_VOICE", 40, "2200", FIELD_NUMBER,
    "", 300, 3000, 50, 0},
  {"#bw_cw", NULL, 1000, -1000, 50, 50, "BW_CW", 40, "400", FIELD_NUMBER,
    "", 300, 3000, 50, 0},
  {"#bw_digital", NULL, 1000, -1000, 50, 50, "BW_DIGITAL", 40, "4000", FIELD_NUMBER,
    "", 300, 5000, 50, 0},

  {"#smeter", NULL, 1000, -1000, 50, 50, "SMETER", 40, "3000", FIELD_NUMBER,
    "", 300, 3000, 50, 0},

	//FT8 controls
	{"#ft8_check", NULL, 1000, -1000, 50, 50, "CHECK", 50, "check", FIELD_TEXT, 
		"nobody", 0,128,0,0},
	{"#ft8_auto", NULL, 1000, -1000, 50, 50, "FT8_AUTO", 40, "ON", FIELD_SELECTION,
		"OFF/ON/CQ_ALT/xOTA", 0, 0, 0, FT8_CONTROL},
	{"#ft8_tx1st", NULL, 1000, -1000, 50, 50, "FT8_TX1ST", 40, "ON", FIELD_TOGGLE, 
		"ON/OFF", 0,0,0, FT8_CONTROL},
  { "#ft8_repeat", NULL, 1000, -1000, 50, 50, "FT8_REPEAT", 40, "5", FIELD_NUMBER,
    "", 1, 10, 1, FT8_CONTROL},

	{"#telneturl", NULL, 1000, -1000, 400, 149, "TELNETURL", 70, "dxc.nc7j.com:7373", FIELD_TEXT, 
		"", 0,32,1, 0},

	//soft keyboard
	{"#kbd_q", do_kbd, 0, 300 ,50, 50, "", 1, "Q", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_w", do_kbd, 50, 300, 50, 50, "", 1, "W", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_e", do_kbd, 100, 300, 50, 50, "", 1, "E", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_r", do_kbd, 150, 300, 50, 50, "", 1, "R", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_t", do_kbd, 200, 300, 50, 50, "", 1, "T", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_y", do_kbd, 250, 300, 50, 50, "", 1, "Y", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_u", do_kbd, 300, 300, 50, 50, "", 1, "U", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_i", do_kbd, 350, 300, 50, 50, "", 1, "I", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_o", do_kbd, 400, 300, 50, 50, "", 1, "O", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_p", do_kbd, 450, 300, 50, 50, "", 1, "P", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_@", do_kbd, 500, 300, 50, 50, "", 1, "@", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#kbd_1", do_kbd, 550, 300, 50, 50, "", 1, "1", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_2", do_kbd, 600, 300, 50, 50, "", 1, "2", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_3", do_kbd, 650, 300, 50, 50, "", 1, "3", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_bs", do_kbd, 700, 300, 100, 50, "", 1, "DEL", FIELD_BUTTON,"", 0,0,0,0},

	{"#kbd_alt", do_kbd, 0, 350 ,50, 50, "", 1, "CMD", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_a", do_kbd, 50, 350 ,50, 50, "*", 1, "A", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_s", do_kbd, 100, 350, 50, 50, "", 1, "S", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_d", do_kbd, 150, 350, 50, 50, "", 1, "D", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_f", do_kbd, 200, 350, 50, 50, "", 1, "F", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_g", do_kbd, 250, 350, 50, 50, "", 1, "G", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_h", do_kbd, 300, 350, 50, 50, "", 1, "H", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_j", do_kbd, 350, 350, 50, 50, "", 1, "J", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_k", do_kbd, 400, 350, 50, 50, "'", 1, "K", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_l", do_kbd, 450, 350, 50, 50, "", 1, "L", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_/", do_kbd, 500, 350, 50, 50, "", 1, "/", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#kbd_4", do_kbd, 550, 350, 50, 50, "", 1, "4", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_5", do_kbd, 600, 350, 50, 50, "", 1, "5", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_6", do_kbd, 650, 350, 50, 50, "", 1, "6", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_enter", do_kbd, 700, 400, 100, 50, "", 1, "Enter", FIELD_BUTTON,"", 0,0,0,0}, 
 
	{"#kbd_ ", do_kbd, 0, 400, 50, 50, "", 1, "SPACE", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_z", do_kbd, 50, 400, 50, 50, "", 1, "Z", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_x", do_kbd, 100, 400, 50, 50, "", 1, "X", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_c", do_kbd, 150,	400, 50, 50, "", 1, "C", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_v", do_kbd, 200, 400, 50, 50, "", 1, "V", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_b", do_kbd, 250, 400, 50, 50, "", 1, "B", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_n", do_kbd, 300, 400, 50, 50, "", 1, "N", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_m", do_kbd, 350, 400, 50, 50, "", 1, "M", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_,", do_kbd, 400, 400, 50, 50, "", 1, ",", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_.", do_kbd, 450, 400, 50, 50, "", 1, ".", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_?", do_kbd, 500, 400, 50, 50, "", 1, "?", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#kbd_7", do_kbd, 550, 400, 50, 50, "", 1, "7", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_8", do_kbd, 600, 400, 50, 50, "", 1, "8", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_9", do_kbd, 650, 400, 50, 50, "", 1, "9", FIELD_BUTTON,"", 0,0,0,0}, 
	{"#kbd_0", do_kbd, 700, 350, 50, 50, "", 1, "0", FIELD_BUTTON,"", 0,0,0,0}, 

	//macros keyboard

	//row 1
	{"#mf1", do_macro, 0, 1360, 65, 40, "F1", 1, "CQ", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf2", do_macro, 65, 1360, 65, 40, "F2", 1, "Call", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf3", do_macro, 130, 1360, 65, 40, "F3", 1, "Reply", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf4", do_macro, 195, 1360, 65, 40, "F4", 1, "RRR", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf5", do_macro, 260, 1360, 70, 40, "F5", 1, "73", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf6", do_macro, 330, 1360, 70, 40, "F6", 1, "Call", FIELD_BUTTON,"", 0,0,0,0}, 

	//row 2

	{"#mf7", do_macro, 0, 1400, 65, 40, "F7", 1, "Exch", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf8", do_macro, 65, 1400, 65, 40, "F8", 1, "Tu", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf9", do_macro, 130, 1400, 65, 40, "F9", 1, "Rpt", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf10", do_macro, 195, 1400, 65, 40, "F10", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf11", do_macro, 260, 1400, 70, 40, "F11", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mf12", do_macro, 330, 1400, 70, 40, "F12", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	//row 3



	{"#mfedit", do_macro, 195, 1440, 65, 40, "Edit", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mfspot"	, do_macro, 260, 1440, 70, 40, "Spot", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	{"#mfkbd", do_macro, 330, 1440, 70, 40, "Kbd", 1, "", FIELD_BUTTON,"", 0,0,0,0}, 

	//the last control has empty cmd field 
	{"", NULL, 0, 0 ,0, 0, "#", 1, "Q", FIELD_BUTTON, "", 0,0,0,0},
};


struct field *get_field(const char *cmd);
void update_field(struct field *f);
void tx_on();
void tx_off();

struct field *get_field(const char *cmd){
	for (int i = 0; active_layout[i].cmd[0] > 0; i++)
		if (!strcmp(active_layout[i].cmd, cmd))
			return active_layout + i;
	return NULL;
}

void field_init(){
	for (int i = 0; active_layout[i].cmd[i] > 0; i++)
		active_layout[i].updated_at= 0;
}

//set the field directly to a particuarl value, programmatically
int set_field(const char *id, const char *value){
	struct field *f = get_field(id);
	int v;
	int debug = 0;

	if (!f){
		printf("*Error: field[%s] not found. Check for typo?\n", id);
		return 1;
	}
	
	if (f->value_type == FIELD_NUMBER){
		int	v = atoi(value);
		if (v < f->min)
			v = f->min;
		if (v > f->max)
			v = f->max;
		sprintf(f->value, "%d",  v);
	}
	else if (f->value_type == FIELD_SELECTION || f->value_type == FIELD_TOGGLE){
		// toggle and selection are the same type: toggle has just two values instead of many more
		char *p, *prev, *next, b[100];
		//search the current text in the selection
		prev = NULL;
		if (debug)
			printf("field selection [%s]\n");
		strcpy(b, f->selection);
		p = strtok(b, "/");
		if (debug)
			printf("first token [%s]\n", p);
		while(p){
			if (!strcmp(value, p))
				break;
			else
				prev = p;
			p = strtok(NULL, "/");
			if (debug)
				printf("second token [%s]\n", p);
		}	
		//set to the first option
		if (p == NULL){
			if (prev)
				strcpy(f->value, prev);
			printf("*Error: setting field[%s] to [%s] not permitted\n", f->cmd, value);
			return 1;
		}
		else{
			if (debug)
				printf("Setting field to %s\n", value);
			strcpy(f->value, value);
		}
	}
	else if (f->value_type == FIELD_BUTTON){
		strcpy(f->value, value);
		do_control_action(f->label);
	}
	else if (f->value_type == FIELD_TEXT){
		if (strlen(value) > f->max || strlen(value) < f->min){
			printf("*Error: field[%s] can't be set to [%s], improper size.\n", f->cmd, value);
			return 1;
		} else {
			if (debug)
				printf("field text [%s] value=[%s]\n", f->cmd, value);
			strcpy(f->value, value);
		}
	}

	if (!strcmp(id, "#rit") || !strcmp(id, "#ft8_auto"))
		debug = 1; 

	//send a command to the radio 
	char buff[200];
	sprintf(buff, "%s %s", f->label, f->value);
	do_control_action(buff);

	update_field(f);
	return 0;
}

struct field *get_field_by_label(const char *label){
	for (int i = 0; active_layout[i].cmd[0] > 0; i++)
		if (!strcasecmp(active_layout[i].label, label))
			return active_layout + i;
	return NULL;
}

const char *field_str(const char *label){
	struct field *f = get_field_by_label(label);
	if (f)
		return f->value;
	else
		return NULL; 
}

int field_int(char *label){
	struct field *f = get_field_by_label(label);
	if (f){
		return atoi(f->value);
	}
	else {
		printf("field_int: %s not found\n", label);
		return -1;
	}
}

int field_set(const char *label, const char *new_value){
	struct field *f = get_field_by_label(label);
	if (!f)
		return -1;
	int r = set_field(f->cmd, new_value); 
	update_field(f);
}

int get_field_value(const char *cmd, char *value){
	struct field *f = get_field(cmd);
	if (!f)
		return -1;
	strcpy(value, f->value);
	return 0;
}

int get_field_value_by_label(const char *label, char *value){
	struct field *f = get_field_by_label(label);
	if (!f)
		return -1;
	strcpy(value, f->value);
	return 0;
}


//prepares to send the latest value of a field to the remote head
int remote_update_field(int i, char *text){
	struct field * f = active_layout + i;

	if (f->cmd[0] == 0)
		return -1;
	
	//always send status afresh
	if (!strcmp(f->label, "STATUS")){
		//send time
		time_t now = time(NULL);
		struct tm *tmp = gmtime(&now);
		sprintf(text, "STATUS %04d/%02d/%02d %02d:%02d:%02dZ",  
			tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec); 
		return 1;
	}

	strcpy(text, f->label);
	strcat(text, " ");
	strcat(text, f->value);
	int update = f->update_remote;
	f->update_remote = 0;

	//debug on
//	if (!strcmp(f->cmd, "#text_in") && strlen(f->value))
//		printf("#text_in [%s] %d\n", f->value, update);
	//debug off
	return update;
}


// console is a list view, resembling a terminal with styled text
void console_init(){
	memset(console_stream, 0, sizeof(console_stream));
	struct field *f = get_field("#console");
	assert(f);
	f->is_dirty = TRUE;
	console_current_line = 0;
}

void web_add_string(char *string){
	while (*string){
		q_write(&q_web, *string++);
	}
}

void  web_write(int style, char *data){
	char tag[20];
    //int n1 = q_length(&q_web);
	switch(style){
		case STYLE_FT8_REPLY:
		case STYLE_FT8_RX:
			strcpy(tag, "WSJTX-RX");
			break;
		case STYLE_FLDIGI_RX:
			strcpy(tag, "FLDIGI-RX");
			break;
		case STYLE_CW_RX:
			strcpy(tag, "CW-RX");
			break;
		case STYLE_FT8_TX:
			strcpy(tag, "WSJTX-TX");
			break;
		case STYLE_FT8_QUEUED:
			strcpy(tag, "WSJTX-Q");
			break;
		case STYLE_FLDIGI_TX:
			strcpy(tag, "FLDIGI-TX");
			break;
		case STYLE_CW_TX:
			strcpy(tag, "CW-TX");
			break;
		case STYLE_TELNET:
			strcpy(tag, "TELNET");
			break;
		default:
			strcpy(tag, "LOG");
	}
	web_add_string("<");
	web_add_string(tag);		
	web_add_string(">");
	while (*data){
				switch(*data){
				case '<':
					q_write(&q_web, '&');
					q_write(&q_web, 'l');
					q_write(&q_web, 't');
					q_write(&q_web, ';');
					break;
				case '>':
					q_write(&q_web, '&');
					q_write(&q_web, 'g');
					q_write(&q_web, 't');
					q_write(&q_web, ';');
					break;
			 	case '"':
					q_write(&q_web, '&');
					q_write(&q_web, 'q');
					q_write(&q_web, 'u');
					q_write(&q_web, 't');
					q_write(&q_web, 'e');
					q_write(&q_web, ';');
					break;
				case '\'':
					q_write(&q_web, '&');
					q_write(&q_web, 'a');
					q_write(&q_web, 'p');
					q_write(&q_web, 'o');
					q_write(&q_web, 's');
					q_write(&q_web, ';');
					break;
				case '\n':
					q_write(&q_web, '&');
					q_write(&q_web, '#');
					q_write(&q_web, 'x');
					q_write(&q_web, 'A');
					q_write(&q_web, ';');
					break;	
				default:
					q_write(&q_web, *data);
			}
			data++;
	}			
	web_add_string("</");
	web_add_string(tag);
	web_add_string(">");
	//int n2 = q_length(&q_web); tlog("web_write", data, n2-n1);
}

int console_init_next_line(){
	console_current_line++;
	if (console_current_line == MAX_CONSOLE_LINES)
		console_current_line = 0;
	memset(&console_stream[console_current_line], 0, sizeof(struct console_line));
	return console_current_line;
}

void write_to_remote_app(int style, const char *text){
	remote_write("{");
	remote_write(text);
	remote_write("}");
}

/*!
    Write \a text to the console with one \a style as the semantic for the whole string.
    \a text should end with a newline if it's meant to be a whole console line;
    otherwise it gets appended to the last line, until the last line gets too long.
*/
void write_console(sbitx_style style, const char *text)
{
    text_span_semantic sem;
    memset(&sem, 0, sizeof(sem));
    sem.length = strlen(text);
    sem.semantic = style;
    write_console_semantic(text, &sem, 1);
}

/*!
    Append \a text with \a sem_count styled spans to the console.
    \a text should end with a newline if it's meant to be a whole console line;
    otherwise it gets appended to the last line, until the last line gets too long.
*/
void write_console_semantic(const char *text, const text_span_semantic *sem, int sem_count)
{
    if (!text || text[0] == 0)
	return;

    // TODO get rid of this: maybe come up with a way to send the `sem` array separately
    // to the web and remote UIs too; otherwise use `sem` to "decorate" with a better markup
    {
	char decorated[1000];
	assert(sem);
	hd_decorate(sem[0].semantic, text, decorated);
	web_write(sem[0].semantic, decorated);
	write_to_remote_app(sem[0].semantic, text);
	zbitx_write(sem[0].semantic, decorated);
    }

    const char *next_char = text;
    char *console_line_string = console_stream[console_current_line].text;
    text_span_semantic *console_line_spans = console_stream[console_current_line].spans;
    int output_span_i = 0;
    int col = console_line_spans[0].length;
    const text_span_semantic *next_sem = sem;
    while (*next_char)
    {
	int text_i = next_char - text;
	while (next_sem < sem + sem_count && next_sem->start_column == text_i) {
	    text_span_semantic *out_sem = &console_line_spans[output_span_i];
	    *out_sem = *next_sem; // copy whole struct
	    out_sem->start_row = console_current_line; // only useful for output to spans file, and should increment forever (TODO)
	    //~ printf("write '%s': span %d col %d len %d: style %d\n",
		//~ text, output_span_i, out_sem->start_column, out_sem->length, out_sem->semantic); // debug
	    ++output_span_i;
	    ++next_sem;
	}
	char c = *next_char;
	if (c == '\n' || col >= console_cols) {
	    console_line_string[col] = 0;
	    console_line_spans[output_span_i].length = col;
	    console_init_next_line();
	    console_line_string = console_stream[console_current_line].text;
	    console_line_spans = console_stream[console_current_line].spans;
	    col = 0;
	    output_span_i = 0;
	}
	else if (c < 128 && c >= ' ') // TODO support UTF-8 (otherwise isgraph() might work)
	{
	    console_line_string[col++] = c & 0x7f;
	}
	++next_char;
    }
    console_line_spans[0].length = col;

    struct field *f = get_field("#console");
    if (f)
	f->is_dirty = 1;
}

int do_console(struct field *f, int event, int a, int b, int c){
	char buff[100], *p, *q;

	switch(event){
		case FIELD_DRAW:
		    //tlog("do_console", "draw", n_lines);
			//draw_console(gfx, f);
			return 1;
		break;
	}
	return 0;	
}

static int mode_id(const char *mode_str){
	if (mode_str[0] == 'C' && mode_str[1] == 'W' && mode_str[2] == 0)
		return MODE_CW;
	else if (mode_str[0] == 'C' && mode_str[1] == 'W' && mode_str[2] == 'R' && mode_str[3] == 0)
		return MODE_CWR;
	else if (mode_str[0] == 'U' && mode_str[1] == 'S' && mode_str[2] == 'B' && mode_str[3] == 0)
		return MODE_USB;
	else if (mode_str[0] == 'L' && mode_str[1] == 'S' && mode_str[2] == 'B' && mode_str[3] == 0)
		return MODE_LSB;
	else if (mode_str[0] == 'F' && mode_str[1] == 'T' && mode_str[2] == '8' && mode_str[3] == 0)
		return MODE_FT8;
	else if (mode_str[0] == 'F' && mode_str[1] == 'T' && mode_str[2] == '4' && mode_str[3] == 0)
		return MODE_FT4;
	else if (mode_str[0] == 'N' && mode_str[1] == 'B' && mode_str[2] == 'F' && mode_str[3] == 'M')
		return MODE_NBFM;
	else if (mode_str[0] == 'A' && mode_str[1] == 'M' && mode_str[2] == '0')
		return MODE_AM;
	else if (mode_str[0] == '2' && mode_str[1] == 'T' && mode_str[2] == 'O' && mode_str[3] == 'N')
		return MODE_2TONE;
	else if (mode_str[0] == 'D' && mode_str[1] == 'I' && mode_str[2] == 'G' && mode_str[3] == 'I')
		return MODE_DIGITAL;
	return -1;
}

/*
static int mode_id(const char *mode_str){
	if (!strcmp(mode_str, "CW"))
		return MODE_CW;
	else if (!strcmp(mode_str, "CWR"))
		return MODE_CWR;
	else if (!strcmp(mode_str, "USB"))
		return MODE_USB;
	else if (!strcmp(mode_str,  "LSB"))
		return MODE_LSB;
	else if (!strcmp(mode_str,  "FT8"))
		return MODE_FT8;
	else if (!strcmp(mode_str, "NBFM"))
		return MODE_NBFM;
	else if (!strcmp(mode_str, "AM"))
		return MODE_AM;
	else if (!strcmp(mode_str, "2TONE"))
		return MODE_2TONE;
	else if (!strcmp(mode_str, "DIGI"))
		return MODE_DIGITAL;
	return -1;
}
*/

static char *mode_name(int mode_id, char *name){
	
	switch(mode_id){
		case MODE_USB:
			return strcpy(name, "USB");
		case MODE_LSB:
			return strcpy(name, "LSB");
		case MODE_CW:
			return strcpy(name, "CW");
		case MODE_CWR:
			return strcpy(name, "CWR");
		case MODE_NBFM:
			return strcpy(name, "NBFM");
		case MODE_AM:
			return strcpy(name, "AM");
		case MODE_FT8:
			return strcpy(name, "FT8");
		case MODE_FT4:
			return strcpy(name, "FT4");
		case MODE_DIGITAL:
			return strcpy(name, "DIGI");
		case MODE_2TONE:
			return strcpy(name, "2TONE");
		case MODE_TUNE:
			return strcpy(name, "TUNE");
		case MODE_CALIBRATE:
			return strcpy(name, "CALIBRATE");
		default:
			return strcpy(name, "USB");
	}
}

void save_user_settings(int forced){
	static int last_save_at = 0;
	char const *file_path = STATEDIR "/user_settings.ini";

	//attempt to save settings only if it has been 30 seconds since the 
	//last time the settings were saved
	int now = millis();
	if ((now < last_save_at + 30000 || !settings_updated) && forced == 0)
		return;


	//copy the current freq settings to the currently selected vfo
	struct field *f_freq = get_field("r1:freq");
	struct field *f_vfo  = get_field("#vfo");
	if (f_vfo->value[0] == 'A')
		set_field("#vfo_a_freq", f_freq->value);
	else
		set_field("#vfo_b_freq", f_freq->value);


	FILE *f = fopen(file_path, "w");
	if (!f){
		printf("Unable to save %s : %s\n", file_path, strerror(errno));
		settings_updated = 0;  // stop repeated attempts to write if file cannot be opened.
		last_save_at = now;
		return;
	}

  // save the field values
	int i;
	for (i= 0; active_layout[i].cmd[0] > 0; i++){
		fprintf(f, "%s=%s\n", active_layout[i].cmd, active_layout[i].value);
		//printf("%s=%s\n", active_layout[i].cmd, active_layout[i].value);
	}

	//now save the band stack
	for (int i = 0; i < sizeof(band_stack)/sizeof(struct band); i++){
		fprintf(f, "\n[%s]\n", band_stack[i].name);
		//fprintf(f, "power=%d\n", band_stack[i].power);
		for (int j = 0; j < STACK_DEPTH; j++)
			fprintf(f, "freq%d=%d\nmode%d=%d\n", j, band_stack[i].freq[j], j, band_stack[i].mode[j]);
	}
	fclose(f);

	last_save_at = now;	// As proposed by Dave N1AI
	settings_updated = 0;
}


void enter_qso(){
	const char *callsign = field_str("CALL");
	const char *rst_sent = field_str("SENT");
	const char *rst_received = field_str("RECV");

	// skip empty or half filled log entry
	if (strlen(callsign) < 3 || strlen(rst_sent) < 1 || strlen(rst_received) < 1){
		printf("log entry is empty [%s], [%s], [%s], no log created\n", callsign, rst_sent, rst_received);
		return;
	}
 
	if (logbook_count_dup(field_str("CALL"), 60)){
		printf("Duplicate log entry not accepted for %s within two minutes of last entry of %s.\n", callsign, callsign);
		return;
	}	
	logbook_add(get_field("#contact_callsign")->value, 
		get_field("#rst_sent")->value, 
		get_field("#exchange_sent")->value, 
		get_field("#rst_received")->value, 
		get_field("#exchange_received")->value);
	char buff[100];
	sprintf(buff, "Logged: %s %s-%s %s-%s\n", 
		field_str("CALL"), field_str("SENT"), field_str("NR"), 
		field_str("RECV"), field_str("EXCH"));
	write_console(STYLE_LOG, buff);
	update_logs = 1;
	// wipe the call if not FT8/FT4
	switch (mode_id(field_str("MODE"))) {
	case MODE_FT4:
	case MODE_FT8:
		break;
	default:
		call_wipe();
	}
}

static int user_settings_handler(void *user, const char *section, const char *name, const char *value)
{
	char cmd[1000];
	char new_value[200];

	strcpy(new_value, value);

	if (!strcmp(section, "r1"))
	{
		sprintf(cmd, "%s:%s", section, name);
		set_field(cmd, new_value);
	}
	else if (!strcmp(section, "tx"))
	{
		strcpy(cmd, name);
		set_field(cmd, new_value);
	}
	else if (!strncmp(section, "#kbd", 4))
	{
		return 1; // skip the keyboard values
	}
	// if it is an empty section
	else if (strlen(section) == 0)
	{
		sprintf(cmd, "%s", name);
		// skip the button actions
		struct field *f = get_field(cmd);
		if (f)
		{
			if (f->value_type != FIELD_BUTTON)
			{
				set_field(cmd, new_value);
			}
		/*
			char *bands = "#80m#60m#40m#30m#20m#17m#15m#12m#10m";
			char *ptr = strstr(bands, cmd);
			if (ptr != NULL)
			{
				// Set the selected band stack index
				int band = (ptr - bands) / 4;
				int ix = get_band_stack_index(new_value);
				if (ix < 0)
				{
					// printf("Band stack index for %c%cm set to 0!\n", *(ptr+1), *(ptr+2));
					ix = 0;
				}
				band_stack[band].index = ix;
				settings_updated++;
			}
		*/
		}
		return 1;
	}

	// band stacks
	int band = -1;
	if (!strcmp(section, "80M"))
		band = BAND80M;
	else if (!strcmp(section, "60M"))
		band = BAND60M;
	else if (!strcmp(section, "40M"))
		band = BAND40M;
	else if (!strcmp(section, "30M"))
		band = BAND30M;
	else if (!strcmp(section, "20M"))
		band = BAND20M;
	else if (!strcmp(section, "17M"))
		band = BAND17M;
	else if (!strcmp(section, "15M"))
		band = BAND15M;
	else if (!strcmp(section, "12M"))
		band = BAND12M;
	else if (!strcmp(section, "10M"))
		band = BAND10M;

	if (band != -1)
	{
		if (strstr(name, "freq"))
		{
			int freq = atoi(value);
			if (freq < band_stack[band].start || band_stack[band].stop < freq)
				return 1;
		}
		if (!strcmp(name, "freq0"))
			band_stack[band].freq[0] = atoi(value);
		else if (!strcmp(name, "freq1"))
			band_stack[band].freq[1] = atoi(value);
		else if (!strcmp(name, "freq2"))
			band_stack[band].freq[2] = atoi(value);
		else if (!strcmp(name, "freq3"))
			band_stack[band].freq[3] = atoi(value);
		else if (!strcmp(name, "mode0"))
			band_stack[band].mode[0] = atoi(value);
		else if (!strcmp(name, "mode1"))
			band_stack[band].mode[1] = atoi(value);
		else if (!strcmp(name, "mode2"))
			band_stack[band].mode[2] = atoi(value);
		else if (!strcmp(name, "mode3"))
			band_stack[band].mode[3] = atoi(value);
/*		else if (!strcmp(name, "gain"))
			band_stack[band].if_gain = atoi(value);
		else if (!strcmp(name, "drive"))
			band_stack[band].drive = atoi(value);
		else if (!strcmp(name, "tnpwr"))
			band_stack[band].tnpwr = atoi(value);*/
	}
	return 1;
}
// mod disiplay holds the tx modulation time domain envelope
// even values are the maximum and the even values are minimum

#define MOD_MAX 800
int mod_display[MOD_MAX];
int mod_display_index = 0;

void sdr_modulation_update(int32_t *samples, int count, double scale_up){
	double min=0, max=0;

	for (int i = 0; i < count; i++){
		if (i % 48 == 0 && i > 0){
			if (mod_display_index >= MOD_MAX)
				mod_display_index = 0;
			mod_display[mod_display_index++] = (min / 40000000.0) / scale_up;
			mod_display[mod_display_index++] = (max / 40000000.0) / scale_up;
			min = 0x7fffffff;
			max = -0x7fffffff;
		}
		if (*samples < min)
			min = *samples;
		if (*samples > max)
			max = *samples;
		samples++;
	}
}

static int waterfall_offset = 30;
static int  *wf = NULL;
uint8_t *waterfall_map = NULL;

void init_waterfall(){
	struct field *f = get_field("waterfall");

	if (wf)
		free(wf);
	//this will store the db values of waterfall
	wf = malloc((MAX_BINS/2) * f->height * sizeof(int));
	if (!wf){
		puts("*Error: malloc failed on waterfall buffer");
		exit(0);
	}
	memset(wf, 0, (MAX_BINS/2) * f->height * sizeof(int));

	if (waterfall_map)
		free(waterfall_map);
	//this will store the bitmap pixles, 3 bytes per pixel
	waterfall_map = malloc(f->width * f->height * 3);
	for (int i = 0; i < f->width; i++)
		for (int j = 0; j < f->height; j++){
			int row = j * f->width * 3;
			int	index = row + i * 3;
			waterfall_map[index++] = 0;
			waterfall_map[index++] = 0;//i % 256;
			waterfall_map[index++] =0;// j % 256; 
	}
}

void update_field(struct field *f){
	if (f->y >= 0)
		f->is_dirty = 1;
	f->update_remote = 1;
	f->updated_at = millis();
} 

// respond to a UI request to change the field value
static void edit_field(struct field *f, int action){
	int v;
	if (f == f_focus)
		focus_since = millis();

	if (f->fn){
		f->is_dirty = 1;
	 	f->update_remote = 1;
		f->updated_at = millis();
		if (f->fn(f, FIELD_EDIT, action, 0, 0))
			return;
	}
	
	if (f->value_type == FIELD_NUMBER){
		int	v = atoi(f->value);
		if (action == MIN_KEY_UP && v + f->step <= f->max)
			v += f->step;
		else if (action == MIN_KEY_DOWN && v - f->step >= f->min)
			v -= f->step;
		sprintf(f->value, "%d",  v);
	}
	else if (f->value_type == FIELD_SELECTION){
		char *p, *prev, *next, b[100], *first, *last;
    // get the first and last selections
    strcpy(b, f->selection);
    p = strtok(b, "/");
    first = p;
    while(p){
      last = p;
      p = strtok(NULL, "/");
    }
		//search the current text in the selection
		prev = NULL;
		strcpy(b, f->selection);
		p = strtok(b, "/");
		while(p){
			if (!strcmp(p, f->value))
				break;
			else
				prev = p;
			p = strtok(NULL, "/");
		}	
		//set to the first option
		if (p == NULL){
			if (prev)
				strcpy(f->value, prev);
		}
		else if (action == MIN_KEY_DOWN){
			prev = p;
			p = strtok(NULL,"/");
			if (p)
				strcpy(f->value, p);
			else
        strcpy(f->value, first); // roll over
				//return;
				//strcpy(f->value, prev); 
		}
		else if (action == MIN_KEY_UP){
			if (prev)
				strcpy(f->value, prev);
			else
        strcpy(f->value, last); // roll over
				//return;
		}
	}
	else if (f->value_type == FIELD_TOGGLE){
		char *p, *prev, *next, b[100];
		//search the current text in the selection
		prev = NULL;
		strcpy(b, f->selection);
		p = strtok(b, "/");
		while(p){
			if (strcmp(p, f->value))
				break;
			p = strtok(NULL, "/");
		}	
		strcpy(f->value, p);
	}
	else if (f->value_type == FIELD_BUTTON){
		NULL; // ah, do nothing!
	}

	//send a command to the radio
	char buff[200];

//	sprintf(buff, "%s=%s", f->cmd, f->value);

	sprintf(buff, "%s %s", f->label, f->value);
	do_control_action(buff);
	f->is_dirty = 1;
	f->update_remote = 1;
	f->updated_at = millis();
//	update_field(f);
	settings_updated++;
}

static void focus_field(struct field *f){
	struct field *prev_hover = f_hover;
	struct field *prev_focus = f_focus;
	
	f_focus = NULL;
	if (prev_hover)
		update_field(prev_hover);
	if (prev_focus)
		update_field(prev_focus);
	if (f){
		f_focus = f_hover = f;
		focus_since = millis();
	}
	update_field(f_hover);

	//is it a toggle field?
	if (f_focus->value_type == FIELD_TOGGLE)
		edit_field(f_focus, MIN_KEY_DOWN);	

	if (f_focus->value_type == FIELD_TEXT)
		f_last_text = f_focus;
  //is it a selection field?
  if (f_focus->value_type == FIELD_SELECTION) 
    edit_field(f_focus, MIN_KEY_UP);

	//if the button has been pressed, do the needful
	if (f_focus->value_type == FIELD_TOGGLE || 
			f_focus->value_type == FIELD_BUTTON)
				do_control_action(f->label);
}



// setting the frequency is complicated by having to take care of the
// rit/split and power levels associated with each frequency
void set_operating_freq(int dial_freq, char *response){
	struct field *rit = get_field("#rit");
	struct field *split = get_field("#split");
	struct field *vfo_a = get_field("#vfo_a_freq");
	struct field *vfo_b = get_field("#vfo_b_freq");
	struct field *rit_delta = get_field("#rit_delta");

	char freq_request[30];
 
	if (!strcmp(rit->value, "ON")){
		if (!in_tx)
			sprintf(freq_request, "r1:freq=%d", dial_freq + atoi(rit_delta->value)); 		
		else
			sprintf(freq_request, "r1:freq=%d", dial_freq); 		
	}
	else if (!strcmp(split->value, "ON")){
		if (!in_tx)
			sprintf(freq_request, "r1:freq=%s", vfo_a->value);	// was vfo_b->value
		else
			sprintf(freq_request, "r1:freq=%s", vfo_b->value);
	}
	else
	{
			sprintf(freq_request, "r1:freq=%d", dial_freq);
	}
	//get back to setting the frequency
	sdr_request(freq_request, response);
}

void abort_tx(){
	set_field("#text_in", "");
	modem_abort();
	tx_off();
}

int do_spectrum(struct field *f, int event, int a, int b, int c){
	struct field *f_freq, *f_span, *f_pitch;
	int span, pitch;
  long freq;
	char buff[100];
  int mode = mode_id(get_field("r1:mode")->value);

	switch(event){
		case FIELD_DRAW:
			//draw_spectrum(f, gfx);
			return 1;
		break;
	}
	return 0;	
}

int do_waterfall(struct field *f, int event, int a, int b, int c){
	switch(event){
		case FIELD_DRAW:
			//draw_waterfall(f, gfx);
			return 1;
	}
	return 0;	
}

void remote_execute(const char *cmd){
	if (q_remote_commands.overflow)
		q_empty(&q_remote_commands);
	while (*cmd)
		q_write(&q_remote_commands, *cmd++);
	q_write(&q_remote_commands, 0);
}


void call_wipe(){
	field_set("CALL", "");
	field_set("SENT", "");
	field_set("RECV", "");
	field_set("EXCH", "");
	field_set("NR", "");
}

// calcualtes the LOW and HIGH settings from bw
// and sets them up, called from UI
void save_bandwidth(int hz){
	char bw[10];

 	int mode = mode_id(get_field("r1:mode")->value);
	sprintf(bw, "%d", hz);
	switch(mode){
		case MODE_CW:
		case MODE_CWR:
			field_set("BW_CW",bw); 
			break;
		case MODE_USB:
		case MODE_LSB:
		case MODE_NBFM:
		case MODE_AM:
			field_set("BW_VOICE",bw); 
			break;
		default:
			field_set("BW_DIGITAL",bw); 
	}
}

void set_filter_high_low(int hz){
	char buff[10], bw_str[10];
	int low, high;

	if (hz < 50)
		return;

	f_mode = get_field("r1:mode");
	f_text_in = get_field("text_in");
	f_pitch = get_field("rx_pitch");

	switch(mode_id(f_mode->value)){
		case MODE_CW:
		case MODE_CWR:
			low = atoi(f_pitch->value) - hz/2;
			high = atoi(f_pitch->value) + hz/2;
			break;
		case MODE_LSB:
		case MODE_USB:
			low = 300;
			high = low + hz;
			break;
		case MODE_DIGITAL:
			low = atoi(f_pitch->value) - (hz/2);
			high = atoi(f_pitch->value) + (hz/2);
			break;
		case MODE_AM:
			low = 300;
			high = hz;
			break;
		case MODE_FT4:
		case MODE_FT8:
			low = 50;
			high = 4000;
			break;
		default:
			low = 50;
			high = 3000;
	}

	if (low < 50)
		low = 50;
	if (high > 5000)
		high = 5000;

	//now set the bandwidth
	sprintf(buff, "%d", low);
	set_field("r1:low", buff);
	sprintf(buff, "%d", high);
	set_field("r1:high", buff);
}
int do_status(struct field *f, int event, int a, int b, int c){
	char buff[100];

	if (event == FIELD_DRAW){
		//time_t now = time_sbitx();
		//struct tm *tmp = gmtime(&now);
		//sprintf(buff, "%04d/%02d/%02d %02d:%02d:%02dZ",  
		//	tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec); 
		//int width = measure_text(gfx, buff, STYLE_FIELD_LABEL);
		//int line_height = font_table[f->font_index].height; 	
		//strcpy(f->value, buff);
		f->is_dirty = 1;
		f->update_remote = 1;
		f->updated_at = millis();
		//sprintf(buff, "sBitx %s %s %04d/%02d/%02d %02d:%02d:%02dZ",  
		//	get_field("#mycallsign")->value, get_field("#mygrid")->value,
		//	tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec); 
		//gtk_window_set_title( GTK_WINDOW(window), buff);

		return 1;
	}
	return 0;
}

void execute_app(char *app){
	char buff[1000];

	sprintf(buff, "%s 0> /dev/null", app); 
	int pid = fork();
	if (!pid){
		system(buff);
		exit(0);	
	}
}

int do_text(struct field *f, int event, int a, int b, int c){
	int width, offset, text_length, line_start, y;	
	char this_line[MAX_FIELD_LENGTH];
	int text_line_width = 0;

	if (event == FIELD_EDIT){
		//if it is a command, then execute it and clear the field
		if (f->value[0] == COMMAND_ESCAPE &&  strlen(f->value) > 1 && (a == '\n' || a == MIN_KEY_ENTER)){
			cmd_exec(f->value + 1);
			f->value[0] = 0;
			update_field(f);
		}
		else if ((a == '\n' || a == MIN_KEY_ENTER) && !strncmp(get_field("r1:mode")->value, "FT", 2) && f->value[0] != COMMAND_ESCAPE){
			ft8_tx(f->value, field_int("TX_PITCH"));
			f->value[0] = 0;		
		}
		else if (a >= ' ' && a <= 127 && strlen(f->value) < f->max-1){
			int l = strlen(f->value);
			f->value[l++] = a;
			f->value[l] = 0;
		}
		//handle ascii delete 8 or gtk 
		else if ((a == MIN_KEY_BACKSPACE || a == 8) && strlen(f->value) > 0){
			int l = strlen(f->value) - 1;
			f->value[l] = 0;
		}
		f->is_dirty = 1;
		f->update_remote = 1;
		f->updated_at = millis();
		f_last_text = f; 
		return 1;
	}
	else if (event == FIELD_DRAW){
		//if (f_focus == f)
		//	fill_rect(gfx, f->x, f->y, f->width,f->height, COLOR_FIELD_SELECTED);
		//else
		//	fill_rect(gfx, f->x, f->y, f->width,f->height, COLOR_BACKGROUND);

		//rect(gfx, f->x, f->y, f->width-1,f->height, COLOR_CONTROL_BOX, 1);
		//text_length = strlen(f->value);
		//line_start = 0;
		//y = f->y + 1;
		//text_line_width= measure_text(gfx, f->value, f->font_index);
		//if (!strlen(f->value))
		//	draw_text(gfx, f->x + 1, y+1, f->label, STYLE_FIELD_LABEL);
		//else 
		//	draw_text(gfx, f->x + 1, y+1, f->value, f->font_index);
		////draw the text cursor, if there is no text, the text baseline is zero
		//if (f_focus == f){
		//	fill_rect(gfx, f->x + text_line_width+3, y+16, 9, 2, COLOR_SELECTED_BOX);
		//}
		
		return 1;
	}
	return 0;
}

int do_pitch(struct field *f, int event, int a, int b, int c){

	int	v = atoi(f->value);

	if (event == FIELD_EDIT){
		if (a == MIN_KEY_UP && v + f->step <= f->max){
			v += f->step;
		}
		else if (a == MIN_KEY_DOWN && v - f->step >= f->min){
			v -= f->step;
		}
		sprintf(f->value, "%d", v);
		update_field(f);
		modem_set_pitch(v);
		char buff[20], response[20];
		sprintf(buff, "rx_pitch=%d", v);
		sdr_request(buff, response);

		//move the bandwidth accordingly
  	int mode = mode_id(get_field("r1:mode")->value);
		int bw = 4000;
		switch(mode){
			case MODE_CW:
			case MODE_CWR:
				bw = field_int("BW_CW");
				break;
			case MODE_USB:
			case MODE_LSB:
				bw = field_int("BW_VOICE");
				break;
			case MODE_FT4:
			case MODE_FT8:
				bw = 4000;
				break;	
			default:
				bw = field_int("BW_DIGITAL");
		}
		set_filter_high_low(bw);
		return 1;
	}
		
	return 0;
}

int do_bandwidth(struct field *f, int event, int a, int b, int c){

	int	v = atoi(f->value);

	if (event == FIELD_EDIT){
		if (a == MIN_KEY_UP && v + f->step <= f->max){
			v += f->step;
		}
		else if (a == MIN_KEY_DOWN && v - f->step >= f->min){
			v -= f->step;
		}
		sprintf(f->value, "%d", v);
		update_field(f);
		modem_set_pitch(v);
		char buff[20], response[20];
		sprintf(buff, "rx_pitch=%d", v);
		sdr_request(buff, response);
		set_filter_high_low(v);
		save_bandwidth(v);
		return 1;
	}
		
	return 0;
}

static char tune_tx_saved_mode[100]={0};

//called for RIT as well as the main tuning
int do_tuning(struct field *f, int event, int a, int b, int c){

	static struct timespec last_change_time, this_change_time;

	int	v = atoi(f->value);
  int temp_tuning_step = tuning_step;

	if (event == FIELD_EDIT){

  if (!strcmp(get_field("tuning_acceleration")->value, "ON")){
    clock_gettime(CLOCK_MONOTONIC_RAW, &this_change_time);
    uint64_t delta_us = (this_change_time.tv_sec - last_change_time.tv_sec) * 1000000 + (this_change_time.tv_nsec - last_change_time.tv_nsec) / 1000;
    char temp_char[100];
    //sprintf(temp_char, "delta: %d", delta_us);
    //strcat(temp_char,"\r\n");
    //write_console(STYLE_LOG, temp_char);
    clock_gettime(CLOCK_MONOTONIC_RAW, &last_change_time);
    if (delta_us < atof(get_field("tuning_accel_thresh2")->value)){
      if (tuning_step < 10000){
        tuning_step = tuning_step * 100;
        //sprintf(temp_char, "x100 activated\r\n");
        //write_console(STYLE_LOG, temp_char);
      }
    } else if (delta_us < atof(get_field("tuning_accel_thresh1")->value)){
      if (tuning_step < 1000){
        tuning_step = tuning_step * 10;
        //printf(temp_char, "x10 activated\r\n");
        //write_console(STYLE_LOG, temp_char);
      }
    }
  }

		if (a == MIN_KEY_UP && v + f->step <= f->max){
			//this is tuning the radio
			if (!strcmp(get_field("#rit")->value, "ON")){
				struct field *f = get_field("#rit_delta");
				int rit_delta = atoi(f->value);
				if(rit_delta < MAX_RIT){
					rit_delta += tuning_step;
					char tempstr[100];
					sprintf(tempstr, "%d", rit_delta);
					set_field("#rit_delta", tempstr);
				}
				else
					return 1;
			}
			else
				v = (v / tuning_step + 1)*tuning_step;
		}
		else if (a == MIN_KEY_DOWN && v - f->step >= f->min){
			if (!strcmp(get_field("#rit")->value, "ON")){
				struct field *f = get_field("#rit_delta");
				int rit_delta = atoi(f->value);
				if (rit_delta > -MAX_RIT){
					rit_delta -= tuning_step;
					char tempstr[100];
					sprintf(tempstr, "%d", rit_delta);
					set_field("#rit_delta", tempstr);
				}
				else
					return 1;
			}
			else
				v = (v / tuning_step - 1)*tuning_step;
			abort_tx();
		}
		
		sprintf(f->value, "%d",  v);
		tuning_step = temp_tuning_step;
		//send the new frequency to the sbitx core
		char buff[100];
		//sprintf(buff, "%s=%s", f->cmd, f->value);
		sprintf(buff, "%s %s", f->label, f->value);
		do_control_action(buff);
		//update the GUI
		update_field(f);
		settings_updated++;
		//leave it to us, we have handled it

		return 1;
	}
	else if (event == FIELD_DRAW){
			//draw_dial(f, gfx);

			return 1; 
	}
	return 0;	
}

int do_kbd(struct field *f, int event, int a, int b, int c){
	if (event == FIELD_DRAW){
		//int label_height = font_table[STYLE_FIELD_LABEL].height;
		//int width = measure_text(gfx, f->label, STYLE_FIELD_LABEL);
		//int offset_x = f->x + f->width/2 - width/2;
		//int label_y;
		//int value_font;

		//fill_rect(gfx, f->x, f->y, f->width,f->height, COLOR_BACKGROUND);
		//rect(gfx, f->x, f->y, f->width,f->height, COLOR_CONTROL_BOX, 1);
		////is it a two line display or a single line?
		//if (!f->value[0]){
		//	label_y = f->y + (f->height - label_height)/2;
		//	draw_text(gfx, offset_x,label_y, f->label, STYLE_FIELD_LABEL);
		//} 
		//else {
		//	if(width >= f->width+2)
		//		value_font = STYLE_SMALL_FIELD_VALUE;
		//	else
		//		value_font = STYLE_FIELD_VALUE;
		//	int value_height = font_table[value_font].height;
		//	label_y = f->y +3;
		//	draw_text(gfx, f->x + 3, label_y, f->label, STYLE_FIELD_LABEL);
		//	width = measure_text(gfx, f->value, value_font);
		//	label_y = f->y + (f->height - label_height)/2;
		//	draw_text(gfx, f->x + f->width/2 - width/2, label_y, f->value, value_font);
		//}
		return 1;
	}	
	return 0;
}


int do_toggle_kbd(struct field *f, int event, int a, int b, int c){
	return 0;
}

void open_url(char *url){
	char temp_line[200];

	sprintf(temp_line, "xdg-open  %s"
	"  >/dev/null 2> /dev/null &", url);
	execute_app(temp_line);
}

void qrz(const char *callsign){
	char 	url[1000];

	sprintf(url, "https://qrz.com/DB/%s &", callsign);
	open_url(url);
}

int do_macro(struct field *f, int event, int a, int b, int c){
	char buff[256], *mode;
	char contact_callsign[100];

	strcpy(contact_callsign, get_field("#contact_callsign")->value);

	if(event == FIELD_UPDATE){
		int fn_key = atoi(f->cmd+3); // skip past the '#mf' and read the function key number

	 	macro_exec(fn_key, buff);
	
		mode = get_field("r1:mode")->value;

		if (!strncmp(get_field("r1:mode")->value, "FT", 2) && strlen(buff)){
			ft8_tx(buff, atoi(get_field("#tx_pitch")->value));
			set_field("#text_in", "");
			//write_console(STYLE_LOG_TX, buff);
		}
		else if (strlen(buff)){
			set_field("#text_in", buff);
			//put it in the text buffer and hope it gets transmitted!
		}
		return 1;
	}
	else if (event == FIELD_DRAW){
		//int width, offset, text_length, line_start, y;	
		//char this_line[MAX_FIELD_LENGTH];
		//int text_line_width = 0;

		//fill_rect(gfx, f->x, f->y, f->width,f->height, COLOR_BACKGROUND);
		//rect(gfx, f->x, f->y, f->width,f->height, COLOR_CONTROL_BOX, 1);

		//width = measure_text(gfx, f->label, STYLE_FIELD_LABEL);
		//offset = f->width/2 - width/2;
		//if (strlen(f->value) == 0)
		//	draw_text(gfx, f->x +5, f->y+13 , f->label , STYLE_FIELD_LABEL);
		//else {
		//	if (strlen(f->label)){
		//		draw_text(gfx, f->x+5, f->y+5 ,  f->label, STYLE_FIELD_LABEL);
		//		draw_text(gfx, f->x+5 , f->y+f->height - 20 , f->value , f->font_index);
		//	}
		//	else
		//		draw_text(gfx, f->x+offset , f->y+5, f->value , f->font_index);
		//	}	
		return 1;
	}

	return 0;
}

int do_record(struct field *f, int event, int a, int b, int c){
	if (event == FIELD_DRAW){

		//if (f_focus == f)
		//	rect(gfx, f->x, f->y, f->width-1,f->height, COLOR_SELECTED_BOX, 2);
		//else if (f_hover == f)
		//	rect(gfx, f->x, f->y, f->width,f->height, COLOR_SELECTED_BOX, 1);
		//else 
		//	rect(gfx, f->x, f->y, f->width,f->height, COLOR_CONTROL_BOX, 1);

		//int width = measure_text(gfx, f->label, STYLE_FIELD_LABEL);
		//int offset = f->width/2 - width/2;
		//int	label_y = f->y + ((f->height 
		//	- font_table[STYLE_FIELD_LABEL].height - 5  
		//	- font_table[STYLE_FIELD_VALUE].height)/2);
		//draw_text(gfx, f->x + offset, label_y, f->label, STYLE_FIELD_LABEL);


		//char duration[12];
		//label_y += font_table[STYLE_FIELD_LABEL].height;

		//if (record_start){
		//	width = measure_text(gfx, f->value, f->font_index);
		//	offset = f->width/2 - width/2;
		//	time_t duration_seconds = time(NULL) - record_start;
		//	int minutes = duration_seconds/60;
		//	int seconds = duration_seconds % 60;
		//	sprintf(duration, "%d:%02d", minutes, seconds); 	
		//}
		//else
		//	strcpy(duration, "OFF");
		//width = measure_text(gfx, duration, STYLE_FIELD_VALUE);
		//draw_text(gfx, f->x + f->width/2 - width/2, label_y, duration, f->font_index);
		return 1;
	}
	return 0;
}

void tx_on(int trigger){
	char response[100];
	struct field *f_batt = get_field("#batt");
	
	if (atoi(f_batt->value) > 900 && sbitx_version == 4){
		//write_console(STYLE_LOG, "Reduce the power supply voltage to transmit\n");
		return;
	}

	if (trigger != TX_SOFT && trigger != TX_PTT){
		puts("Error: tx_on trigger should be SOFT or PTT");
		return;
	}

	struct field *f = get_field("r1:mode");
	
	if (f){
		if (!strcmp(f->value, "CW"))
			tx_mode = MODE_CW;
		else if (!strcmp(f->value, "CWR"))
			tx_mode = MODE_CWR;
		else if (!strcmp(f->value, "USB"))
			tx_mode = MODE_USB;
		else if (!strcmp(f->value, "LSB"))
			tx_mode = MODE_LSB;
		else if (!strcmp(f->value, "NBFM"))
			tx_mode = MODE_NBFM;
		else if (!strcmp(f->value, "AM"))
			tx_mode = MODE_AM;
		else if (!strcmp(f->value, "2TONE"))
			tx_mode = MODE_2TONE;
		else if (!strcmp(f->value, "DIGITAL"))
			tx_mode = MODE_DIGITAL;
		else if (!strcmp(f->value, "TUNE"))
			tx_mode = MODE_TUNE;
	}

	if (in_tx == 0){
		char response[20];
		in_tx = trigger; //can be PTT or softswitch
		struct field *freq = get_field("r1:freq");
		set_operating_freq(atoi(freq->value), response);
		sdr_request("tx=on", response);
		update_field(get_field("r1:freq"));
		//printf("TX\n");
		//tlog("tx_on", freq->value, trigger);
	}

	tx_start_time = millis();
}

void tx_off(){
	char response[100];

	modem_abort();

	if (in_tx){
		sdr_request("tx=off", response);	
		in_tx = 0;
		sdr_request("key=up", response);
		char response[20];
		struct field *freq = get_field("r1:freq");
		set_operating_freq(atoi(freq->value), response);
		update_field(get_field("r1:freq"));
		//printf("RX\n");
		//tlog("tx_off", response, millis()-tx_start_time);
	}
	sound_input(0); //it is a low overhead call, might as well be sure
}

void set_ui(int id){
	struct field *f = get_field("#kbd_q");

	if (id == LAYOUT_KBD){
		// the "#kbd" is out of screen, get it up and "#mf" down
		for (int i = 0; active_layout[i].cmd[0] > 0; i++){
			if (!strncmp(active_layout[i].cmd, "#kbd", 4) && active_layout[i].y > 1000)
				active_layout[i].y -= 1000;
			else if (!strncmp(active_layout[i].cmd, "#mf", 3) && active_layout[i].y < 1000)
				active_layout[i].y += 1000;
			active_layout[i].is_dirty = 1;	
		}
	}
	if (id == LAYOUT_MACROS) {
		// the "#mf" is out of screen, get it up and "#kbd" down
		for (int i = 0; active_layout[i].cmd[0] > 0; i++){
			if (!strncmp(active_layout[i].cmd, "#kbd", 4) && active_layout[i].y < 1000)
				active_layout[i].y += 1000;
			else if (!strncmp(active_layout[i].cmd, "#mf", 3) && active_layout[i].y > 1000)
				active_layout[i].y -= 1000;
			active_layout[i].is_dirty = 1;	
		}
	}
	current_layout = id;
}

/* hardware specific routines */

void init_gpio_pins(){
	for (int i = 0; i < 15; i++){
		pinMode(pins[i], INPUT);
		pullUpDnControl(pins[i], PUD_UP);
	}

	pinMode(PTT, INPUT);
	pullUpDnControl(PTT, PUD_UP);
	pinMode(DASH, INPUT);
	pullUpDnControl(DASH, PUD_UP);
}

int key_poll(){
	int key = CW_IDLE;
	//int input_method = get_cw_input_method();
	if (cw_input == NULL){
		printf("cw_input field must point to the CW_INPUT field\n");
		return 0;
	}

	//quick look up of one of the three values of keying type
	//STRAIG[H]T
	//IAMBIC[\0]
	//IAMBIC[B]

	int input_method = CW_IAMBIC; 
	switch(cw_input->value[6]){
		case 0:
			input_method = CW_IAMBIC;
			break;
		case 'H':
			input_method = CW_STRAIGHT;
			break;
		case 'B':
			input_method = CW_IAMBICB;
			break;
	}
	

	if (input_method == CW_IAMBIC || input_method == CW_IAMBICB){	
		if (ptt_state == LOW)
		//if (digitalRead(PTT) == LOW)
			key |= CW_DASH;
		if (dash_state == LOW)
		//if (digitalRead(DASH) == LOW)
			key |= CW_DOT;
	}
	//straight key
	else if (ptt_state == LOW || dash_state == LOW)
			key = CW_DOWN;

	return key;
}

void enc_init(struct encoder *e, int speed, int pin_a, int pin_b){
	e->pin_a = pin_a;
	e->pin_b = pin_b;
	e->speed = speed;
	e->history = 5;
}

int enc_state (struct encoder *e) {
	return (digitalRead(e->pin_a) ? 1 : 0) + (digitalRead(e->pin_b) ? 2: 0);
}

int enc_read(struct encoder *e) {
  int result = 0; 
  int newState;
  
  newState = enc_state(e); // Get current state  
    
  if (newState != e->prev_state)
     delay (1);
  
  if (enc_state(e) != newState || newState == e->prev_state)
    return 0; 

  //these transitions point to the encoder being rotated anti-clockwise
  if ((e->prev_state == 0 && newState == 2) || 
    (e->prev_state == 2 && newState == 3) || 
    (e->prev_state == 3 && newState == 1) || 
    (e->prev_state == 1 && newState == 0)){
      e->history--;
      //result = -1;
    }
  //these transitions point to the enccoder being rotated clockwise
  if ((e->prev_state == 0 && newState == 1) || 
    (e->prev_state == 1 && newState == 3) || 
    (e->prev_state == 3 && newState == 2) || 
    (e->prev_state == 2 && newState == 0)){
      e->history++;
    }
  e->prev_state = newState; // Record state for next pulse interpretation
  if (e->history > e->speed){
    result = 1;
    e->history = 0;
  }
  if (e->history < -e->speed){
    result = -1;
    e->history = 0;
  }
  return result;
}

static int tuning_ticks = 0;
void tuning_isr(void){
	int tuning = enc_read(&enc_b);
	if (tuning < 0)
		tuning_ticks++;
	if (tuning > 0)
		tuning_ticks--;	
}

void key_isr(void){
	dash_state = digitalRead(DASH);
	ptt_state = digitalRead(PTT);
}

void hw_init(){
	wiringPiSetup();
	init_gpio_pins();

	enc_init(&enc_a, ENC_FAST, ENC1_B, ENC1_A);
	enc_init(&enc_b, ENC_FAST, ENC2_A, ENC2_B);

//	int e = g_timeout_add(1, ui_tick, NULL);

	wiringPiISR(ENC2_A, INT_EDGE_BOTH, tuning_isr);
	wiringPiISR(ENC2_B, INT_EDGE_BOTH, tuning_isr);
	wiringPiISR(PTT, INT_EDGE_BOTH, key_isr);
	wiringPiISR(DASH, INT_EDGE_BOTH, key_isr);
}

void hamlib_tx(int tx_input){
  if (tx_input){
    sound_input(1);
		tx_on(TX_SOFT);
	}
  else {
    sound_input(0);
		tx_off();
	}
}

int get_cw_delay(){
	return atoi(get_field("#cwdelay")->value);
}

int get_cw_input_method(){
	struct field *f = get_field("#cwinput");
	if (!strcmp(f->value, "KEYBOARD"))
		return CW_KBD;
	else if (!strcmp(f->value, "IAMBIC"))
		return CW_IAMBIC;
	else if (!strcmp(f->value, "IAMBICB"))
		return CW_IAMBICB;
	else
		return CW_STRAIGHT;
}

int get_pitch(){
	struct field *f = get_field("rx_pitch");
	return atoi(f->value);
}

int get_cw_tx_pitch(){
	struct field *f = get_field("#tx_pitch");
	return atoi(f->value);
}

long get_freq(){
	return atol(get_field("r1:freq")->value);
}

int  web_get_console(char *buff, int max){
	char c;
	int i;

	if (q_length(&q_web) == 0)
		return 0;
	//sprintf(buff,"underflow %d  overflow %d  max_q %d len %d",
	//	q_web.underflow, q_web.overflow, q_web.max_q, q_length(&q_web));
	//tlog("web_get_console", buff, max);
	strcpy(buff, "CONSOLE ");
	buff += strlen("CONSOLE ");
	for (i = 0; (c = q_read(&q_web)) && i < max; i++){
		if (c < 128 && c >= ' ')
			*buff++ = c;
	}
	*buff = 0;
	return i;
}


void web_get_spectrum(char *buff){

  int n_bins = (int)((1.0 * spectrum_span) / 46.875);
  //the center frequency is at the center of the lower sideband,
  //i.e, three-fourth way up the bins.
  int starting_bin = (3 *MAX_BINS)/4 - n_bins/2;
  int ending_bin = starting_bin + n_bins;

  int j = 3;
  if (in_tx){
    strcpy(buff, "TX ");
    for (int i = 0; i < MOD_MAX; i++){
      int y = (2 * mod_display[i]) + 32;
      if (y > 127)
        buff[j++] = 127;
      else if(y > 0 && y <= 95)
        buff[j++] = y + 32;
      else
        buff[j++] = ' ';
    }
  }
  else{
    strcpy(buff, "RX ");
    for (int i = starting_bin; i <= ending_bin; i++){
      int y = spectrum_plot[i] + waterfall_offset;
      if (y > 95)
        buff[j++] = 127;
      else if(y >= 0 )
        buff[j++] = y + 32;
      else
        buff[j++] = ' ';
    }
  }

  buff[j++] = 0;
  return;
}

void set_radio_mode(char *mode){
	char umode[10], request[100], response[100];
	int i;

	for (i = 0; i < sizeof(umode) - 1 && *mode; i++)
		umode[i] = toupper(*mode++);
	umode[i] = 0;

	sprintf(request, "r1:mode=%s", umode);
	sdr_request(request, response);
	if (strcmp(response, "ok")){
		printf("mode %d: unavailable\n", umode);
		return;
	}
	int new_bandwidth = 3001;
	switch(mode_id(umode)){
		case MODE_CW:
		case MODE_CWR:
			new_bandwidth = field_int("BW_CW");
			break;
		case MODE_LSB:
		case MODE_USB:
		case MODE_AM:
			new_bandwidth = field_int("BW_VOICE");
			break;
		case MODE_FT4:
		case MODE_FT8:
			new_bandwidth = 4000;
			break;
		default:
			new_bandwidth = field_int("BW_DIGITAL");
	}
	struct field *f = get_field_by_label("MODE");
	if (strcmp(f->value, umode))
		field_set("MODE", umode);
	//let the bw control trigger the filter
	char bw_str[10];
	sprintf(bw_str, "%d", new_bandwidth);
	field_set("BW", bw_str);

}

void zbitx_write(int style, char *text){
	char buffer[256];

	if (!zbitx_available){
		return;
	}

	if (strlen(text) > sizeof(buffer) - 10){
		printf("*zbitx_write update is oversized\n");
		return;
	}
	snprintf(buffer, sizeof(buffer), "%d %s", old_style_font(style), text);
	char *p = buffer;		
	if (q_zbitx_console.overflow)
		q_empty(&q_zbitx_console);
	while (*p)
		q_write(&q_zbitx_console, *p++);
	q_write(&q_zbitx_console, 0);
}

//cramp all the spectrum into 250 points
void zbitx_get_spectrum(char *buff){

  int n_bins = (int)((1.0 * spectrum_span) / 46.875);
  //the center frequency is at the center of the lower sideband,
  //i.e, three-fourth way up the bins.
  int starting_bin = (3 *MAX_BINS)/4 - n_bins/2;
  int ending_bin = starting_bin + n_bins;

  int j;
  if (in_tx){
    strcpy(buff, "WF ");
		j = strlen(buff);
		float step = MOD_MAX/250.0;
		//printf("wf on tx %d / %d", step, MOD_MAX);
		for (float i = 0; i < MOD_MAX; i+= step){
      int y = (2 * mod_display[(int)i]) + 32;
      if (y > 127)
        buff[j] = 127;
			else if (y < 32)
				buff[j] = ' ';
      else
        buff[j] = y;
			j++;
    }
  }
  else{
    strcpy(buff, "WF ");
		j = strlen(buff);
		float step = (1.0  * (ending_bin - starting_bin))/250.0;
		float i = 1.0 * starting_bin;
    while(i <= (int) ending_bin){
      int y = spectrum_plot[(int)i] + waterfall_offset;
      if (y > 95)
        buff[j++] = 127;
      else if(y >= 0 )
        buff[j++] = y + 32;
      else
        buff[j++] = ' ';
			i += step;
    }
  }

  buff[j++] = 0;
//	if (in_tx)
//		printf("%s : %d\n", buff, strlen(buff)); 
  return;
}

static void zbitx_logs(){
	char logbook_path[PATH_MAX];
	char row_response[1000], row[1000];
	char query[100];
	char args[100];
	int	row_id;

	printf("Sending the last 50 log entries to zbitx\n");	
	query[0] = 0;
	row_id = -1;
	logbook_query(NULL, row_id, logbook_path);
	FILE *pf = fopen(logbook_path, "r");
	if (!pf)
		return;
	while(fgets(row, sizeof(row), pf)){
		sprintf(row_response, "QSO %s}", row);
		//printf(row_response);
		i2cbb_write_i2c_block_data(ZBITX_I2C_ADDRESS, '{', strlen(row_response), row_response);
	}
	fclose(pf);
}

void zbitx_poll(int all){
	char buff[3000];
	static unsigned int last_update = 0;
	static int wf_update = 1;

	int count = 0;
	int e = 0;
	int retry;
	unsigned int this_time = millis();

	for (int i = 0; active_layout[i].cmd[0] > 0; i++){
		struct field *f = active_layout+i;
		if (!strcmp(f->label, "WATERFALL") || !strcmp(f->label, "SPECTRUM") || !strcmp(f->label, "CONSOLE"))
			continue;
		if (all || f->updated_at >  last_update){
			sprintf(buff, "%s %s}", f->label, f->value);
			retry = 3;
			do {
				e = i2cbb_write_i2c_block_data(ZBITX_I2C_ADDRESS, '{', strlen(buff), buff);
				if (!e){
					if (retry < 3)
						printf("Sucess on %d\n", retry);
					break;
				}
				delay(3);
				printf("Retrying I2C %d\n", retry);
			}while(retry--);
			f->update_remote = 0;
			count++;
			delay(10);
		}
	}
	last_update = this_time;
	
	//check if the console q has any new updates
	while (q_length(&q_zbitx_console) > 0){
		char remote_cmd[1000];
		int c, i;

		i = 0;
		while(i < sizeof(remote_cmd)-3 && (c = q_read(&q_zbitx_console)) >= ' ')
			remote_cmd[i++] = c;
		remote_cmd[i++] = '}';
		remote_cmd[i++] = 0;
 	
		e = i2cbb_write_i2c_block_data(ZBITX_I2C_ADDRESS, '{', 
			strlen(remote_cmd), remote_cmd);
	}

	if (wf_update){
		zbitx_get_spectrum(buff);
		strcat(buff, "}"); //terminate the block
		//spectrum can be lost mometarily, it is alright	
		delay(1);
		i2cbb_write_i2c_block_data(0x0a, '{', strlen(buff), buff);
	}

	//transmit in_tx
	sprintf(buff, "IN_TX %d}", in_tx);
	delay(1);
	i2cbb_write_i2c_block_data(0x0a, '{', strlen(buff), buff);


	if(update_logs){
		zbitx_logs();
		update_logs = 0;
	}

	int  reply_length;

	if ((reply_length = i2cbb_read_rll(0xa, buff)) != -1){
	//zero terminate the reply
		buff[reply_length] = 0;

		if(!strncmp(buff, "FT8 ", 4)){
			char ft8_message[100];
			hd_strip_decoration(ft8_message, buff);
			//ft8_process(ft8_message, FTX_START_QSO);
			printf("FT4/8 from zbitx: %s\n", ft8_message);
			remote_execute(ft8_message);
		}
		else if (!strcmp(buff, "WF ON"))
			wf_update = 1;
		else if (!strcmp(buff, "WF OFF"))
			wf_update = 0;
		else if (!strncmp(buff, "SHUTDOWN", 8)) {
			printf("Shutting down system...\n");
			system("sudo /sbin/shutdown -h now");
		}
		else{
			if (!strncmp(buff, "OPEN", 4)){
				update_logs = 1;
				printf("<<<< refresh the log >>>>>\n");
			}
			remote_execute(buff);
		}
	}
	last_update = this_time;
}

void zbitx_init(){
	char buff[100];
	sprintf(buff, "9 %s}", VER_STR);
 	int e = i2cbb_write_i2c_block_data (ZBITX_I2C_ADDRESS, '{', 
		strlen(buff), buff);


	if (!e){
		printf("zBitx front panel detected\n");
		zbitx_available = 1;


 		e = i2cbb_write_i2c_block_data (ZBITX_I2C_ADDRESS, '{', 
		strlen(VER_STR), VER_STR);

		FILE *pf = popen("hostname -I", "r");
		if (pf){
			char ip_str[100], buff[100];
			fgets(ip_str, 100, pf);
			pclose(pf);
			//terminate the string at the first space
			char *p = strchr(ip_str, ' ');
			if (p){
				*p = 0;
				sprintf(buff, "9 \nzBitx on http://%s\n}", ip_str);
 				i2cbb_write_i2c_block_data (ZBITX_I2C_ADDRESS, '{', 
					strlen(buff), buff);
			}
		}
	}
}

bool ui_tick(){
	int static ticks = 0;

	ticks++;

	while (q_length(&q_remote_commands) > 0){
		//read each command until the 
		char remote_cmd[1000];
		int c, i;
		for (i = 0; i < sizeof(remote_cmd)-2 &&  (c = q_read(&q_remote_commands)) >= ' '; i++){
			remote_cmd[i] = c;
		}
		remote_cmd[i] = 0;

		//echo the keystrokes for chatty modes like cw/rtty/psk31/etc
		if (!strncmp(remote_cmd, "key ", 4))
			for (int i = 4; remote_cmd[i] > 0; i++)
				edit_field(get_field("#text_in"), remote_cmd[i]);	
		else if (strlen(remote_cmd)){
			cmd_exec(remote_cmd);
			settings_updated = 1; //save the settings
		}
	}

	// check the tuning knob
	struct field *f = get_field("r1:freq");

	while (tuning_ticks > 0){
		edit_field(f, MIN_KEY_DOWN);
		tuning_ticks--;
    //sprintf(message, "tune-\r\n");
    //write_console(STYLE_LOG, message);

	}

	while (tuning_ticks < 0){
		edit_field(f, MIN_KEY_UP);
		tuning_ticks++;
    //sprintf(message, "tune+\r\n");
    //write_console(STYLE_LOG, message);
	}


	//the modem tick is called on every tick
	//each modem has to optimize for efficient operation

 	modem_poll(mode_id(f_mode->value), ticks);

/*
	if (ticks % 20 == 0){
	}
*/

	int tick_count = 50;
	switch(mode_id(f_mode->value)){
		case MODE_CW:
		case MODE_CWR:
			tick_count = 50;
			break;
		case MODE_FT4:
		case MODE_FT8:
			tick_count = 200;
			break;
		default:
			tick_count = 100; 
	}
	if (ticks >= tick_count){

		char response[6], cmd[10];
		cmd[0] = 1;

		if (zbitx_available)
			zbitx_poll(0);

		if(in_tx){
			char buff[10];

			sprintf(buff,"%d", fwdpower);
			set_field("#fwdpower", buff);		
			sprintf(buff, "%d", vswr);
			set_field("#vswr", buff);
		}

		struct field *f = get_field("spectrum");
		update_field(f);	//move this each time the spectrum watefall index is moved
		f = get_field("waterfall");
		update_field(f);

		if (digitalRead(ENC1_SW) == 0){
			//flip between mode and volume
			if (f_focus && !strcmp(f_focus->label, "AUDIO"))
				focus_field(get_field("r1:mode"));
			else
				focus_field(get_field("r1:volume"));
			printf("Focus is on %s\n", f_focus->label);
		}

		if (record_start)
			update_field(get_field("#record"));

    // check if low and high settings are stepping on each other
    char new_value[20];
    while (atoi(get_field("r1:low")->value) > atoi(get_field("r1:high")->value)){
      sprintf(new_value, "%d", atoi(get_field("r1:high")->value)+get_field("r1:high")->step);
      set_field("r1:high",new_value);
    }

    int cursor_type;

		ticks = 0;
  }
	//update_field(get_field("#text_in")); //modem might have extracted some text

  hamlib_slice();
	remote_slice();
	save_user_settings(0);

 
	//straight key in CW
	if (f && (!strcmp(f_mode->value, "2TONE") || !strcmp(f_mode->value, "LSB") 
	|| !strcmp(f_mode->value, "AM") || !strcmp(f_mode->value, "USB"))){
		if (ptt_state == LOW && in_tx == 0)
			tx_on(TX_PTT);
		else if (ptt_state == HIGH && in_tx  == TX_PTT)
			tx_off();
	}

	int scroll = enc_read(&enc_a);
	if (scroll && f_focus){
		if (scroll < 0)
			edit_field(f_focus, MIN_KEY_DOWN);
		else
			edit_field(f_focus, MIN_KEY_UP);
	}	
	return TRUE;
}

void ui_init(int argc, char *argv[]){
 
	q_init(&q_web, 5000);
	q_init(&q_zbitx_console, 1000);

	webserver_start();
	f_last_text = get_field_by_label("TEXT");
}

int get_tx_data_byte(char *c){
	//take out the first byte and return it to the modem
	struct field *f = get_field("#text_in");
	int length = strlen(f->value);

	if (f->value[0] == '\\' || !length)
		return 0;
	if (length){
		*c = f->value[0];
		//now shift the buffer down, hopefully, this copies the trailing null too
		for (int i = 0; i < length; i++)
			f->value[i] = f->value[i+1];
	}
	f->is_dirty = 1;
	f->update_remote = 1;
	f->updated_at = millis();
	//update_field(f);
	return length;
}

int get_tx_data_length(){
	struct field *f = get_field("#text_in");

	if (strlen(f->value) == 0)
		return 0;

	if (f->value[0] != COMMAND_ESCAPE)
		return strlen(get_field("#text_in")->value);
	else
		return 0;
}

int is_in_tx(){
	return in_tx;
}


/* handle the ui request and update the controls */

void change_band(char *request){
	int i, old_band, new_band; 
	int max_bands = sizeof(band_stack)/sizeof(struct band);
	long new_freq, old_freq;
	char buff[100];

	//find the band that has just been selected, the first char is #, we skip it
	for (new_band = 0; new_band < max_bands; new_band++)
		if (!strcmp(request, band_stack[new_band].name))
			break;

	//continue if the band is legit
	if (new_band == max_bands)
		return;

	// find out the tuned frequency
	struct field *f = get_field("r1:freq");
	old_freq = atol(f->value);
	f = get_field("r1:mode");
	int old_mode = mode_id(f->value);
	if (old_mode == -1)
		return;

	//first, store this frequency in the appropriate bin
	for (old_band = 0; old_band < max_bands; old_band++)
		if (band_stack[old_band].start <= old_freq && old_freq <= band_stack[old_band].stop)
				break;

	int stack = 0;
	if (old_band < max_bands) {
		stack = band_stack[old_band].index;
		if (stack < 0 || stack >= STACK_DEPTH)
			stack = 0;
		//update the old band setting 
		if (stack >= 0 && stack < STACK_DEPTH){
				band_stack[old_band].freq[stack] = old_freq;
				band_stack[old_band].mode[stack] = old_mode;
		}
	}

	//if we are still in the same band, move to the next position
	if (new_band == old_band){
		stack = ++band_stack[new_band].index;
		//move the stack and wrap the band around
		if (stack >= STACK_DEPTH)
			stack = 0;
		band_stack[new_band].index = stack;
	}
	stack = band_stack[new_band].index;
	sprintf(buff, "%d", band_stack[new_band].freq[stack]);
	char resp[100];
	set_operating_freq(band_stack[new_band].freq[stack], resp);
	field_set("FREQ", buff);

	mode_name(band_stack[new_band].mode[stack], resp);
	field_set("MODE", resp);	
	update_field(get_field("r1:mode"));

	struct field *bandswitch = get_field_by_label(band_stack[new_band].name);
	sprintf(bandswitch->value, "%d", band_stack[new_band].index+1);
	set_field("#selband", buff);
	q_empty(&q_web);// inserted by llh 
  console_init(); // inserted by llh 
  // this fixes bug with filter settings not being applied after a band change, not sure why it's a bug - k3ng 2022-09-03

	abort_tx();
}

void meter_calibrate(){
	//we change to 40 meters, cw
	printf("starting meter calibration\n"
	"1. Attach a power meter and a dummy load to the antenna\n"
	"2. Adjust the drive until you see 40 watts on the power meter\n"
	"3. Press the tuning knob to confirm.\n");

	set_field("r1:freq", "7035000");
	set_radio_mode("CW");	
	struct field *f_bridge = get_field("bridge");
	set_field("bridge", "100");	
	focus_field(f_bridge);
}

void do_control_action(char *cmd){	
	char request[1000], response[1000], buff[100];

	strcpy(request, cmd);			//don't mangle the original, thank you

	if (!strcmp(request, "CLOSE")){
		//gtk_window_iconify(GTK_WINDOW(window));
	}
	else if (!strcmp(request, "OFF")){
		tx_off();
		set_field("#record", "OFF");
		save_user_settings(1);
		exit(0);
	}
	else if (!strncmp(request, "BW ",3)){
		int bw = atoi(request+3);	
		set_filter_high_low(bw); //calls do_control_action again to set LOW and HIGH
		//we have to save this as well
		save_bandwidth(bw);
	}
	else if (!strcmp(request, "WIPE"))
		call_wipe();
	else if (!strcmp(request, "ESC")){
		//empty the text buffer
		modem_abort();
		tx_off();
		call_wipe();
		field_set("TEXT", "");
		modem_abort();
		tx_off();
	}
	else if (!strcmp(request, "TX")){	
		tx_on(TX_SOFT);
	}
	else if (!strcmp(request, "WEB")){
		open_url("http://127.0.0.1:8080");
	}
	else if (!strcmp(request, "RX")){
		tx_off();
	}
	else if (!strncmp(request, "RIT", 3))
		update_field(get_field("r1:freq"));
	else if (!strncmp(request, "SPLIT", 5)){
		update_field(get_field("r1:freq"));	
		if (!strcmp(get_field("#vfo")->value, "B"))
			set_field("#vfo", "A");
	}
	else if (!strcmp(request, "VFO B")){
		struct field *f = get_field("r1:freq");
		struct field *vfo = get_field("#vfo");
		struct field *vfo_a = get_field("#vfo_a_freq");
		struct field *vfo_b = get_field("#vfo_b_freq");
		if (!strcmp(vfo->value, "B")){
			//vfo_a_freq = atoi(f->value);
			strcpy(vfo_a->value, f->value);
			//sprintf(buff, "%d", vfo_b_freq);
			set_field("r1:freq", vfo_b->value);
			settings_updated++;
		}
	}
	else if (!strcmp(request, "VFO A")){
		struct field *f = get_field("r1:freq");
		struct field *vfo = get_field("#vfo");
		struct field *vfo_a = get_field("#vfo_a_freq");
		struct field *vfo_b = get_field("#vfo_b_freq");
		//printf("vfo old %s, new %s\n", vfo->value, request);
		if (!strcmp(vfo->value, "A")){
		//	vfo_b_freq = atoi(f->value);
			strcpy(vfo_b->value, f->value);
	//		sprintf(buff, "%d", vfo_a_freq);
			set_field("r1:freq", vfo_a->value);
			settings_updated++;
		}
	}
	else if (!strcmp(request, "KBD ON")){
		//layout_ui();
	
	}
	else if (!strcmp(request, "KBD OFF")){
		//layout_ui();
	}
	else if (!strcmp(request, "SAVE")){
			enter_qso();
	}
	//tuning step
  else if (!strcmp(request, "STEP 1M"))
    tuning_step = 1000000;
	else if (!strcmp(request, "STEP 100K"))
		tuning_step = 100000;
	else if (!strcmp(request, "STEP 10K"))
		tuning_step = 10000;
	else if (!strcmp(request, "STEP 1K"))
		tuning_step = 1000;
	else if (!strcmp(request, "STEP 500H"))
		tuning_step = 500;
	else if (!strcmp(request, "STEP 100H"))
		tuning_step = 100;
	else if (!strcmp(request, "STEP 10H"))
		tuning_step = 10;

	//spectrum bandwidth
	else if (!strcmp(request, "SPAN 2.5K"))
		spectrum_span = 2500;
	else if (!strcmp(request, "SPAN 6K"))
		spectrum_span = 6000;
	else if (!strcmp(request, "SPAN 10K"))
		spectrum_span = 10000;
	else if (!strcmp(request, "SPAN 25K"))
		spectrum_span = 25000;
		
	//handle the band stacking
	else if (!strcmp(request, "80M") || 
		!strcmp(request, "60M") ||
		!strcmp(request, "40M") || 
		!strcmp(request, "30M") || 
		!strcmp(request, "20M") || 
		!strcmp(request, "17M") || 
		!strcmp(request, "15M") || 
		!strcmp(request, "12M") || 
		!strcmp(request, "10M")){
		change_band(request); 		
	}
	else if(!strcmp(request, "TUNE ON")){
		puts("Turning on TUNE");	
		strcpy(tune_tx_saved_mode, get_field("r1:mode")->value);
		//field_set("MODE", "TUNE");	
		//update_field(get_field("r1:mode"));
		//this is not correct, but ...
		char response[200];
		sdr_request("r1:mode=TUNE", response);
		delay(100);
		tx_on(TX_SOFT);
	}
	else if(!strcmp(request, "TUNE OFF")){
		puts("Turning off TUNE");
		tx_off();
		if (tune_tx_saved_mode[0]){
			field_set("MODE", tune_tx_saved_mode);	
			update_field(get_field("r1:mode"));
		}
	}
	else if (!strcmp(request, "REC ON")){
		char fullpath[PATH_MAX];

		char *path = getenv("HOME");
		time(&record_start);
		struct tm *tmp = localtime(&record_start);
		sprintf(fullpath, "%s/sbitx/audio/%04d%02d%02d-%02d%02d-%02d.wav", path, 
			tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec); 

		char request[300], response[100];
		sprintf(request, "record=%s", fullpath);
		sdr_request(request, response);
		sprintf(request, "Recording:%s\n", fullpath);
		write_console(STYLE_LOG, request);
	}
	else if (!strcmp(request, "REC OFF")){
		sdr_request("record", "off");
		write_console(STYLE_LOG, "Recording stopped\n");
		record_start = 0;
	}
	else if (!strcmp(request, "QRZ") && strlen(field_str("CALL")) > 0)
		qrz(field_str("CALL"));
	else {
		//send this to the radio core
		char args[MAX_FIELD_LENGTH];
		char exec[20];
		int i, j;

  	args[0] = 0;

		//copy the exec
		for (i = 0; *cmd > ' ' && i < sizeof(exec) - 1; i++)
			exec[i] = *cmd++;
		exec[i] = 0; 

		//skip the spaces
		while(*cmd == ' ')
			cmd++;

		j = 0;
		for (i = 0; *cmd && i < sizeof(args) - 1; i++){
			if (*cmd > ' ')
					j = i;
			args[i] = *cmd++;
		}
		args[++j] = 0;
		
		//translate the frequency of operating depending upon rit, split, etc.
		if (!strncmp(request, "FREQ", 4))
			set_operating_freq(atoi(request+5), response);
		else if (!strncmp(request, "MODE ", 5)){
			set_radio_mode(request+5);
			update_field(get_field("r1:mode"));
		}
		else{
			struct field *f = get_field_by_label(exec); 
			if (f){
				sprintf(request, "%s=%s", f->cmd, args);
				sdr_request(request, response);
			}
		}
	}
}

int get_ft8_callsign(const char* message, char* other_callsign) {
	int i = 0, j = 0, m = 0, len, cur_field = 0;
	char fields[4][32];
	other_callsign[0] = 0;
	len = (int)strlen(message);
	const char* mycall = field_str("MYCALLSIGN");
	while (i <= len) {
		if (message[i] == ' ' || message[i] == '\0' || j >= 31) {
			i++;
			while (i < len && message[i] == ' ') { i++; }
			if (m > 3) {
				break;
			}
			fields[m][j] = '\0';
			if (cur_field == 4) {
				if (strcmp(fields[m], "~")) {
					return -1;  // no tilde
				}
			}
			cur_field++;
			if (cur_field > 5) {
				m++;
			}
			j = 0;
		}
		else {
			fields[m][j++] = message[i];
			i++;
		}

		if (m > 4) {
			return -2; // to many fields
		}
	}
	if (cur_field < 7) {
		return -3; // to few fields
	}
	if (!strcmp(fields[0], "CQ")) {
		if (m == 4) {
			i = 2; // CQ xx callsign grid
		}
		else {
			i = 1; // CQ callsign grid
		}
	}
	else if (!strcmp(fields[0], mycall)) {
		i = 1; // mycallsign callsign
	}
	else if (!strcmp(fields[1], mycall)) {
		i = 0; // mycallsign callsign
	}
	else {
		i = 1; // callsign other -the one we hear
	}
	strcpy(other_callsign, fields[i]);
	return m;
}

void pre_ft8_check(char* message) {
	char result[500];
	char other_callsign[40];
	
	//printf("pre_ft8_check: message='%s'\n", message);
	if (get_ft8_callsign(message, other_callsign) >= 0) {
		//strcpy(result,"FT8_check_res ");
		int cnt = logbook_prev_log(other_callsign, result);
		char *p =strchr(message, '~');
		if (p) {
			strcat(result, p-1);
		}

		printf("pre_ft8_check: '%s'\n", result);

		if (strlen(result) > 127 ) {
			result[127] = 0;
		}
        int equal_last_check = strcmp(get_field("#ft8_check")->value, result);
		set_field("#ft8_check", result);

		if (cnt == 0 || equal_last_check == 0) {
			ft8_process(message, FTX_START_QSO);
		}
	}
}

/*
	These are user/remote entered commands.
	The command format is "CMD VALUE", the CMD is an all uppercase text
	that matches the label of a control.
	The value is taken as a string past the separator space all the way
	to the end of the string or new line, including any spaces.

	It also handles many commands that don't map to a control
	like metercal or txcal, etc.
*/
void cmd_exec(char *cmd){
	int i, j;
	int mode = mode_id(get_field("r1:mode")->value);

	char args[MAX_FIELD_LENGTH];
	char exec[20];

  args[0] = 0;

	//copy the exec
	for (i = 0; *cmd > ' ' && i < sizeof(exec) - 1; i++)
		exec[i] = *cmd++;
	exec[i] = 0; 

	//skip the spaces
	while(*cmd == ' ')
		cmd++;

	j = 0;
	for (i = 0; *cmd && i < sizeof(args) - 1; i++){
		if (*cmd > ' ')
				j = i;
		args[i] = *cmd++;
	}
	args[++j] = 0;

	char response[100];

	if (!strcmp(exec, "FT8")){
		ft8_process(args, FTX_START_QSO);
	}
	else if (!strcmp(exec, "FT8_check")) {
		pre_ft8_check(args);
	}
	else if (!strcmp(exec, "callsign")){
		strcpy(get_field("#mycallsign")->value,args); 
		sprintf(response, "\n[Your callsign is set to %s]\n", get_field("#mycallsign")->value);
		write_console(STYLE_LOG, response);
	}
	else if (!strcmp(exec, "QSODEL")){
		logbook_delete(atoi(args));
		update_logs = 1;
	}
	else if (!strcmp(exec, "power")){
		set_field("#fwdpower", args);
	}
	else if (!strcmp(exec, "vswr") && in_tx)
		set_field("#vswr", args);
	else if (!strcmp(exec, "vbatt"))
		set_field("#batt", args);
	else if (!strcmp(exec, "metercal")){
		meter_calibrate();
	}
	else if (!strcmp(exec, "abort"))
		abort_tx();
	else if (!strcmp(exec, "txcal")){
		char response[10];
		sdr_request("txcal=", response);
	}
	else if (!strcmp(exec, "grid")){	
		set_field("#mygrid", args);
		sprintf(response, "\n[Your grid is set to %s]\n", get_field("#mygrid")->value);
		write_console(STYLE_LOG, response);
	}
	else if (!strcmp(exec, "clear")){
		console_init();
	}
	else if(!strcmp(exec, "macro") || !strcmp(exec, "MACRO")){
		if (!strcmp(args, "list"))
			macro_list(NULL);
		else if (!macro_load(args, NULL)){
			set_ui(LAYOUT_MACROS);
			set_field("#current_macro", args);
		}
		else if (strlen(get_field("#current_macro")->value)){
			write_console(STYLE_LOG, "current macro is ");
			write_console(STYLE_LOG, get_field("#current_macro")->value);
			write_console(STYLE_LOG, "\n");
		}
		else
			write_console(STYLE_LOG, "macro file not loaded\n");
	}
	else if (!strcmp(exec, "qso"))
		enter_qso(args);
	else if (!strcmp(exec, "exchange")){
		set_field("#contest_serial", "0");
		set_field("#sent_exchange", "");

		if (strlen(args)){
			set_field("#sent_exchange", args);
			if (atoi(args))
				set_field("#contest_serial", args);
		}
		write_console(STYLE_LOG, "Exchange set to [");
		write_console(STYLE_LOG, get_field("#sent_exchange")->value);
		write_console(STYLE_LOG, "]\n");
	}
	else if(!strcmp(exec, "freq") || !strcmp(exec, "f") ||
		!strcmp(exec, "FREQ")){
		long freq = atol(args);
		if (freq == 0){
			write_console(STYLE_LOG, "Usage: \f xxxxx (in Hz or KHz)\n");
		}
		else if (freq < 30000)
			freq *= 1000;

		struct field *f_rit = get_field_by_label("RIT");
		if (!strcmp(f_rit->value, "ON")){
			struct field *f_freq = get_field_by_label("FREQ");
			struct field *f_rit = get_field_by_label("RIT");
			struct field *f_rit_delta = get_field_by_label("RIT_DELTA");
			unsigned int rx_freq = atoi(f_freq->value) + atoi(f_rit_delta->value);
			int rit_delta = freq - rx_freq;

			char s_rit_delta[30];
			sprintf(s_rit_delta, "%d", rit_delta);
			set_field("#rit_delta", s_rit_delta);
			update_field(get_field_by_label("FREQ"));
		}
		else if (freq > 0){
			char freq_s[20];
			sprintf(freq_s, "%ld",freq);
			set_field("r1:freq", freq_s);
		}
	}
  else if (!strcmp(exec, "exit")){
    tx_off();
    set_field("#record", "OFF");
    save_user_settings(1);
    exit(0);
  }
	else if (!strcmp(exec, "qrz")){
		if(strlen(args))
			qrz(args);
		else
			write_console(STYLE_LOG, "/qrz [callsign]\n");
	}
	else if (!strcmp(exec, "mode") || !strcmp(exec, "m") || !strcmp(exec, "MODE")){
		set_radio_mode(args);
		update_field(get_field("r1:mode"));
	}
	else if (!strcmp(exec, "t"))
		tx_on(TX_SOFT);
	else if (!strcmp(exec, "r"))
		tx_off();
// added rtx for web remote tx function coming soon
        else if (!strcmp(exec, "rtx")) {
                tx_on(TX_SOFT);
                sound_input(1);
            }
	else if (!strcmp(exec, "telnet")){
		if (strlen(args) > 5) 
			telnet_open(args);
		else
			telnet_open(get_field("#telneturl")->value);
	}
	else if (!strcmp(exec, "tclose"))
		telnet_close(args);
	else if (!strcmp(exec, "tel"))
		telnet_write(args);
	else if (!strcmp(exec, "txpitch")){
		if (strlen(args)){
			int t = atoi(args);	
			if (t > 100 && t < 4000)
				set_field("#tx_pitch", args);
			else
				write_console(STYLE_LOG, "cw pitch should be 100-4000");
		}
		char buff[100];
		sprintf(buff, "txpitch is set to %d Hz\n", get_cw_tx_pitch());
		write_console(STYLE_LOG, buff);
	}
/*	else if (!strcmp(exec, "PITCH")){
		struct field *f = get_field_by_label(exec);
		field_set("PITCH", args);
		focus_field(f);
	}
*/
	
	else if (exec[0] == 'F' && isdigit(exec[1])){
		char buff[1000];
		printf("executing macro %s\n", exec);
		do_macro(get_field_by_label(exec), FIELD_UPDATE, 0, 0, 0);
	}
	else {
		char field_name[32];
		//conver the string to upper if not already so
		for (char *p = exec; *p; p++)
			*p =  toupper(*p);
		struct field *f = get_field_by_label(exec);
		if (f) {
			//convert all the letters to uppercase
			for(char *p = args; *p; p++)
				*p = toupper(*p);
			if(set_field(f->cmd, args)) {
				//write_console(STYLE_LOG, "Invalid setting:");
			} else {
						//this is an extract from focus_field()
						//it shifts the focus to the updated field
						//without toggling/jumping the value 
						struct field *prev_hover = f_hover;
						struct field *prev_focus = f_focus;
						f_focus = NULL;
						f_focus = f_hover = f;
						focus_since = millis();
						update_field(f_hover);
			}
		}
	}
	save_user_settings(0);
}

// a global variable for our journal
sd_journal *journal;

// A signal handler for stopping
static void
stop(int sig)
{
  save_user_settings(1);
  fprintf(stderr, SD_NOTICE "zbitx service is stopping\n");
  sd_notify(0, "STOPPING=1");
  sd_journal_close(journal);
  exit(0);
}

int main( int argc, char* argv[] ) {

	setvbuf(stdout, NULL, _IONBF, 0);

	// Install our signal handlers
	if(signal(SIGTERM, stop) == SIG_ERR)
	{
		sd_notifyf(0, "STATUS=Failed to install signal handler for stopping service %s\n"
			"ERRNO=%i",
			strerror(errno),
			errno);
	}

	// open the journal
	sd_journal_open(&journal, 0);

	fprintf(stderr, SD_NOTICE "zBitx service started\n");
	sd_journal_print(LOG_NOTICE, "zBitx service started\n");

	puts(VER_STR);
	active_layout = main_controls;

	//unlink any pending ft8 transmission
	unlink("/home/pi/sbitx/ft8tx_float.raw");
	call_wipe();
	
	//we cache some fields for fast lookup
	cw_input = get_field_by_label("CW_INPUT");
	f_mode = get_field_by_label("MODE");
	f_pitch= get_field_by_label("PITCH");
	f_text_in = get_field_by_label("TEXT");
	ui_init(argc, argv);
	hw_init();
	console_init();

	q_init(&q_remote_commands, 1000); //not too many commands

// If a parameter was passed on the command line, use it as the audio output device	

	if (argc > 1)
		setup(argv[1]);
	else
		setup("plughw:0,0");	// otherwise use the default audio output device

	struct field *f;
	f = active_layout;
	field_init();		

	hd_createGridList();
	
	//initialize the modulation display

	tx_mod_max = get_field("spectrum")->width;
	tx_mod_buff = malloc(sizeof(int32_t) * tx_mod_max);
	memset(tx_mod_buff, 0, sizeof(int32_t) * tx_mod_max);
	tx_mod_index = 0;
	init_waterfall();

	strcpy(vfo_a_mode, "USB");
	strcpy(vfo_b_mode, "LSB");
	
	f = get_field("spectrum");
	update_field(f);
	set_volume(20000000);

	set_field("#mycallsign", "NOBODY");
	set_field("r1:freq", "7000000");
	set_field("r1:mode", "USB");
	set_field("tx_gain", "24");
	set_field("tx_power", "40");
	set_field("r1:gain", "41");
	set_field("r1:volume", "85");

  if (ini_parse(STATEDIR "/user_settings.ini", user_settings_handler, NULL)<0){
    printf("Unable to load user_settings.ini\n"
		"Loading default.ini instead\n");
  	ini_parse(STATEDIR "/default_settings.ini", user_settings_handler, NULL);
  }

	//the logger fields may have an unfinished qso details
	call_wipe();

	macro_load(get_field("#current_macro")->value, NULL);

	//now set the frequency of operation and more to vfo_a
  set_field("r1:freq", get_field("#vfo_a_freq")->value);

	char buf[64];
	snprintf(buf, sizeof(buf), "\n%s\n", VER_STR);
	write_console(STYLE_LOG, buf);
	write_console(STYLE_LOG, "For help: https://github.com/dg0jde/zbitxd/wiki\n\n");

	if (strcmp(get_field("#mycallsign")->value, "NOBADY")){
		char *mycall = get_field("#mycallsign")->value;
		char *mygrid = get_field("#mygrid")->value;
		snprintf(buf, sizeof(buf), "Welcome %s your grid is %s\n", mycall, mygrid);
		text_span_semantic sems[2];
		sems[0].start_column = 8;
		sems[0].length = strlen(mycall);
		sems[0].semantic = STYLE_MYCALL;
		sems[1].start_column = 8 + strlen(mycall) + 14;
		sems[1].length = strlen(mygrid);
		sems[1].semantic = STYLE_GRID;
		write_console_semantic(buf, sems, 2);
	}
	else 
		write_console(STYLE_LOG, "Set your callsign with '\\callsign [yourcallsign]'\n"
		"Set your 6 letter grid with '\\grid [yourgrid]\n");

	set_field("#text_in", "");
	field_set("REC", "OFF");
	field_set("KBD", "OFF");
	if (!strcmp(field_str("TUNE"), "ON"))
		field_set("TUNE", "OFF");

	// you don't want to save the recently loaded settings
	settings_updated = 0;
  hamlib_start();
	remote_start();

	zbitx_init();

	if (zbitx_available)
		zbitx_poll(1); // send all the field values

//	//switch to maximum priority
//	struct sched_param sch;
//	sch.sched_priority = sched_get_priority_max(SCHED_FIFO);
//	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch);

	// tell the service manager we're in the ready state
	sd_notify(0, "READY=1");

	struct timespec loopms = {0 /*secs*/, 1000000 /*nanosecs*/};
	while(1) {
		ui_tick();
		nanosleep(&loopms, &loopms);
	}

	return 0;
}
