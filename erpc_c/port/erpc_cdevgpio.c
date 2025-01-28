#include "erpc_sysgpio.h"

#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>

const char *gpio_chipName = "/dev/gpiochip0";

int gpio_open_chip(const char *chipName)
{
   int ret;
   struct gpiochip_info chip_info;

   int chipFd = open(chipName, O_RDONLY);
   if (chipFd < 0) {
      printf("cannot open chip, errno=%d\n",errno);
      return -1;
   }

   return chipFd;
}

    const char *listener_label = "gpio-erpc-listener";

int gpio_req_line(const char *chipName,int lineOffs,uint32_t events)
{
   int ret, chipFd = gpio_open_chip(chipName);
   struct gpiohandle_request line_request;
   struct gpioevent_request event_request;

   if (chipFd <= 0) return -1;

   event_request.lineoffset = lineOffs;
   event_request.eventflags = events;
   event_request.handleflags = GPIOHANDLE_REQUEST_INPUT;
   strcpy(event_request.consumer_label,listener_label);
   ret = ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &event_request);
   if (ret < 0 || event_request.fd <= 0) {
      printf("cannot request event(s), errno=%d\n",errno);
      return -3;
   }

   close(chipFd);

   return event_request.fd;
}

int gpio_poll_line(int evreqFd,int timeout)
{
    return gpio_poll_line_intr(evreqFd,timeout,NULL);
}

int gpio_poll_line_intr(int evreqFd,int timeout,f_poll_line_intr intr)
{
   int ret;
   struct pollfd poll_file_descriptor;

   poll_file_descriptor.fd = evreqFd;
   poll_file_descriptor.events = POLLIN /*| POLLPRI*/;
   poll_file_descriptor.revents = 0;

   ret = poll(&poll_file_descriptor, 1, timeout);
   if (ret == 0) return -1;
   if (ret < 0) return -2;

   if (poll_file_descriptor.revents & (POLLIN /*| POLLPRI*/)) {
      struct gpioevent_data event_data;
      ret = read(poll_file_descriptor.fd, &event_data, sizeof(event_data));
      if (ret < sizeof(event_data)) return -2;
      if (event_data.id == GPIOEVENT_EVENT_RISING_EDGE) {
        if (intr != NULL)
            intr(GPIOEVENT_EVENT_RISING_EDGE,event_data.timestamp);
        return 1;
      }
      else if (event_data.id == GPIOEVENT_EVENT_FALLING_EDGE) {
        if (intr != NULL)
            intr(GPIOEVENT_EVENT_FALLING_EDGE,event_data.timestamp);
        return 0;
      }
      else return -2;
   }

   return -2;
}
