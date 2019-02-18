/*
 * Copyright (c) 2016-2017, Alex Taradov <alex@taradov.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*- Includes ----------------------------------------------------------------*/
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <string.h>
#include "samd11.h"
#include "hal_gpio.h"
#include "nvm_data.h"
#include "debug.h"
#include "usb.h"

/*- Definitions -------------------------------------------------------------*/
HAL_GPIO_PIN(LED,      A, 16)

#define APP_EP_SEND    1
#define APP_EP_RECV    2

#define APP_MAGIC      0x78656c41
#define APP_VERSION    1

enum
{
  CMD_GET_VERSION  = 0xf0,
};

/*- Variables ---------------------------------------------------------------*/
static alignas(4) uint8_t app_usb_recv_buffer[64];
static alignas(4) uint8_t app_response_buffer[64];

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void irq_handler_tc1(void)
{
  if (TC1->COUNT16.INTFLAG.reg & TC_INTFLAG_MC(1))
  {
    HAL_GPIO_LED_toggle();
    TC1->COUNT16.INTFLAG.reg = TC_INTFLAG_MC(1);
  }
}

//-----------------------------------------------------------------------------
static void timer_init(void)
{
  PM->APBCMASK.reg |= PM_APBCMASK_TC1;

  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(TC1_GCLK_ID) |
      GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN(0);

  TC1->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ |
      TC_CTRLA_PRESCALER_DIV1024 | TC_CTRLA_PRESCSYNC_RESYNC;

  TC1->COUNT16.COUNT.reg = 0;

  TC1->COUNT16.CC[0].reg = (F_CPU / 1000ul / 1024) * 500;
  TC1->COUNT16.COUNT.reg = 0;

  TC1->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;

  TC1->COUNT16.INTENSET.reg = TC_INTENSET_MC(1);
  NVIC_EnableIRQ(TC1_IRQn);
}

//-----------------------------------------------------------------------------
static void sys_init(void)
{
  uint32_t coarse, fine;

  SYSCTRL->OSC8M.bit.PRESC = 0;

  SYSCTRL->INTFLAG.reg = SYSCTRL_INTFLAG_BOD33RDY | SYSCTRL_INTFLAG_BOD33DET |
      SYSCTRL_INTFLAG_DFLLRDY;

  NVMCTRL->CTRLB.bit.RWS = 2;

  coarse = NVM_READ_CAL(DFLL48M_COARSE_CAL);
  fine = NVM_READ_CAL(DFLL48M_FINE_CAL);

  SYSCTRL->DFLLCTRL.reg = 0; // See Errata 9905
  while (0 == (SYSCTRL->PCLKSR.reg & SYSCTRL_PCLKSR_DFLLRDY));

  SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_MUL(48000);
  SYSCTRL->DFLLVAL.reg = SYSCTRL_DFLLVAL_COARSE(coarse) | SYSCTRL_DFLLVAL_FINE(fine);

  SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_ENABLE | SYSCTRL_DFLLCTRL_USBCRM |
      SYSCTRL_DFLLCTRL_BPLCKC | SYSCTRL_DFLLCTRL_CCDIS | SYSCTRL_DFLLCTRL_MODE;

  while (0 == (SYSCTRL->PCLKSR.reg & SYSCTRL_PCLKSR_DFLLRDY));

  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(0) | GCLK_GENCTRL_SRC(GCLK_SOURCE_DFLL48M) |
      GCLK_GENCTRL_RUNSTDBY | GCLK_GENCTRL_GENEN;
  while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY);
}
#if 0
//-----------------------------------------------------------------------------
static uint32_t get_uint32(uint8_t *data)
{
  return ((uint32_t)data[0] << 0) | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

//-----------------------------------------------------------------------------
static uint32_t get_uint16(uint8_t *data)
{
  return ((uint16_t)data[0] << 0) | ((uint16_t)data[1] << 8);
}
#endif
//-----------------------------------------------------------------------------
static void set_uint32(uint8_t *data, uint32_t value)
{
  data[0] = (value >> 0) & 0xff;
  data[1] = (value >> 8) & 0xff;
  data[2] = (value >> 16) & 0xff;
  data[3] = (value >> 24) & 0xff;
}
#if 0
//-----------------------------------------------------------------------------
static void set_uint16(uint8_t *data, uint16_t value)
{
  data[0] = (value >> 0) & 0xff;
  data[1] = (value >> 8) & 0xff;
}
#endif
//-----------------------------------------------------------------------------
void usb_send_callback(void)
{
}

//-----------------------------------------------------------------------------
void usb_recv_callback(void)
{
  int cmd = app_usb_recv_buffer[0];

  app_response_buffer[0] = cmd;
  app_response_buffer[1] = true;

  if (CMD_GET_VERSION == cmd)
  {
    set_uint32(&app_response_buffer[2], APP_MAGIC);
    app_response_buffer[6] = APP_VERSION;
  }

  else
  {
    app_response_buffer[1] = false;
  }

  usb_send(APP_EP_SEND, app_response_buffer, sizeof(app_response_buffer), usb_send_callback);

  usb_recv(APP_EP_RECV, app_usb_recv_buffer, sizeof(app_usb_recv_buffer), usb_recv_callback);
}

//-----------------------------------------------------------------------------
void usb_configuration_callback(int config)
{
  usb_recv(APP_EP_RECV, app_usb_recv_buffer, sizeof(app_usb_recv_buffer), usb_recv_callback);

  (void)config;
}

//-----------------------------------------------------------------------------
int main(void)
{
  sys_init();
  timer_init();
  debug_init();

  debug_puts("osmo-clkgen\r\n");

  usb_init();

  HAL_GPIO_LED_out();
  HAL_GPIO_LED_clr();

  while (1)
  {
  }

  return 0;
}

