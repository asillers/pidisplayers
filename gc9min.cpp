#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>   // <-- gives you rand()
#include <time.h>     // <-- optional, for seeding

// === your pins ===
static const int PIN_DC  = 5;   // GPIO5 (D/C)
static const int PIN_CS  = 7;   // GPIO7 (but ignored)
static const int PIN_RST = 6;   // GPIO6 (reset)

static const char *SPI_DEV = "/dev/spidev0.0";  // same one Python uses
static const uint32_t SPI_SPEED = 500000;       // 0.5 MHz so flicker is visible
static const uint8_t  SPI_MODE  = SPI_MODE_3;
static const uint8_t  SPI_BITS  = 8;



int spi_fd;

void exportPin(int pin) {
    char path[64];
    sprintf(path, "/sys/class/gpio/gpio%d", pin);
    if (access(path, F_OK)==0) return;

    int fd=open("/sys/class/gpio/export",O_WRONLY);
    char buf[8]; int n=sprintf(buf,"%d",pin);
    write(fd,buf,n); close(fd);

    sprintf(path,"/sys/class/gpio/gpio%d/direction",pin);
    fd=open(path,O_WRONLY);
    write(fd,"out",3); close(fd);
}

void writePin(int pin,int val) {
    char p[64];
    sprintf(p,"/sys/class/gpio/gpio%d/value",pin);
    int fd=open(p,O_WRONLY);
    write(fd,val?"1":"0",1);
    close(fd);
}

int main() {
    printf("=== GC9 RAW SPI TEST ===\n");

    srand(time(NULL));

    exportPin(PIN_DC);
    exportPin(PIN_RST);
    exportPin(PIN_CS);

    // keep CS LOW permanently (your GC9 ignores CS toggling)
    writePin(PIN_CS, 0);

    // hardware reset
    writePin(PIN_RST,0);
    usleep(50000);
    writePin(PIN_RST,1);
    usleep(50000);

    // open SPI
    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) { perror("spi open"); return 1; }

    ioctl(spi_fd, SPI_IOC_WR_MODE, &SPI_MODE);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED);

    printf("Sending raw garbage onto MOSI forever...\n");

    // random garbage buffer
    uint8_t buf[256];

    while (1) {
        // fill with new random noise
        for (int i = 0; i < 256; i++)
            buf[i] = rand() & 0xFF;

        // randomize DC to break panel's interpretation
        writePin(PIN_DC, rand() & 1);

        // blast raw electrical noise into SPI MOSI
        write(spi_fd, buf, sizeof(buf));

        // short wait so your eye can observe flicker
        usleep(5000);  
    }

    return 0;
}