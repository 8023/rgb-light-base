/*
 * program.c
 *
 * Created: 2018/2/5 15:59:08
 * Author : i
 */ 

#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "./lib/light_ws2812.h"
#include "stdbool.h"

#define __PIN_SK6812 PB0 //sk6812 rgb light string
#define __PIN_INDICATOR PB1 //led
#define __PIN_TOUCH PB2 //utouch1b
#define __PIN_SELECTOR PB3 //adc 3296 50k
#define __PIN_XXX PB4

//mode const
#define __MODE_COUNT 8
//led const
#define __LEDS_CONST 6
//100fps
#define __REFRESH_RATE 10
//1step is 0.25ms, mast less than 16
#define __BREATHING_RATE_STEP 2
//0.25ms per timer interrupt, mast less than 16, interrupt use 0.2ms
#define __TIMER_STEP 3 
//per 4ms replace next color
#define __TRAVERSAL_MS 10 

#define __GRADIENT_MS 10

#define __GRADIENT_INTERWAL 100

uint8_t _MODE = 0; //mode
uint32_t _TIMER = 0; //global timer, per ms plus one, overflow at 49 days later
uint8_t _BTNFLAG = 0; //int0 interrupt flag

struct cRGBW _LEDS[__LEDS_CONST];

void initialization () {
    /* normal io */
    //output pin
    DDRB |= _BV(__PIN_INDICATOR) | _BV(__PIN_SK6812);
    //pull up
    PORTB |= _BV(__PIN_XXX) | _BV(__PIN_TOUCH);
    
    /* pwm and timer interrupt with timer0 */
    //fast pwm mode
    TCCR0A |= _BV(COM0B1) | _BV(WGM01) | _BV(WGM00);
    //8 frequency division
    TCCR0B |= _BV(CS01);
    //timer0 overflow interrupt
    TIMSK |= _BV(TOIE0);
    
    /* int0 */
    //set global interrupt enable
    sei();
    //enable external interrupt 0
    GIMSK |= _BV(INT0);
    //interrupt by rising edge
    MCUCR |= _BV(ISC01) | _BV(ISC00);
    
    /* adc3 */
    //select single adc3 in pb3
    ADMUX |= _BV(MUX1) | _BV(MUX0);
    //enable adc, enable adc interrupt, 64 frequency division
    ADCSRA |= _BV(ADEN) | _BV(ADIE) | _BV(ADPS2) | _BV(ADPS1);
    
    /* timer interrupt with timer1 */
    ////64 frequency division
    //TCCR1 |= _BV(CS12) | _BV(CS11) | _BV(CS10);
    ////timer1 overflow interrupt
    //TIMSK |= _BV(TOIE1);
}

inline void start_adc3 () {
    ADCSRA |= _BV(ADSC); //start adc convert
}

inline uint16_t get_adc3 () {
    return ADC; //return adc value
}

inline uint16_t sync_get_adc3 () {
    start_adc3(); //start adc convert
    while (ADCSRA & _BV(ADSC)); //wait for adc convert finish
    return get_adc3(); //return adc value
}

inline bool get_button () {
    return PINB & _BV(PINB2);
}

inline void set_next_mode () {
    _MODE = _MODE >= __MODE_COUNT - 1 ? 0 : _MODE + 1;
}

inline void set_btn_flag () {
    _BTNFLAG++;
}

inline void set_indicator_led (uint8_t status) {
    OCR0B = status;
}

inline uint8_t plus_avoid_overflow (uint8_t value) {
    return value == UINT8_MAX ? UINT8_MAX : value + 1;
}

inline uint8_t minus_avoid_overflow (uint8_t value) {
    return value == 0 ? 0 : value - 1;
}

inline uint16_t convert_a1024_to_c1536 (uint16_t adc) {
    return ( adc < 128 || adc >= 895 ) ? 0 : (adc - 127) * 2;
}

struct cRGB convert_c1536_to_crgb (uint16_t color) {
    struct cRGB rgb;
    uint8_t i = color % 256;
    uint8_t j = 255 - i;
    
    switch (color / 256) {
        case 0: //red to yellow
        rgb.r = 255; rgb.g = i; rgb.b = 0; break;
        case 1: //yellow to green
        rgb.r = j; rgb.g = 255; rgb.b = 0; break;
        case 2: //green to cyan
        rgb.r = 0; rgb.g = 255; rgb.b = i; break;
        case 3: //cyan to blue
        rgb.r = 0; rgb.g = j; rgb.b = 255; break;
        case 4: //blue to purple
        rgb.r = i; rgb.g = 0; rgb.b = 255; break;
        case 5: //purple to red
        rgb.r = 255; rgb.g = 0; rgb.b = j; break;
        default: break;
    }
    return rgb;
}

struct cRGB convert_crgb_to_crgba (struct cRGB color, uint8_t lighteness) {
    struct cRGB new_color;
    new_color.r = color.r / lighteness;
    new_color.g = color.g / lighteness;
    new_color.b = color.b / lighteness;
    return new_color;
}

struct cRGBW convert_crgba_to_crgbwa(struct cRGB color, uint8_t white) {
    struct cRGBW new_color;
    new_color.r = color.r;
    new_color.g = color.g;
    new_color.b = color.b;
    new_color.w = white;
    return new_color;
}

void refresh_breathing_led () {
    static uint16_t i = 0;

    if (_BTNFLAG) { //use _BTNFLAG to set i to one
        i = 1;
        _BTNFLAG --;
    }
    
    if (i) { //if i not equal zero, i = 1
        i++;

        /**
         *  running result the same as this code, 
         *  but it can save 4 bytes
         *
         *  if (i < 256) {
         *      set_indicator_led(i);
         *  } else if (i < 512) {
         *      set_indicator_led(511 - i);
         *  } else {
         *      i = 0;
         *  }
        **/
        if (i < 512) { //i mast be less than 512
            set_indicator_led(i < 256 ? i : 511 - i); //0~255, 255~0
        } else {
            i = 0;
        }
    }
}

void the_same_color (struct cRGBW color) {
    for (uint8_t x = 0; x < __LEDS_CONST; x++) {
        _LEDS[x].r = color.r;
        _LEDS[x].g = color.g;
        _LEDS[x].b = color.b;
        _LEDS[x].w = color.w;
    }
}

void mode_off () {
    struct cRGBW rgbwa;
    rgbwa.r = rgbwa.g = rgbwa.b = rgbwa.w = 0;
    the_same_color(rgbwa);
}

void mode_adc_with_lighteness (uint8_t lighteness) {
    uint16_t a1024 = get_adc3();
    uint16_t color1536 = convert_a1024_to_c1536(a1024);
    struct cRGB rgb = convert_c1536_to_crgb(color1536);
    struct cRGB rgba = convert_crgb_to_crgba(rgb, lighteness);
    struct cRGBW rgbwa =  convert_crgba_to_crgbwa(rgba, 0);
    the_same_color(rgbwa);
}

void mode_adc_with_gradient () {
    static struct cRGB step;
    static struct cRGBW rgbwa;
    static uint16_t timer = 0;

    if (timer == 0) {
        uint16_t a1024 = get_adc3();
        uint16_t color1536 = convert_a1024_to_c1536(a1024);
        struct cRGB rgb = convert_c1536_to_crgb(color1536);
        rgbwa.r = rgbwa.g = rgbwa.b = rgbwa.w = 0;
        step.r = UINT8_MAX / plus_avoid_overflow(rgb.r);
        step.g = UINT8_MAX / plus_avoid_overflow(rgb.g);
        step.b = UINT8_MAX / plus_avoid_overflow(rgb.b);
    }
    if (!(_TIMER % __GRADIENT_MS)) {
        timer++;
    }
    if (timer >= 512 + __GRADIENT_INTERWAL) {
        timer = 0;
    }

    uint8_t decrease = 511 - timer;
    if (timer < 256) {
        if (!(timer % step.r)) {
            rgbwa.r = plus_avoid_overflow(rgbwa.r);
        }
        if (!(timer % step.g)) {
            rgbwa.g = plus_avoid_overflow(rgbwa.g);
        }
        if (!(timer % step.b)) {
            rgbwa.b = plus_avoid_overflow(rgbwa.b);
        }
    } else if (timer < 512) {
        if (!(decrease % step.r)) {
            rgbwa.r = minus_avoid_overflow(rgbwa.r);
        }
        if (!(decrease % step.g)) {
            rgbwa.g = minus_avoid_overflow(rgbwa.g);
        }
        if (!(decrease % step.b)) {
            rgbwa.b = minus_avoid_overflow(rgbwa.b);
        }
    } else {
        rgbwa.r = rgbwa.g = rgbwa.b = 0;
    }
    
    the_same_color(rgbwa);
}

void mode_white_with_lighteness (uint8_t lighteness) {
    struct cRGBW rgbwa;
    rgbwa.r = rgbwa.g = rgbwa.b = 0;
    rgbwa.w = UINT8_MAX / lighteness;
    the_same_color(rgbwa);
}

void mode_white_with_gradient () {
    static uint16_t timer = 0;
    if (!(_TIMER % __GRADIENT_MS)) {
        timer++;
    }
    if (timer >= 512 + __GRADIENT_INTERWAL) {
        timer = 0;
    }
    
    struct cRGBW rgbwa;
    if (timer < 256) {
        rgbwa.w = timer;
    } else if (timer < 512) {
        rgbwa.w = 511 - timer;
    } else {
        rgbwa.w = 0;
    }
    the_same_color(rgbwa);
}

void mode_traversal () {
    static uint16_t timer = 0;
    if (!(_TIMER % __TRAVERSAL_MS)) {
        timer++;
    }
    if (timer >= 1536) {
        timer = 0;
    }
    struct cRGB rgb = convert_c1536_to_crgb(timer);
    struct cRGB rgba = convert_crgb_to_crgba(rgb, 1);
    struct cRGBW rgbwa =  convert_crgba_to_crgbwa(rgba, 0);
    the_same_color(rgbwa);
}

void select_mode () {
    switch (_MODE) {
        case 0: {
            mode_off();
            break;
        }
        case 1: {
            mode_adc_with_lighteness(1);
            break;
        }
        case 2: {
            mode_adc_with_lighteness(4);
            break;
        }
        case 3: {
            mode_adc_with_gradient();
            break;
        }
        case 4: {
            mode_white_with_lighteness(1);
            break;
        }
        case 5: {
            mode_white_with_lighteness(4);
            break;
        }
        case 6: {
            mode_white_with_gradient();
            break;
        }
        break;
        case 7: {
            mode_traversal();
            break;
        }
        break;
        default:
        break;
    }
}

void refresh_leds_color () {
    ws2812_setleds_rgbw(_LEDS, __LEDS_CONST);
}

ISR (INT0_vect) {
    uint8_t i = 20;
    for (uint8_t j = 0; j < 20; j++) {
        _delay_ms(1);
        if (get_button()) {
            i--;
        }
    }
    if (!i) {
        set_next_mode();
        set_btn_flag();
    }
}

ISR (TIMER0_OVF_vect) {
    static uint8_t timer = 0;
    if (timer >= __TIMER_STEP * 8) {
        timer = 0;
    } else {
        timer++;
    }
    
    //per step
    if (!(timer % __BREATHING_RATE_STEP)) {
        refresh_breathing_led();
    }

    if (!(timer % __TIMER_STEP)) {
        if (_TIMER >= UINT32_MAX) {
            _TIMER = 0;
        } else {
            _TIMER++;
        }

        //per ms
        if (!(_TIMER % __REFRESH_RATE)) {
            start_adc3();
        }
    }
}

ISR (ADC_vect) {
    select_mode();
    refresh_leds_color();
}

int main (void)
{
    initialization();

    while (true) {
        ;
    }
}

