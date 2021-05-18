/*
 * interface.c
 *
 *  Created on: Mar 4, 2021
 *      Author: douwe
 */


#include <stdbool.h>
#include <math.h>
#include "cmsis_os.h"
#include "LCD1602.h"

#include "interface.h"
#include "interface_screens.h"

/***************************
 *	public variables
 **************************/

uint8_t volume_level = 0; // between 0 - 20
bool LED_enabled = false;

/***************************
 *	private variables
 **************************/

//	volume control
int volume_delta = 0; // volume wheel went up/down
const uint8_t LPOT_ADDR = 0b01010100, RPOT_ADDR = 0b01011110; // I2C adresses
I2C_HandleTypeDef * i2c_handle;
TIM_HandleTypeDef * tim_handle;

//	screen control
bool screen_updated = true; // indicator of when to rewrite screen output
enum screentype previous_screen = StartScreen, current_screen = StartScreen; // keep track of the current and previous screen
uint16_t popup_timer = 0;
// button control
uint32_t value_adc;
enum direction last_registered_button = NoSelection;
enum direction last_button_state = NoSelection;
struct buttonaction current_action = {NoBehavior, 0};

/********************************
 *	private function declaration
 ********************************/

//	separate driver methods
void interface_update(uint32_t frame); // regulates the interface configurations
void screen_update(uint32_t frame); // processes the LCD output
void volume_update(uint32_t frame); // processes the volume input
//	interface control
void set_screen(enum screentype screen); // toggle between menu screens
//	button control
void buttons_controller(uint32_t value);
//  volume control
void configure_potentiometers();

/***************************
 *	public variables
 **************************/
void init_interface(I2C_HandleTypeDef * hi2c, TIM_HandleTypeDef *htim)
{
	i2c_handle = hi2c;
	tim_handle = htim;
	lcd_init(htim);
}

void tick_interface(uint32_t frame)
{
	interface_update(frame);
	volume_update(frame);
	screen_update(frame);
}


void interrupt_interface(uint16_t GPIO_Pin , ADC_HandleTypeDef *hadc1)
{
	if(GPIO_Pin == GPIO_PIN_9)
	{
		if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == 1) volume_delta++;
		else volume_delta--;
	}

	if(GPIO_Pin == GPIO_PIN_8){
		HAL_ADC_Start_IT(hadc1);
	}
}

void interrupt_adc_interface(ADC_HandleTypeDef *hadc){
	    value_adc = HAL_ADC_GetValue(hadc);
	    buttons_controller ( value_adc);
	    HAL_ADC_Stop_IT(hadc);
}

void interrupt_timer_interface(TIM_HandleTypeDef *htim){
	if(htim == tim_handle){
		HAL_TIM_Base_Start_IT(tim_handle);
	}
}


/***************************
 *	private functions
 **************************/

void interface_update(uint32_t frame)
{
	// check if popup has been shown for "popup_timer" seconds
	if(screens[current_screen].popup_time != 0 && popup_timer == 0)
	{
		current_screen = previous_screen;
		screen_updated = true;
	} else popup_timer--;
	if(current_action.action != NoBehavior)
	{
		switch(current_action.action)
		{
			case GoToPreviousScreen:
				current_screen = previous_screen;
				screen_updated = true;
				break;
			case GoToScreen:
				current_screen = current_action.screen;
				screen_updated = true;
				break;
			default:
				screen_updated = true;
				break;
		}
	}
}

void screen_update(uint32_t frame)
{
	if(screen_updated || frame % 15 == 0) // update when new screen
	{
		lcd_clear();
		char first[16], second[16];
		screens[current_screen].GetScreenOutput(frame, first, second);
		lcd_send_string(first);
		lcd_put_cur(1,0);
		lcd_send_string(second);
		screen_updated = false;
	}
}

// Volume wheel driver. Handles input of volume wheel.
void volume_update(uint32_t frame)
{
	if(volume_delta > 0 && volume_level < 20) { volume_level++; }
	else if(volume_delta < 0 && volume_level > 0) { volume_level--; }
	if(volume_delta != 0) {
		volume_delta = 0;
		set_screen(VolumeScreen);
	}
	configure_potentiometers();
}

void configure_potentiometers()
{
	// write to RDAC1 meaning positive side potentiometers
	uint8_t com_and_adr = 0b00010000, data = floor(0xff / 20) * volume_level; // 0 = command and address of RDAC register, 1 = to write value of RDAC register
	uint16_t buf = data;
	buf = buf << 8;
	buf += com_and_adr;
	uint8_t sound_info[2] = {0b00010000, floor(0xff / 20) * volume_level};
	// send volume as long as the potentiometers accept
	while(HAL_I2C_Master_Transmit(i2c_handle, LPOT_ADDR, &buf, 2, 5) != HAL_OK);
	while(HAL_I2C_Master_Transmit(i2c_handle, RPOT_ADDR, sound_info, 2, 5) != HAL_OK);
	// write to RDAC2 meaning negative side potentiometers
	com_and_adr = 0b00010001; // 0 = command and address of RDAC register, 1 = to write value of RDAC register
	buf = data;
	buf = buf << 8;
	buf += com_and_adr;
	// configure the positive side potentiometers
	while(HAL_I2C_Master_Transmit(i2c_handle, LPOT_ADDR, &buf, 2, 5) != HAL_OK);
	while(HAL_I2C_Master_Transmit(i2c_handle, RPOT_ADDR, &buf, 2, 5) != HAL_OK);
}

// Button driver
void buttons_controller( uint32_t value ){
	enum direction button = NoSelection;
	// I use a marge of +- 100
	if ( value <= 2000) {
	} else if ( value <= 2730){ // avg 2610
		button = Right;
	} else if ( value <= 2900){ // avg 2770
		button = Down;
	} else if ( value <= 3300){ // avg 3100
		button = Up;
	} else if ( value <= 3800){ // avg 3590
		button =  Enter;
	} else {
		button = Left;
	}
	last_registered_button = button;
	current_action = screens[current_screen].OnButtonPressed(button);
}

// Interface functions
void set_screen(enum screentype screen)
{
	if(screens[current_screen].popup_time == 0) previous_screen = current_screen;
	popup_timer = screens[screen].popup_time;
	current_screen = screen;
	screen_updated = true;
}
