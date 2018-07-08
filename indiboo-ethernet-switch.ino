#include <util/atomic.h>
#include <Adafruit_NeoPixel.h>
#include <TaskAction.h>

#include "PinChangeInterrupt.h"

#include "fixed-length-accumulator.h"
#include "very-tiny-http.h"

#include "game-ethernet.h"

static int8_t s_debouncer = 0;
static const int8_t MAX_DEBOUNCE = 5;

static const uint8_t NEOPIXEL_PIN = 2;
static const uint8_t BUTTON_PIN = 3;

static Adafruit_NeoPixel s_pixels = Adafruit_NeoPixel(2, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static bool s_pressed_flag = false;

static void send_standard_erm_response()
{
    http_server_set_response_code("200 OK");
    http_server_set_header("Access-Control-Allow-Origin", "*");
    http_server_finish_headers();
}

static void debug_task_fn(TaskAction*task)
{
    Serial.print("Button ");
    Serial.println(s_pressed_flag ? "press pending" : "not pressed");
}
static TaskAction s_debug_task(debug_task_fn, 500, INFINITE_TICKS);

static void debounce_task_fn(TaskAction*task)
{
    bool pressed_now = (digitalRead(BUTTON_PIN) == LOW);

    if (pressed_now && (s_debouncer < MAX_DEBOUNCE))
    {
        s_debouncer++;
        if (s_debouncer == MAX_DEBOUNCE) { s_pressed_flag = true; }
    }
    else if (s_debouncer > 0)
    {
        s_debouncer--;
        if (s_debouncer == 0) { s_pressed_flag = false; }
    }
}

static TaskAction s_debounce_task(debounce_task_fn, 25, INFINITE_TICKS);

static void switch_state_req_handler(char const * const url)
{
    (void)url;
    Serial.println(F("Handling /button/status"));
    send_standard_erm_response();
    http_server_add_body(s_pressed_flag ? "PRESSED" : "NOT PRESSED");
    s_pressed_flag = false;
}

static http_get_handler s_handlers[] = 
{
    {"/button/status", switch_state_req_handler},
    {"", NULL}
};

void setup()
{
    ethernet_setup(s_handlers);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Serial.begin(115200);

    s_pixels.begin();

    Serial.println("Cave Escape Ethernet Button");
    delay(200);
}

void loop()
{
    s_debug_task.tick();
    s_debounce_task.tick();
    ethernet_tick();    
}
