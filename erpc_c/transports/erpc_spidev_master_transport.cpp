/*
 * Copyright 2020-2021 NXP
 * Copyright 2021 ACRIOS Systems s.r.o.
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "erpc_spidev_master_transport.hpp"

#include "erpc_message_buffer.hpp"
#include "erpc_spidev.h"
#include "erpc_sysgpio.h"

extern "C" {
#include <unistd.h>
#include <linux/gpio.h> // everything about GPIOs
}

using namespace erpc;

#define __USE_SYSFS__

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#ifndef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
#define ERPC_BOARD_SPI_SLAVE_READY_MARKER1 0xABU
#define ERPC_BOARD_SPI_SLAVE_READY_MARKER2 0xCDU
#else
#ifndef ERPC_BOARD_SPI_INT_PIN
#error "Please define the BOARD_SPI_INT_PIN used to notify when the SPI Slave is ready to transmit"
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
static volatile int s_gpioHandle = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
void erpc::spiWaitForSlaveReadyGpio(int gpioHandle)
{
#ifdef __USE_SYSFS__
    for (;;)
    {
         /*
         * The GPIO pin has been configured to generate interrupts on edge event
         * The poll() will return whenever the interrupt was triggered
         */
         if (gpio_poll(gpioHandle, -1) == 0)
         {
             break;
         }
    }
#else
    for (;;)
    {
         if (gpio_poll_line(gpioHandle, -1) == 0)
         {
             break;
         }
    }
#endif
}

static inline void SpidevMasterTransport_WaitForSlaveReadyGpio()
{
    spiWaitForSlaveReadyGpio(s_gpioHandle);
}
#else
static inline void SpidevMasterTransport_WaitForSlaveReadyMarker(int spi_fd)
{
    uint8_t detected = 0;
    uint8_t data;
    for (;;)
    {
        spidev_transfer(spi_fd, NULL, &data, 1);
        if (ERPC_BOARD_SPI_SLAVE_READY_MARKER1 == data)
        {
            detected = 1;
        }
        else if (detected && (ERPC_BOARD_SPI_SLAVE_READY_MARKER2 == data))
        {
            break;
        }
        else
        {
            detected = 0;
            usleep(100);
        }
    }
}
#endif

SpidevMasterTransport::SpidevMasterTransport(const char *spidev, uint32_t speed_Hz) :
m_spidevHandle(0), m_spidev(spidev), m_speed_Hz(speed_Hz)
{
}

SpidevMasterTransport::~SpidevMasterTransport(void)
{
    if (m_spidevHandle > 0)
    {
        spidev_close(m_spidevHandle);
    }

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
    if (s_gpioHandle > 0)
    {
        gpio_close(s_gpioHandle);
    }
#endif
}

erpc_status_t SpidevMasterTransport::init(void)
{
    erpc_status_t status = kErpcStatus_Success;

    /* Initialize the SPI device */
    /* Open SPI device file descriptor */
    m_spidevHandle = spidev_open(m_spidev);
    if (m_spidevHandle < ERPC_SPIDEV_STATUS_SUCCESS)
    {
        status = kErpcStatus_InitFailed;
    }

    /* Set SPI mode to SPI_MODE_0 (CPOL = 0, CPHA = 0) */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SPIDEV_STATUS_SUCCESS != spidev_set_mode(m_spidevHandle, 0))
        {
            status = kErpcStatus_InitFailed;
        }
    }

    /* Set SPI default max speed */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SPIDEV_STATUS_SUCCESS != spidev_set_speed(m_spidevHandle, m_speed_Hz))
        {
            status = kErpcStatus_InitFailed;
        }
    }
    if (status == kErpcStatus_Success)
    {
        m_speed_Hz = spidev_get_speed(m_spidevHandle);
        if (m_speed_Hz == 0)
        {
            status = kErpcStatus_InitFailed;
        }
    }

    /* Set SPI device word length */
    if (status == kErpcStatus_Success)
    {
        if ((ERPC_SPIDEV_STATUS_SUCCESS != spidev_set_wordbits(m_spidevHandle, 8)))
        {
            status = kErpcStatus_InitFailed;
        }
    }

    /* Set SPI device msb\lsb order */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SPIDEV_STATUS_SUCCESS != spidev_set_lsbmsb(m_spidevHandle,ERPC_SPIDEV_MSBFIRST))
        {
            status = kErpcStatus_InitFailed;
        }
    }

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
#ifdef __USE_SYSFS__
    /* Initialize the GPIO SPI_INT_PIN */
    /* Export GPIO */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SYSGPIO_STATUS_SUCCESS != gpio_export(ERPC_BOARD_SPI_INT_PIN))
        {
            status = kErpcStatus_InitFailed;
        }
    }

    /* Set GPIO direction to input */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SYSGPIO_STATUS_SUCCESS != gpio_direction(ERPC_BOARD_SPI_INT_PIN, 1))
        {
            status = kErpcStatus_InitFailed;
        }
    }

    /* Set GPIO edge interrupt trigger */
    if (status == kErpcStatus_Success)
    {
        if (ERPC_SYSGPIO_STATUS_SUCCESS != gpio_set_edge(ERPC_BOARD_SPI_INT_PIN, (char *)"falling"))
        {
            status = kErpcStatus_InitFailed;
        }
    }

    if (status == kErpcStatus_Success)
    {
        /* Open GPIO file descriptor */
        s_gpioHandle = gpio_open(ERPC_BOARD_SPI_INT_PIN);
        if (s_gpioHandle < ERPC_SYSGPIO_STATUS_SUCCESS)
        {
            status = kErpcStatus_InitFailed;
        }
    }
#else
    if (status == kErpcStatus_Success)
    {
        s_gpioHandle = gpio_req_line(gpio_chipName,ERPC_BOARD_SPI_INT_PIN,GPIOEVENT_REQUEST_FALLING_EDGE);
        if (s_gpioHandle < ERPC_SYSGPIO_STATUS_SUCCESS)
        {
            status = kErpcStatus_InitFailed;
        }
    }
#endif
#endif

    return status;
}

unsigned int SpidevMasterTransport::getSpeed(void)
{
    return m_speed_Hz;
}

int SpidevMasterTransport::getGpioHandle(void)
{
    return s_gpioHandle;
}

erpc_status_t SpidevMasterTransport::underlyingSend(const uint8_t *data, uint32_t size)
{
    erpc_status_t status = kErpcStatus_Success;

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
    SpidevMasterTransport_WaitForSlaveReadyGpio();
#endif

    if (ERPC_SPIDEV_STATUS_SUCCESS != spidev_transfer(m_spidevHandle, data, NULL, size))
    {
        status = kErpcStatus_SendFailed;
    }

    return status;
}

erpc_status_t SpidevMasterTransport::underlyingReceive(uint8_t *data, uint32_t size)
{
    erpc_status_t status = kErpcStatus_Success;

#ifdef ERPC_BOARD_SPI_SLAVE_READY_USE_GPIO
    SpidevMasterTransport_WaitForSlaveReadyGpio();
#else
    SpidevMasterTransport_WaitForSlaveReadyMarker(m_spidevHandle);
#endif

    if (ERPC_SPIDEV_STATUS_SUCCESS != spidev_transfer(m_spidevHandle, NULL, data, size))
    {
        status = kErpcStatus_ReceiveFailed;
    }

    return status;
}
