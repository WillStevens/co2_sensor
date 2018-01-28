/* Program for reading CO2 concentration in ppm from the MH-Z14A CO2 sensor. This program turns off automatic baseline correction,
 * and sets the range of the sensor to 10000ppm. CO2 concentration is output on the standard output every 10 seconds. */

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define BAUDRATE B9600
#define DEVICE "/dev/ttyUSB0"

#define TRACE 0

typedef enum {None,CO2Level,ABCOff,SetRange,Other} PacketType;

/* The most recent CO2 level received (ppm) */
int co2_level = -1;

/* If a byte has been received, process it. If a complete packet has been received
   then return the packet type. If the return value is CO2Level, then the co2_level variable
   has been updated with the received CO2 level */
PacketType rx_packet(int fd)
{
  static int rxstate = 1;
  static unsigned char checksum = 0;
  static unsigned char byte3;
  static PacketType packet = None;
  unsigned char c;

  if (read(fd,&c,1)==1)
  {
    if (TRACE) printf("%d ",(int)c);
    checksum += c;
    switch(rxstate)
    {
      case 1: // Waiting for 0xff start of packet
        if (c==0xff)
        {
          rxstate = 2;
          checksum = c;
        }
        break;
      case 2: // Waiting for command byte
        switch(c)
        {
          case 0x86:
            packet = CO2Level;
            break;
          case 0x79:
            packet = ABCOff;
            break;
          case 0x99:
            packet = SetRange;
            break;
          default:
            packet = Other;
            break;
        }
        rxstate = 3;
        break;
      case 3:
        byte3 = c;
        rxstate = 4;
        break;
      case 4:
        if (packet == CO2Level)
        {
          co2_level = 256*byte3 + c;
        }
      case 5:
      case 6:
      case 7:
      case 8:
        rxstate++;
        break;
      case 9: // Checksum byte
        rxstate = 1;
        if (TRACE) printf(" :%d:%d:\n",(int)checksum,(int)packet); 
        if (checksum == 0xff)
          return packet;
        break;
      default:
        break;
    }
  }

  return None;
}

/* Send a packet to request the CO2 level */
void request_co2_level(int fd)
{
  static unsigned char buf[9] = {0xff,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79};
  write(fd,buf,9);
}

/* Send a packet to turn off Automatic Baseline Correction (ABC) */
void abc_off(int fd)
{
  static unsigned char buf[9] = {0xff,0x01,0x79,0x00,0x00,0x00,0x00,0x00,0x86};
  write(fd,buf,9);
}

/* Set range - range can be 2000,5000,10000. Returns -1 if invalid range passed */
int set_range(int fd, int range)
{
  static unsigned char buf[9] = {0xff,0x01,0x99,0x00,0x00,0x00,0x00,0x00,0x00};

  if (range != 2000 && range != 5000 && range != 10000)
    return -1;
 
  buf[3] = range/256;
  buf[4] = range%256;

  buf[8] = (0xff - ((buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]+buf[7])&0xff) + 1) & 0xff;  

  write(fd,buf,9);

  return 0;
}

int main(void)
{
  int fd;
  struct termios oldtio,newtio;
  time_t ref_time,cur_time;
  int timed_out;

  fd = open(DEVICE,O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd < 0) {perror(DEVICE); return -1;}

  tcgetattr(fd,&oldtio);

  bzero(&newtio, sizeof(newtio));

  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;
  newtio.c_lflag = 0;

  /* These values mean: read() is satisfied either when VMIN=1 characters are received,
     or when VTIME=1 tenths of a second have expired */
  newtio.c_cc[VTIME] = 1;
  newtio.c_cc[VMIN] = 1;

  tcflush(fd,TCIFLUSH);
  tcsetattr(fd,TCSANOW,&newtio);

  /* Send ABC off command and wait for response */
  if (TRACE) printf("Requesting ABC off\n");
  abc_off(fd);
  ref_time = time(NULL);
  timed_out = 1;

  while (time(NULL)-ref_time < 2)
  {
    fflush(stdout);
    if (rx_packet(fd) == ABCOff)
    {
      timed_out = 0;
      break;
    }
  }

  if (timed_out)
  {
    fprintf(stderr,"Error initialising sensor - did not receive response from 'ABC off' command\n");
    return 1;
  }

  set_range(fd,10000);
  ref_time = time(NULL);
  timed_out = 1;

  while (time(NULL)-ref_time < 2)
  {
    fflush(stdout);
    if (rx_packet(fd) == SetRange)
    {
      timed_out = 0;
      break;
    }
  }

  if (timed_out)
  {
    fprintf(stderr,"Error initialising sensor - did not receive response from 'Set range' command\n");
    return 1;
  }

  ref_time = time(NULL);

  if (TRACE) printf("Starting CO2 readings\n");
  fflush(stdout);
  while(1)
  {
    if (rx_packet(fd) == CO2Level)
    {
      printf("%d\n",co2_level);
      fflush(stdout);
    }

    cur_time = time(NULL);

    if (cur_time - ref_time > 10)
    {
      if (TRACE) printf("Requesting CO2 level\n");
      request_co2_level(fd);
      ref_time += 10;
    }
  }
  
  tcsetattr(fd,TCSANOW,&oldtio);

  return 0;
}
