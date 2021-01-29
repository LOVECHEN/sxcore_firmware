/*!
    \file  main.c 
    \brief this file realizes the MSC host

    \version 2017-06-06, V1.0.0, firmware for GD32F3x0
    \version 2019-06-01, V2.0.0, firmware for GD32F3x0
*/

/*
    Copyright (c) 2019, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "usb_delay.h"
#include "usbh_core.h"
#include "usbh_usr.h"
#include "usbh_msc_core.h"
#include "usbh_std.h"
#include "usbh_ctrl.h"
#include "systick.h"

#define HOST_POWERSW_PORT_RCC     RCU_GPIOD
#define HOST_POWERSW_PORT         GPIOD
#define HOST_POWERSW_VBUS         GPIO_PIN_2

void usb_rcu_init                 (void);
void usb_gpio_init                (void);
void usb_hwp_interrupt_enable     (usb_core_handle_struct *pudev);
void usb_hwp_vbus_config          (usb_core_handle_struct *pudev);
void usb_hwp_vbus_drive           (void *pudev, uint8_t state);
void system_clock_config          (void);

uint8_t timer_prescaler = 0;
uint32_t usbfs_prescaler = 0;

usb_core_handle_struct usb_core_dev =
{
    .host = 
    {
        .vbus_drive = usb_hwp_vbus_drive,
    },
    .udelay = delay_us,
    .mdelay = delay_ms,
};

usbh_host_struct usb_host = 
{
    .usbh_backup_state.host_backup_state      = HOST_IDLE,
    .usbh_backup_state.enum_backup_state      = ENUM_IDLE,
    .usbh_backup_state.ctrl_backup_state      = CTRL_IDLE,
    .usbh_backup_state.class_req_backup_state = 0,
    .usbh_backup_state.class_backup_state     = 0,

    .usr_cb         = &usr_cb,
    .class_init     = usbh_msc_interface_init,
    .class_deinit   = usbh_msc_interface_deinit,

};

usbh_state_handle_struct usbh_state_core;

/*!
    \brief      main routine
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    /* usb rcu init */
    usb_rcu_init();

    /* timer nvic initialization */
    timer_nvic_init();

    systick_config();

    /* configure GPIO pin used for switching VBUS power */
    usb_hwp_vbus_config(&usb_core_dev);

    /* host de-initializations */
    usbh_deinit(&usb_core_dev, &usb_host, &usbh_state_core);

    /* start the USB core */
    hcd_init(&usb_core_dev, USB_FS_CORE_ID);

    /* init usr call back */
    usb_host.usr_cb->init();

    /* enable interrupts */
    usb_hwp_interrupt_enable(&usb_core_dev);
  
    while (1) {
        host_state_polling_fun(&usb_core_dev, &usb_host, &usbh_state_core);
    }
}

/*!
    \brief      initialize usb rcu
    \param[in]  none
    \param[out] none
    \retval     none
*/
void usb_rcu_init (void)
{
    uint32_t system_clock = rcu_clock_freq_get(CK_SYS);
  
    if (system_clock == 48000000) {
        usbfs_prescaler = RCU_USBFS_CKPLL_DIV1;
        timer_prescaler = 3;
    } else if (system_clock == 72000000) {
        usbfs_prescaler = RCU_USBFS_CKPLL_DIV1_5;
        timer_prescaler = 5;
    } else if (system_clock == 96000000) {
        usbfs_prescaler = RCU_USBFS_CKPLL_DIV2;
        timer_prescaler = 7;
    } else {
        /*  reserved  */
    }

    rcu_usbfs_clock_config(usbfs_prescaler);
    rcu_periph_clock_enable(RCU_USBFS);
}

/*!
    \brief      configure usb global interrupt
    \param[in]  pudev: pointer to usb device
    \param[out] none
    \retval     none
*/
void usb_hwp_interrupt_enable(usb_core_handle_struct *pudev)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);
    nvic_irq_enable((uint8_t)USBFS_IRQn, 2U, 0U);
}

/*!
    \brief      drives the Vbus signal through GPIO
    \param[in]  pudev: pointer to usb device
    \param[out] none
    \retval     none
*/
void usb_hwp_vbus_drive(void *pudev, uint8_t state)
{
    if (0U == state) {
        /* disable is needed on output of the power switch */
        gpio_bit_set(HOST_POWERSW_PORT, HOST_POWERSW_VBUS);
    } else {
        /* enable the power switch by driving the enable low */
        gpio_bit_reset(HOST_POWERSW_PORT, HOST_POWERSW_VBUS);
    }
}

/*!
    \brief      configures the gpio for the vbus and overcurrent
    \param[in]  pudev: pointer to usb device
    \param[out] none
    \retval     none
*/
void  usb_hwp_vbus_config(usb_core_handle_struct *pudev)
{
    rcu_periph_clock_enable(HOST_POWERSW_PORT_RCC);

    gpio_mode_set(HOST_POWERSW_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, HOST_POWERSW_VBUS);
    gpio_output_options_set(HOST_POWERSW_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, HOST_POWERSW_VBUS);
  
    /* by default, disable is needed on output of the power switch */
    gpio_bit_set(HOST_POWERSW_PORT, HOST_POWERSW_VBUS);

    /* delay is need for stabilising the vbus low in reset condition, 
     * when vbus = 1 and reset-button is pressed by user 
     */
    if (NULL != pudev->mdelay) {
        pudev->mdelay(200U);
    }
}
