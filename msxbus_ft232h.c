#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <libusb.h>

// MSX bus commands
#define RD_SLTSL1   0x00
#define RD_SLTSL2   0x01
#define RD_MEM      0x02
#define RD_IO       0x03
#define WR_SLTSL1   0x04
#define WR_SLTSL2   0x05
#define WR_MEM      0x06
#define WR_IO       0x07

#define FT232H_VID  0x0403
#define FT232H_PID  0x6014
#define ENDPOINT_IN  0x81
#define ENDPOINT_OUT 0x02
#define TIMEOUT     1000

static libusb_device_handle *dev_handle = NULL;

static bool ft232h_init(void) {
    int ret;
    
    ret = libusb_init(NULL);
    if (ret != 0) return false;
    
    dev_handle = libusb_open_device_with_vid_pid(NULL, FT232H_VID, FT232H_PID);
    if (!dev_handle) return false;
    
    // Reset device
    ret = libusb_control_transfer(dev_handle, 0x40, 0x00, 0x0000, 0x0000, NULL, 0, TIMEOUT);
    if (ret != 0) return false;
    
    // Set latency timer
    ret = libusb_control_transfer(dev_handle, 0x40, 0x09, 1, 0x0000, NULL, 0, TIMEOUT);
    if (ret != 0) return false;
    
    return true;
}

static void ft232h_close(void) {
    if (dev_handle) {
        libusb_close(dev_handle);
        libusb_exit(NULL);
    }
}

uint8_t msxread(uint8_t cmd, uint16_t addr) {
    uint8_t tx_buf[3] = {cmd, addr & 0xFF, (addr >> 8) & 0xFF};
    uint8_t rx_buf[3] = {0};
    int transferred, ret;
    
    // Send command and address
    ret = libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, tx_buf, 3, &transferred, TIMEOUT);
    if (ret != 0 || transferred != 3) return 0xFF;
    
    // Read first two bytes
    ret = libusb_bulk_transfer(dev_handle, ENDPOINT_IN, rx_buf, 2, &transferred, TIMEOUT);
    if (ret != 0 || transferred != 2) return 0xFF;
    
    // If second byte is 0xFF, read one more byte
    if (rx_buf[1] == 0xFF) {
        ret = libusb_bulk_transfer(dev_handle, ENDPOINT_IN, rx_buf + 2, 1, &transferred, TIMEOUT);
        if (ret != 0 || transferred != 1) return 0xFF;
        return rx_buf[2];
    }
    
    // If first byte is 0xFF, return second byte
    if (rx_buf[0] == 0xFF) {
        return rx_buf[1];
    }
    
    return 0xFF;
}

void msxwrite(uint8_t cmd, uint16_t addr, uint8_t data) {
    uint8_t tx_buf[4] = {cmd, addr & 0xFF, (addr >> 8) & 0xFF, data};
    uint8_t rx_buf[1] = {0};
    int transferred, ret;
    
    // Send command, address and data
    ret = libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, tx_buf, 4, &transferred, TIMEOUT);
    if (ret != 0 || transferred != 4) return;
    
    // Wait for ACK (0xFF)
    do {
        ret = libusb_bulk_transfer(dev_handle, ENDPOINT_IN, rx_buf, 1, &transferred, TIMEOUT);
    } while (ret == 0 && transferred == 1 && rx_buf[0] != 0xFF);
}

// Example usage in main()
int main(void) {
    if (!ft232h_init()) {
        printf("Failed to initialize FT232H\n");
        return 1;
    }
    
    uint8_t buffer[16];
    
    // Read memory from 0x4000 to 0xBFFF
    for (uint16_t addr = 0x4000; addr < 0xC000; addr += 16) {
        printf("%04X: ", addr);
        
        // Read 16 bytes
        for (int i = 0; i < 16; i++) {
            buffer[i] = msxread(RD_MEM, addr + i);
            printf("%02X ", buffer[i]);
        }
        printf("\n");
    }
    
    ft232h_close();
    return 0;
}