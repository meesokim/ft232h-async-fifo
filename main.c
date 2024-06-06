#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <err.h>

#include <libusb.h>

/*
ft232h-async-fifo

A small tool to use a FT232H as an *asynchronous* FIFO for transmitting data to the PC. Uses only libusb.

(c) 2024 by kittennbfive - https://github.com/kittennbfive/

AGPLv3+ and NO WARRANTY! Experimental stuff!

Please read the fine manual.
*/

#define SIZE_BUF 512

#define LATENCY_TIMER_VALUE 255 //1-255, set to 0 to leave unchanged

#define NB_PAR_TRANSFERS 8

#define RUN_FOR_SECONDS 0 //set to 0 to run forever/until Ctrl+C


//--- do not change anything below this line ---

#define TIME_DELTA_SEC(tv_a, tv_b) (((tv_b.tv_sec*1E6+tv_b.tv_usec)-(tv_a.tv_sec*1E6+tv_a.tv_usec))/1E6)

#if RUN_FOR_SECONDS
	static struct timeval tv_started;
#endif

static bool running=true;

//a struct is not needed for a single variable, but it makes the code more future proof for further additions...
typedef struct
{
	uint_fast64_t nb_transfer;
} user_data_t;

#if !RUN_FOR_SECONDS
static void sighandler(int sig)
{
	(void)sig;
	fprintf(stderr, "\nreceived Ctrl+C, stopping\n");
	running=false;
}
#endif

static void cb(struct libusb_transfer *transfer)
{
	static uint_fast64_t next_nb_transfer=0;
	
	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	uint_fast64_t * ptr_nb_transfer=&(((user_data_t*)(transfer->user_data))->nb_transfer);
	
#if RUN_FOR_SECONDS
	if(TIME_DELTA_SEC(tv_started, tv)>RUN_FOR_SECONDS)
	{
		fprintf(stderr, "time limit reached, stopping\n");
		running=false;
	}
#endif
	
	if(transfer->status==LIBUSB_TRANSFER_COMPLETED)
	{
		if(transfer->actual_length>2) //more than modem status stuff?
		{
			if(next_nb_transfer==(*ptr_nb_transfer))
				fwrite(&transfer->buffer[2], (transfer->actual_length-2), 1, stdout);
			else
				errx(1, "inside callback: data arrival out of order! expected %lu but got %lu", next_nb_transfer, *ptr_nb_transfer);
		}
		
		if(running)
		{
			(*ptr_nb_transfer)+=NB_PAR_TRANSFERS;
			next_nb_transfer++;
			if(libusb_submit_transfer(transfer))
				errx(1, "inside callback: libusb_submit_transfer for transfer %lu failed", *ptr_nb_transfer);
		}
		else
			libusb_free_transfer(transfer);
	}
	else
		errx(1, "inside callback: status %u (%s) != LIBUSB_TRANSFER_COMPLETED for transfer %lu\n", (uint)transfer->status, libusb_error_name(transfer->status), *ptr_nb_transfer);
}

int main(void)
{
	fprintf(stderr, "This is ft232h-async-fifo by kittennbfive - AGPLv3+ - NO WARRANTY\n");
	
#if !RUN_FOR_SECONDS
	struct sigaction act;
	act.sa_handler=&sighandler;
	act.sa_flags=0;
	
	if(sigaction(SIGINT, &act, NULL)<0)
		err(1, "registering handler for SIGINT failed");
#endif
	
	int ret;
	
	ret=libusb_init(NULL);
	if(ret!=0)
		errx(1, "libusb_init: %s", libusb_strerror(ret));
	
	fprintf(stderr, "libusb init ok\n");
	
	libusb_device ** list;
	libusb_device * found=NULL;
	libusb_device_handle * dev_handle;
	struct libusb_device_descriptor descriptor;
	
	ssize_t nb_dev=libusb_get_device_list(NULL, &list);
	fprintf(stderr, "found %ld USB devices\n", nb_dev);
	
	ssize_t i;
	for(i=0; i<nb_dev; i++)
	{
		ret=libusb_get_device_descriptor(list[i], &descriptor);
		if(ret!=0)
			errx(1, "libusb_get_device_descriptor: %s", libusb_strerror(ret));
		
		if(descriptor.idVendor==0x0403 && descriptor.idProduct==0x6014) //FT232H
		{
			found=list[i];
			break;
		}	
	}
	
	if(found==NULL)
		errx(1, "device not found");
	
	libusb_free_device_list(list, 1);
	
	ret=libusb_open(found, &dev_handle);
	if(ret!=0)
		errx(1, "libusb_open: %s", libusb_strerror(ret));
	
	fprintf(stderr, "device opened\n");
	
	//RESET
	ret=libusb_control_transfer(dev_handle, 0x40, 0x00, 0x0000, 0x0000, NULL, 0, 100);
	if(ret!=0)
		errx(1, "libusb_control_transfer for RESET: %s", libusb_strerror(ret));
	
	fprintf(stderr, "RESET sent\n");

#if LATENCY_TIMER_VALUE
	//LATENCY TIMER (ms)
	ret=libusb_control_transfer(dev_handle, 0x40, 0x09, LATENCY_TIMER_VALUE, 0x0000, NULL, 0, 100);
	if(ret!=0)
		errx(1, "libusb_control_transfer for latency timer: %s", libusb_strerror(ret));
	
	fprintf(stderr, "latency timer set to %ums\n", LATENCY_TIMER_VALUE);
#endif
	
	fprintf(stderr, "using %u parallel transfers\n", NB_PAR_TRANSFERS);
	
	uint8_t * buf[NB_PAR_TRANSFERS];
	struct libusb_transfer * transfers[NB_PAR_TRANSFERS];
	user_data_t user_data[NB_PAR_TRANSFERS];
	
	for(i=0; i<NB_PAR_TRANSFERS; i++)
	{
		buf[i]=malloc(SIZE_BUF*sizeof(uint8_t));
		transfers[i]=libusb_alloc_transfer(0);
		
		user_data[i].nb_transfer=i;
	
		libusb_fill_bulk_transfer(transfers[i], dev_handle, 0x81, buf[i], SIZE_BUF, &cb, &user_data[i], 0);
		
		ret=libusb_submit_transfer(transfers[i]);
		if(ret!=0)
			errx(1, "libusb_submit_transfer for %ld failed: %s", i, libusb_strerror(ret));
	}
	
	fprintf(stderr, "%u transfers submitted\n", NB_PAR_TRANSFERS);

#if RUN_FOR_SECONDS
	gettimeofday(&tv_started, NULL);
#endif
	
	while(running)
		libusb_handle_events(NULL);
	
	fprintf(stderr, "cleanup\n");
	
	libusb_close(dev_handle);
	libusb_exit(NULL);
	
	for(i=0; i<NB_PAR_TRANSFERS; i++)
		free(buf[i]);
	
	fprintf(stderr, "all done\n\n");
	
	return 0;
}
