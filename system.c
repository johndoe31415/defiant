/*
 *  WARNING: THIS FILE IS AUTO-GENERATED. CHANGES WILL BE OVERWRITTEN.
 *  Generated at 2020-09-06 14:37:09
 *  Generated by https://github.com/johndoe31415/mcuconfig
 */

/**
 *	mcuconfig - Generation of microcontroller build setups.
 *	Copyright (C) 2019-2020 Johannes Bauer
 *
 *	This file is part of mcuconfig.
 *
 *	mcuconfig is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; this program is ONLY licensed under
 *	version 3 of the License, later versions are explicitly excluded.
 *
 *	mcuconfig is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with mcuconfig; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	Johannes Bauer <JohannesBauer@gmx.de>
**/

#include <stdbool.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_flash.h>
#include <stm32f10x_gpio.h>
#include "system.h"

void default_fault_handler(void) {
	while (true);
}

static void clock_switch(void) {
	/* Enable HSE oscillator, 8.000 MHz */
	RCC->CR |= RCC_CR_HSEON;

	/* Wait for HSE to become ready */
	while (!(RCC->CR & RCC_CR_HSERDY));

	/* Source for PLL is HSE (base 8.000 MHz */
	/* Source clock for PLL 8.000 MHz * 9 = 72.000 MHz */
	/* APB1 prescaler needs to be /2, APB1 clock is 36 MHz maximum */
	RCC->CFGR = RCC_CFGR_PLLMULL9 | RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PPRE1_DIV2;

	/* Enable the PLL */
	RCC->CR |= RCC_CR_PLLON;

	/* Wait for PLL to become ready */
	while (!(RCC->CR & RCC_CR_PLLRDY));

	/* Two Flash wait state needed between 48 MHz and 72 MHz SYSCLK (SYSCLK = 72.000 MHz) */
	FLASH_SetLatency(FLASH_Latency_2);

	/* Switch clock source to PLL */
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;

	/* Wait for PLL to become active clock */
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

	/* Disable HSI to save power */
	RCC->CR &= ~RCC_CR_HSION;
}

static void gpio_init(void) {
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
			.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_9,
			.GPIO_Mode = GPIO_Mode_AF_PP,
			.GPIO_Speed = GPIO_Speed_2MHz,
	});
	GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
			.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_10,
			.GPIO_Mode = GPIO_Mode_IN_FLOATING,
			.GPIO_Speed = GPIO_Speed_2MHz,
	});
	GPIO_Init(GPIOB, &(GPIO_InitTypeDef){
			.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_13,
			.GPIO_Mode = GPIO_Mode_Out_PP,
			.GPIO_Speed = GPIO_Speed_2MHz,
	});
}

void early_system_init(void) {
	clock_switch();
	gpio_init();
}
