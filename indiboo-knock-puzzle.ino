#include <util/atomic.h>
#include <Adafruit_NeoPixel.h>
#include <TaskAction.h>

#include "PinChangeInterrupt.h"

#include "fixed-length-accumulator.h"
#include "very-tiny-http.h"

#include "game-ethernet.h"

enum knock
{
	SOFT_KNOCK,
	HARD_KNOCK,
};
typedef enum knock KNOCK;

enum knock_event
{
	KNOCK_EVENT_SOFT_KNOCK,
	KNOCK_EVENT_HARD_KNOCK,
	KNOCK_EVENT_TIMEOUT
};
typedef enum knock_event KNOCK_EVENT;

enum knock_state
{
	KNOCK_STATE_IDLE,
	KNOCK_STATE_SOFT_SHORT_TIMER,
	KNOCK_STATE_SOFT,
	KNOCK_STATE_HARD,
};
typedef enum knock_state KNOCK_STATE;

enum game_state
{
	GAME_STATE_PLAYING,
	GAME_STATE_WON
};
typedef enum game_state GAME_STATE;

static const uint8_t NUMBER_OF_KNOCKS = 6;

static const KNOCK VALID_COMBINATION[NUMBER_OF_KNOCKS] = {SOFT_KNOCK, SOFT_KNOCK, SOFT_KNOCK, HARD_KNOCK, HARD_KNOCK, HARD_KNOCK};
static const KNOCK RESET_COMBINATION[NUMBER_OF_KNOCKS] = {SOFT_KNOCK, SOFT_KNOCK, SOFT_KNOCK, HARD_KNOCK, HARD_KNOCK, HARD_KNOCK};

static const uint8_t SOFT_KNOCK_PIN = 4;
static const uint8_t HARD_KNOCK_PIN = 3;

static const uint8_t RELAY_PIN = 2;
static const uint8_t NEOPIXEL_PIN = 8;

static const uint16_t TIMER_RELOAD = 500;

static const char * STATE_STRINGS[] = {
	"Idle",
	"ShortTimer",
	"Soft",
	"Hard"
};

static KNOCK_STATE s_knock_state = KNOCK_STATE_IDLE;
static KNOCK s_last_knock;
static KNOCK s_knock_history[NUMBER_OF_KNOCKS] = {-1,-1,-1,-1,-1,-1};

static bool s_valid_knock_flag = false;

static uint16_t s_timer = 0;

static Adafruit_NeoPixel s_pixels = Adafruit_NeoPixel(2, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

static GAME_STATE s_game_state = GAME_STATE_PLAYING;

static void print_history( KNOCK * history)
{
	for(uint8_t i = 0; i < NUMBER_OF_KNOCKS; i++)
	{
		Serial.print(history[i]);
		Serial.print(",");
	}
	Serial.println("");
}

static bool match_history(KNOCK * knocks1, KNOCK * knocks2)
{
	bool match = true;
	for(uint8_t i = 0; i < NUMBER_OF_KNOCKS-1; i++)
	{
		match &= knocks1[i] == knocks2[i];
	}
	return match;
}

static void record_knock(KNOCK knock, KNOCK * history)
{
	for(uint8_t i = 0; i < NUMBER_OF_KNOCKS-1; i++)
	{
		history[i] = history[i+1];
	}
	history[NUMBER_OF_KNOCKS-1] = knock;
}
static void start_knock_timer(uint16_t timeout)
{
	s_timer = timeout;
}

static void handle_valid_knock(KNOCK knock)
{
	s_last_knock = knock;
	s_valid_knock_flag = true;
	record_knock(knock, s_knock_history);
}

static KNOCK_STATE handle_knock_event_in_idle(KNOCK_EVENT knock_event)
{
	KNOCK_STATE new_state = KNOCK_STATE_IDLE;
	switch(knock_event)
	{
	case KNOCK_EVENT_SOFT_KNOCK:
		start_knock_timer(5);
		new_state = KNOCK_STATE_SOFT_SHORT_TIMER;
		break;

	case KNOCK_EVENT_HARD_KNOCK:
		start_knock_timer(TIMER_RELOAD);
		handle_valid_knock(HARD_KNOCK);
		new_state = KNOCK_STATE_HARD;
		break;
		
	default:
		break;
	}
	return new_state;
}

static KNOCK_STATE handle_knock_event_in_short_timer(KNOCK_EVENT knock_event)
{
	KNOCK_STATE new_state = KNOCK_STATE_SOFT_SHORT_TIMER;
	switch(knock_event)
	{
	case KNOCK_EVENT_HARD_KNOCK:
		start_knock_timer(TIMER_RELOAD);
		handle_valid_knock(HARD_KNOCK);
		new_state = KNOCK_STATE_HARD;
		break;
		
	case KNOCK_EVENT_TIMEOUT:
		start_knock_timer(TIMER_RELOAD);
		handle_valid_knock(SOFT_KNOCK);
		new_state = KNOCK_STATE_SOFT;
		break;
			
	default:
		break;
	}
	return new_state;
}

static KNOCK_STATE handle_knock_event_in_soft(KNOCK_EVENT knock_event)
{
	return (knock_event == KNOCK_EVENT_TIMEOUT) ? KNOCK_STATE_IDLE : KNOCK_STATE_SOFT;
}
static KNOCK_STATE handle_knock_event_in_soft_hard(KNOCK_EVENT knock_event)
{
	return (knock_event == KNOCK_EVENT_TIMEOUT) ? KNOCK_STATE_IDLE : KNOCK_STATE_HARD;
}

static void knock_state_handler(KNOCK_EVENT knock_event)
{
	KNOCK_STATE new_state = s_knock_state;
	switch(s_knock_state)
	{
	case KNOCK_STATE_IDLE:
		new_state = handle_knock_event_in_idle(knock_event);
		break;
	case KNOCK_STATE_SOFT_SHORT_TIMER:
		new_state = handle_knock_event_in_short_timer(knock_event);
		break;
	case KNOCK_STATE_SOFT:
		new_state = handle_knock_event_in_soft(knock_event);
		break;
	case KNOCK_STATE_HARD:
		new_state = handle_knock_event_in_soft_hard(knock_event);
		break;
	}

	if (s_knock_state != new_state)
	{
		s_knock_state = new_state;
		Serial.println(STATE_STRINGS[s_knock_state]);
	}
}

static void soft_knock_isr()
{
	knock_state_handler(KNOCK_EVENT_SOFT_KNOCK);
}

static void hard_knock_isr()
{
	knock_state_handler(KNOCK_EVENT_HARD_KNOCK);
}

static void set_pixels_from_knock_states(Adafruit_NeoPixel& pixels, KNOCK knock)
{
	pixels.setPixelColor(0, knock==SOFT_KNOCK ? pixels.Color(64,0,0) : pixels.Color(0,0,0));
	pixels.setPixelColor(1, knock==HARD_KNOCK ? pixels.Color(64,0,0) : pixels.Color(0,0,0));
	pixels.show();
}

static void clear_pixels(Adafruit_NeoPixel& pixels)
{
	pixels.clear();
	pixels.show();
}

static void flash_pixels(Adafruit_NeoPixel& pixels, uint8_t r, uint8_t g, uint8_t b, uint16_t duration)
{
	pixels.setPixelColor(0, r, g, b);
	pixels.setPixelColor(1, r, g, b);
	pixels.show();
	delay(duration);
	pixels.setPixelColor(0, 0, 0, 0);
	pixels.setPixelColor(1, 0, 0, 0);
	pixels.show();
}

static bool check_and_clear(bool &flag)
{
	bool value;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		value = flag;
		flag = false;
	}
	return value;
}

static void end_game()
{
	Serial.println("GAME END");
	flash_pixels(s_pixels, 0, 64, 0, 500);
	digitalWrite(RELAY_PIN, HIGH);
	s_game_state = GAME_STATE_WON;
}

static void reset_game()
{
	Serial.println("GAME RESET");
	flash_pixels(s_pixels, 0, 0, 64, 500);
	digitalWrite(RELAY_PIN, LOW);
	s_game_state = GAME_STATE_PLAYING;
}

static void debug_task_fn(TaskAction*task)
{
	(void)task;
}
static TaskAction s_debug_task(debug_task_fn, 500, INFINITE_TICKS);

static void send_standard_erm_response()
{
    http_server_set_response_code("200 OK");
    http_server_set_header("Access-Control-Allow-Origin", "*");
    http_server_finish_headers();
}

static void win_handler(char const * const url)
{
    (void)url;
    Serial.println(F("Handling /win"));
    send_standard_erm_response();
    end_game();
}

static http_get_handler s_handlers[] = 
{
    {"/win", win_handler},
    {"", NULL}
};

void setup()
{
	attachPCINT(digitalPinToPCINT(SOFT_KNOCK_PIN), soft_knock_isr, RISING);
	attachPCINT(digitalPinToPCINT(HARD_KNOCK_PIN), hard_knock_isr, RISING);

	ethernet_setup(s_handlers);

	pinMode(RELAY_PIN, OUTPUT);

	Serial.begin(115200);
    s_pixels.begin();
	delay(200);
}

static unsigned long last_millis = 0;
void loop()
{
	unsigned long now = millis();

	s_debug_task.tick();
	ethernet_tick();
	
	if (last_millis != now)
	{
		last_millis = now;
		if (s_timer)
		{
			if (--s_timer == 0)
			{
				knock_state_handler(KNOCK_EVENT_TIMEOUT);
			}
		}
	}

	if (s_timer)
	{
		set_pixels_from_knock_states(s_pixels, s_last_knock);
	}
	else
	{
		clear_pixels(s_pixels);
	}

	if (check_and_clear(s_valid_knock_flag))
	{
		print_history(s_knock_history);
		switch(s_game_state)
		{
		case GAME_STATE_PLAYING:
			if (match_history(s_knock_history, VALID_COMBINATION))
			{
				end_game();
			}
			break;
		case GAME_STATE_WON:
			if (match_history(s_knock_history, RESET_COMBINATION))
			{
				reset_game();
			}
			break;
		}
	}
}
