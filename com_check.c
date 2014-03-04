#include <stdio.h>      /*标准输入输出定义*/
#include <stdlib.h>
#include <unistd.h>     /*Unix标准函数定义*/
#include <sys/types.h>  /**/
#include <sys/stat.h>   /**/
#include <fcntl.h>      /*文件控制定义*/
#include <termios.h>    /*PPSIX终端控制定义*/
#include <errno.h>      /*错误号定义*/
#include <getopt.h>
#include <string.h>
#include <time.h>

#define FALSE			(1)
#define TRUE			(0)

#define BITS_DEFAULT	(8)
#define STOP_DEFAULT	(1)
#define PARITY_DEFAULT	('N')

#define BAUD_DEFAULT	(9600)

#define WIDTH_ADD		(20)
//#define WIDTH_INIT		(2720)
//#define WIDTH_TOTAL 	(3024)

#define WIDTH_INIT		(120)
#define WIDTH_TOTAL 	(1024)

#define DATA_PREFIX(x)  "<"x">"


enum {
	FUNCTION_SEND	= 1,
	FUNCTION_RECV	= 2,
	FUNCTION_SEND_RECV	= 3,
};

struct baud_setting{
	int		m_baud;
	int     m_value;
};

const struct baud_setting t_serial_baud_setting[] = {
	{921600,	B921600},	
	{460800,	B460800},	
	{230400,	B230400},	
	{115200,	B115200},	
	{57600 ,	B57600 },	
	{38400 ,	B38400 },	
	{19200 ,	B19200 },	
	{9600  ,	B9600  },	
	{4800  ,	B4800  },	
	{2400  ,	B2400  },	
	{1200  ,	B1200  },	
	{300   ,	B300   },	
};

int dev_conf(int fd, int baud, int databits, int stopbits, int parity){

	struct termios newtio;
	int i;

    tcgetattr(fd, &newtio);
	bzero(&newtio, sizeof(newtio));

	//baud
	for (i = 0;  i < sizeof(t_serial_baud_setting)/sizeof(t_serial_baud_setting[0]);  i++) {
		if (baud == t_serial_baud_setting[i].m_baud){
			cfsetispeed(&newtio, t_serial_baud_setting[i].m_value);
			cfsetospeed(&newtio, t_serial_baud_setting[i].m_value);
			break;
		}
	}
	if (i >= sizeof(t_serial_baud_setting)/sizeof(t_serial_baud_setting[0])){
		printf("dev baud param invalid!\n");
		return -1;
	}

	//setting   c_cflag
	newtio.c_cflag &= ~CSIZE;
	/*设置数据位数 */
	switch (databits) {
	case 7:
		newtio.c_cflag |= CS7;	//7位数据位
		break; 
	case 8:
		newtio.c_cflag |= CS8;	//8位数据位
		break;
	default:
		newtio.c_cflag |= CS8;
		break;
	}
	//设置校验
	switch (parity){
	case 'n':
	case 'N':
		newtio.c_cflag &= ~PARENB;	/* Clear parity enable */
		newtio.c_iflag &= ~INPCK;	/* Enable parity checking */
		break;
	case 'o':
	case 'O':
		newtio.c_cflag |= (PARODD | PARENB);	/* 设置为奇效验 */
		newtio.c_iflag |= INPCK;	/* Disnable parity checking */
		break;
	case 'e':
	case 'E':
	   newtio.c_cflag |= PARENB;	/* Enable parity */
	   newtio.c_cflag &= ~PARODD;	/* 转换为偶效验 */
	   newtio.c_iflag |= INPCK;	/* Disnable parity checking */
		break;
	case 'S':
	case 's':			/*as no parity */
		newtio.c_cflag &= ~PARENB; newtio.c_cflag &= ~CSTOPB; break; default:
       newtio.c_cflag &= ~PARENB;	/* Clear parity enable */
       newtio.c_iflag &= ~INPCK;	/* Enable parity checking */
       break;
	}
	//设置停止位
    switch (stopbits){
	case 1:
       newtio.c_cflag &= ~CSTOPB;	//1
       break;
	case 2:
       newtio.c_cflag |= CSTOPB;	//2
		break;
	default:
       newtio.c_cflag &= ~CSTOPB;
	   break;
	}
	newtio.c_cc[VTIME] = 0; newtio.c_cc[VMIN] = 0;
	newtio.c_cflag |= (CLOCAL | CREAD);
	//newtio.c_oflag |= OPOST;
	newtio.c_iflag &= ~(IXON | IXOFF | IXANY);
	newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	newtio.c_oflag &=~ OPOST;

	tcflush(fd, TCIFLUSH);
	if (tcsetattr(fd, TCSANOW, &newtio) != 0) {
		perror("serial dev config fail");
		return -1;
	}
	return 0;
}

/**
	*@breif 打开串口
*/
const char * dev_file_name(int index){

	static char	device_str[100];

	sprintf(device_str, DEV_FILE_PATH"%d", index);
#if (DEBUG > 0)

	printf("DBG:device name:%s\n", device_str);
#endif

	return device_str;
}

int dev_open(int index){

	int fd;
	//non - blocking open
    fd = open(dev_file_name(index), O_RDWR | O_NOCTTY | O_NDELAY);
	printf("DBG:fd [%d] open\n", fd);

	return fd;
}

int dev_close(int fd){

	tcflush(fd, TCIOFLUSH);
	close(fd);
	printf("DBG:fd [%d] close\n", fd);

	return 0;
}

int dev_write(int fd, const char *str, int len){

	int rt;
	int n		= 0;
	int tot_n	= 0;
	const char *str_tmp	= str;

	while (len){

		n = write(fd, str+tot_n, len);
		if (n < 0){
			if (n == EINTR){
				continue;
			}else {
				break;
			}
		}
		tot_n	+= n;
		len		-= n;
	}

	return tot_n;
}

int char_read(int fd, char *buf, int len, int maxwaittime)	
{
	int		no		= 0;
	int		rt;
	int		loop	= 1;
	int		rtnum	= len;
	struct	timeval tv;
	struct	timeval *ptimeval;
	fd_set	readfd;

	if (maxwaittime == 0){
		ptimeval	= NULL;
	}else {
		tv.tv_sec	= maxwaittime / 1000;	//SECOND
		tv.tv_usec	= maxwaittime % 1000 * 1000;	//USECOND
		ptimeval	= &tv;
	}

	while (loop){

		loop		= 0;
		FD_ZERO(&readfd);
		FD_SET(fd, &readfd);
		rt = select(fd + 1, &readfd, NULL, NULL, ptimeval);
		if (rt == -1){
			if (rt == EINTR){
				loop	= 1;
				continue;
			}else{
				break;
			}
		}else if (rt > 0) {
			while (len) {
				rt = read(fd, &buf[no], len);
				if (rt > 0){
					no += rt;
				}else {
				#if (DEBUG > 0)

					printf("no = %d, len = %d, read return -1\n", no, rtnum);
				#endif
					return no;
				}
				len = len - rt;
			}
			return no;
		}else {
			//time out
		#if (DEBUG > 0)

			//printf("select time out rt = %d\n", rt);
		#endif
		}
	}

	return rt;
}

//non-blocking read
int dev_read(int fd, char *buf, int *plen){

	int		maxwaittime		= 0;
	int		len				= 0;
	int		rt;
	int     cnt_per_read	= 10;
	
	if (NULL == buf){
		return -1;
	}

	maxwaittime		= 1000;
	while ((rt = char_read(fd, (char *)&buf[len], cnt_per_read, maxwaittime)) > 0){
		len			+= rt;
		maxwaittime  = 100;
	}
	if (NULL != plen){
		*plen				= len;
	}

	return 0;
}

int tty_dev_open(const char *pdev, int baud){

	int fd;

	fd = dev_open(pdev);

	if (fd < 0) {
		fprintf(stderr, "Error opening %s: %s\n", pdev, strerror(errno));
		return -1;
	}
	if (dev_conf(fd, baud, BITS_DEFAULT, STOP_DEFAULT, PARITY_DEFAULT) < 0) {
		fprintf(stderr, "%s: Set Parity Error\n", pdev);
		close(fd);
		return -1;
	}
	printf("device [%s]: setting\n", pdev);
	printf("           : fd  \t[%d]\n", fd);

	printf("           : baud\t[%dbps]\n", baud);
	printf("           : bits\t[%d]\n", BITS_DEFAULT);
	printf("           : stop\t[%d]\n", STOP_DEFAULT);
	printf("           : parity\t[%c]\n", PARITY_DEFAULT);

	return fd;
}

/* The name of this program */
const char * program_name;

const char *const short_options = "hd:t:f:b:";

const struct option long_options[] = {
	{ "help",   0, NULL, 'h'},
	{ "src device", 1, NULL, 'd'},
	{ "dest device", 1, NULL, 't'},
	{ "function", 1, NULL, 'f'},
	{ "baud", 1, NULL, 'b'},
	{ NULL,     0, NULL, 0  }
};

/* Prints usage information for this program to STREAM (typically
 * stdout or stderr), and exit the program with EXIT_CODE. Does not
 * return.
 */

void print_usage (FILE *stream, int exit_code)
{
    fprintf(stream, "Usage: %s option [ dev... ] \n", program_name);
    fprintf(stream,
            "\t-h  --help		  Display this usage information.\n"
            "\t-d  --src device   The device ttySAC[1-8] or ttyEXT[0-3]\n"
            "\t-t  --dest device  The device ttySAC[1-8] or ttyEXT[0-3]\n"
            "\t-f  --function     Function:send  recv  [send&recv] when -f=send&recv -t param is invalid\n"
            "\t-b  --baud		  Baud per sec:value 2400 4800 9600 19200 38400 57600 115200\n"
			);
    exit(exit_code);
}
/*
	*@breif  main()
 */
int main(int argc, char *argv[])
{
    int  src_fd = -1, dest_fd = -1, next_option, havearg = 0;
	int  option_d_assigned = 0, option_t_assigned  = 0;
    char *src_dev = NULL; /* Default device */
    char *dest_dev = NULL; /* Default device */
	char *send_dev;
	char *recv_dev;
	int send_fd = -1;
	int recv_fd = -1;
	int switch_flg = 0;

	int loop_cnt	= 1;
 	int nread, nsend, send;			/* Read the counts of data */
	pid_t pid;
	char xmit[WIDTH_TOTAL];
 	char buff[WIDTH_TOTAL];		/* Recvice data buffer */

#if 0
    /*** ext Uart Test program ***/
    char recv_buff[512];
    unsigned long total = 0;
    unsigned long samecnt = 0;
#endif
	int	count	= 0;
	time_t	now_time;
	int function = FUNCTION_SEND_RECV;
	FILE *fs_p = NULL;  
	unsigned int seed = 0;  
	int	 random_send;
	int  width_ctrl, width_total = WIDTH_INIT;
	int  index;
	int  baud	= BAUD_DEFAULT;
		      
    program_name = argv[0];
    do {
        next_option = getopt_long (argc, argv, short_options, long_options, NULL);
        switch (next_option) {
            case 'h':
                print_usage (stdout, 0);
            case 'd':
                src_dev = optarg;
				havearg = 1;
				break;
            case 't':
                dest_dev = optarg;
				havearg = 1;
				break;
            case 'b':
                baud = atoi(optarg);
				havearg = 1;
				break;
            case 'f':
				if (0 == strcmp("send", optarg)){
					function	= FUNCTION_SEND;
					havearg = 1;
				}else if (0 == strcmp("recv", optarg)){
					function	= FUNCTION_RECV;
					havearg = 1;
				}else if (0 == strcmp("send&recv", optarg)){
					function	= FUNCTION_SEND_RECV;
					havearg = 1;
				}else {
					print_usage (stderr, 1);
					abort ();
				}
				break;
            case -1:
				if (havearg)  break;
            case '?':
                print_usage (stderr, 1);
            default:
                abort ();
        }
    }while(next_option != -1);

	if ((NULL == src_dev) && (NULL == dest_dev)){
		printf("please assign src_dev or dest_dev\n");
		return -1;
	}else if ((NULL != src_dev) && (NULL != dest_dev)){
		if (0 == strcmp(src_dev, dest_dev)){
			printf("src_dev can not be dest_dev\n");
			return -1;
		}
	}
	if (NULL != src_dev){
		src_fd = tty_dev_open((const char *)src_dev, baud);
		if (src_fd < 0){
			return -1;
		}
	}
	if (NULL != dest_dev){
		dest_fd = tty_dev_open((const char *)dest_dev, baud);
		if (dest_fd < 0){
			return -1;
		}
	}
	fs_p = fopen ("/dev/urandom", "r");  
	if (NULL == fs_p)   
	{  
		printf("Can not open /dev/urandom\n");  
		return -1;  
	}
	
	switch_flg					= 0;
	while (1){
		now_time				= time(NULL);
		strcpy(xmit, ctime((const time_t *)&now_time));
		xmit[strlen(xmit) - 1]		= 0;

		if (FUNCTION_SEND == function){
			send_fd				= src_fd;
			send_dev			= src_dev;
			recv_fd				= -1;
			recv_dev			= "external device";
		}else if (FUNCTION_RECV == function){
			send_fd				= -1;
			send_dev			= "external device";
			recv_fd				= src_fd;
			recv_dev			= src_dev;
		}else {

			if (switch_flg){
				send_fd				= dest_fd;
				send_dev			= dest_dev;
				recv_fd				= src_fd;
				recv_dev			= src_dev;
				switch_flg			= 0;
			}else {
				send_fd				= src_fd;
				send_dev			= src_dev;
				recv_fd				= dest_fd;
				recv_dev			= dest_dev;
				switch_flg			= 1;
			}
		}
		printf("[%s]: %s(fd = %d) -> %s(fd = %d)\n", xmit, send_dev, send_fd, recv_dev, recv_fd);
		printf("loop cnts: [%d]\n", loop_cnt);
		fread(&seed, sizeof(int), 1, fs_p);  //obtain one unsigned int data   
		srand(seed);  
		memset(xmit, 0, sizeof(xmit));
		//printf("Random numner is %u.\n", rand());  
		index = sprintf(xmit, "[seq = %d, len = %d]", loop_cnt, width_total);
		loop_cnt++;
		sprintf(&xmit[index], DATA_PREFIX("%0*d"), width_total - index - strlen(DATA_PREFIX()), rand());
		width_total		+= WIDTH_ADD;
		if (width_total >= WIDTH_TOTAL){
			width_total	= WIDTH_INIT;
		}
		if (send_fd > 0){
			nsend	= strlen(xmit);
			//nsend	= 1;
			send = write(send_fd, xmit, nsend);
			if (send != nsend){
				printf("send err! continue\n");
				continue;
			}
			printf("send len : %d\n", nsend);
			printf("send data: %s\n", xmit);
			sleep(2);
		//	sleep(15);
		}
		if (recv_fd > 0){
			memset(buff, 0, sizeof(buff));
			nread = read(recv_fd, buff, sizeof(buff));
			if (nread > 0) {
				printf("recv len : %d\n", nread);
				printf("recv data: %s\n", buff);
			}else {
				printf("recv err : [%s]\n", strerror(errno));

			}
			sleep(1);
		}
		if (FUNCTION_SEND_RECV == function){
			if ((nread == nsend) && (0 == strcmp(buff, xmit))){
				printf("result   : [------------ok-----------]\n\n");
			}else {
				printf("result   : [-----------fail----------]\n\n");
			}
		}
	}
#if 0
	pid = fork();	
	if (pid < 0) { 
		fprintf(stderr, "Error in fork!\n"); 
    } else if (pid == 0){
		while(1) {
			count++;
			nread	= strlen(xmit);
			printf("DEV  name :\t%s\n", src_dev);
			printf("SEND count:\t%d\n", count);
			printf("SEND len  :\t%d\n", nread);
			printf("SEND data :\t%s\n", xmit);
			write(fd, xmit, nread);
			sleep(1);
		}
		exit(0);
    } else { 
		while(1) {
			nread = read(fd, buff, sizeof(buff));
			if (nread > 0) {
				count++;
				buff[nread] = '\0';
				printf("DEV  name :\t%s\n", src_dev);
				printf("RECV count:\t%d\n", count);
				printf("RECV len  :\t%d\n", nread);
				printf("RECV data :\t%s\n", buff);
			}
		}	
    }
#endif

#if 0
    while ( 1 )
    {
	nread = read(fd, buff, sizeof(buff));
	if(nread > 0)
	{
	    buff[nread] = '\0';
		printf("RECV len :%d\n", nread);
	    printf("RECV data: %s\n", buff);
	}
    }
    close(fd);
    exit(0);
#endif

	return 0;
}

