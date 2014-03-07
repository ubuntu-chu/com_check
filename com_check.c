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
#include <signal.h>

/*----------------------------------------------------------------------*/

#define FALSE			(1)
#define TRUE			(0)

#define BITS_DEFAULT	(8)
#define STOP_DEFAULT	(1)
#define PARITY_DEFAULT	('N')

#define BAUD_DEFAULT	(9600)

#define WIDTH_ADD		(20)
#if 0

#define WIDTH_INIT		(2720)
#define WIDTH_TOTAL 	(3024)
#else

#define WIDTH_INIT		(120)
#define WIDTH_TOTAL 	(1024)
#endif

#define DATA_PREFIX(x)  "<"x">"
#define DEV_FILE_PATH	"/dev/ttySAC"

#define DEBUG			(1)
#define NONBLOCKING_ACCESS	(1)

enum {
	FUNCTION_SEND	= 1,
	FUNCTION_RECV	= 2,
	FUNCTION_SEND_RECV	= 3,
};

struct baud_setting{
	int		m_baud;
	int     m_value;
};

/*----------------------------------------------------------------------*/

int  src_fd		= -1;
struct termios src_oldtermios;
int  dest_fd	= -1; 
struct termios dest_oldtermios;
sig_atomic_t run = TRUE;

const char valid_input_set[] = {
	'\n', 's', 'q'
};

struct operate_hint_param{
	const char *m_hint_str;
	const char *m_valid_set;
	int			m_numb;
};

const struct operate_hint_param t_hint_param[] = {
	{
		(const char *)"Press Enter to continue, s to skip this step, q to quit, Ctrl+c to interrupt.\n",
		valid_input_set,
		sizeof(valid_input_set)/sizeof(valid_input_set[0]),
	},
};


/*----------------------------------------------------------------------*/

static int stdin_uninit(void);
int dev_close(int fd);
void dev_block(int fd);
void dev_nonblock(int fd);
int tty_dev_close(int fd, struct termios *ptermios);
int dev_conf_save(int fd, struct termios *ptermios);
int dev_conf_restore(int fd, struct termios *ptermios);

//若想中断信号发生时 程序通过判断run的取值为false而退出  则需要更改dev_read函数的逻辑 当内部的read、select
//返回-1且errno为INTR时，不要再进行循环  而是直接退出即可  
//
//目前程序直接在信号处理函数中退出  可根据实际需要更改逻辑即可
void hand_sig(int signo) {

	run = FALSE;
	stdin_uninit();
	tty_dev_close(src_fd, &src_oldtermios);
	tty_dev_close(dest_fd, &dest_oldtermios);

	exit(0);
}

/*----------------------------------------------------------------------*/

struct termios stdin_oldtermios;

static int stdin_init(void){

	struct termios newt;

	dev_conf_save(STDIN_FILENO, &stdin_oldtermios);
	newt			= stdin_oldtermios;  
	//取消回显
	newt.c_lflag	&= ~(ICANON | ECHO);  
	if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0){
		perror("tcsetattr error!\n");
		return -1;
	}

	/* set the nonblock */
	dev_nonblock(STDIN_FILENO);

	return 0;  
}  

static int stdin_uninit(void){

	dev_conf_restore(STDIN_FILENO, &stdin_oldtermios);
	dev_block(STDIN_FILENO);

	return 0;
}

static void stdin_flush(void){

	fd_set rfds;
	struct timeval tv;
	int retval;

	while (1){
		/* Watch stdin (fd 0) to see when it has input. */
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		tv.tv_sec  = 0;
		tv.tv_usec = 0;

		retval = select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv);
		/* Don't rely on the value of tv now! */

		if (retval == -1) {
			if (errno == EINTR){
				continue;
			}else{
				break;
			}
		}else if (retval){
			getchar();
		}else{
			break;
		}
	}
}

static int char_get(void){

	fd_set rfds;
	struct timeval tv;
	int retval;
	int c;

	while (1){
		/* Watch stdin (fd 0) to see when it has input. */
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		tv.tv_sec  = 0;
		tv.tv_usec = 0;

		retval = select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv);
		/* Don't rely on the value of tv now! */

		if (retval == -1) {
			perror("select()");
			if (retval == EINTR){
				continue;
			}else{
				c	= -1;
				break;
			}
		}else if (retval){
			c	= getchar();
			break;
		}
	}

	return c;
}
									   

int operate_hint(const struct operate_hint_param *pparam){

	int		input_char;
	int		i;

	stdin_flush();	
	if (NULL != pparam->m_hint_str){
		printf(pparam->m_hint_str);
	}

	do {
		input_char	= char_get();
		for (i = 0; i < pparam->m_numb; i++){
			if (input_char == pparam->m_valid_set[i]){
				break;
			}
		}
		if (i == pparam->m_numb){
			if (input_char == '\n'){
				printf("[enter]: invalid char input! please input again!\n");
			}else {
				printf("[%c]: invalid char input! please input again!\n", input_char);
			}
			if (NULL != pparam->m_hint_str){
				printf(pparam->m_hint_str);
			}
		}
	}while (i == pparam->m_numb);
#if (DEBUG > 0)

	printf("input char = %d\n", input_char);
#endif

	return input_char;
}

/*----------------------------------------------------------------------*/


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

int dev_conf_save(int fd, struct termios *ptermios){

	if (tcgetattr(fd, ptermios) != 0){
		perror("tcgetattr error!\n");
		return -1;
	}

	return 0;
}

int dev_conf_restore(int fd, struct termios *ptermios){

	if (tcsetattr(fd, TCSANOW, ptermios) != 0){
		perror("tcsetattr error!\n");
		return -1;
	}

	return 0;
}

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

/*
 *
 * 在串口编程模式下，open未设置O_NONBLOCK或O_NDELAY的情况下。c_cc[VTIME]和c_cc[VMIN]影响read函数的返回。
 * 若在open或fcntl设置了O_NDELALY或O_NONBLOCK标志，read调用不会阻塞而是立即返回，那么VTIME和VMIN就没有
 * 意义，效果等同于与把VTIME和VMIN都设为了0。
 *
 * VTIME:定义等待的时间，单位是百毫秒(通常是一个8位的unsigned char变量，取值不能大于cc_t)。
 * VMIN:定义了要求等待的最小字节数，(不是要求读的字节数——read()的第三个参数才是指定要求读的最大字节数)，
 * 这个字节数可能是0
 *
 * c_cc[VMIN] > 0, c_cc[VTIME] == 0 如果VTIME取0，VMIN定义了要求等待读取的最小字节数。
 * 函数read()只有在读取了VMIN个字节的数据或者收到一个信号的时候才返回。
 *
 * c_cc[VMIN] == 0, c_cc[VTIME] > 0 如果VMIN取0，VTIME定义了即使没有数据可以读取，read()函数返回前也要
 * 等待几百毫秒的时间量。这时，read()函数不需要像其通常情况那样要遇到一个文件结束标志才返回0。
 *
 * c_cc[VMIN] > 0, c_cc[VTIME] > 0 如果VTIME和VMIN都不取0，VTIME定义的是当接收到第一个字节的数据后开始
 * 计算等待的时间量。如果当调用read函数
 * 时可以得到数据，计时器马上开始计时。如果当调用read函数时还没有任何数据可读，则等接收到第一个字节的数据后，
 * 计时器开始计时。函数read可能会在读取到VMIN个字节的数据后返回，也可能在计时完毕后返回，这主要取决于哪个
 * 条件首先实现。不过函数至少会读取到一个字节的数据，因为计时器是在读取到第一个数据时开始计时的。
 *
 * c_cc[VMIN] == 0, c_cc[VTIME] == 0 如果VTIME和VMIN都取0，即使读取不到任何数据，函数read也会立即返回。
 * 同时，返回值0表示read函数不需要等待文件结束标志就返回了。
 *
 */

	newtio.c_cc[VTIME] = 0;                 //注意此处的设置
	newtio.c_cc[VMIN] = 1;

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

int dev_path_open(const char * ppath){

	int fd;
#if (NONBLOCKING_ACCESS > 0)
	
	//non - blocking open
    fd = open(ppath, O_RDWR | O_NOCTTY | O_NDELAY | O_SYNC);
#if (DEBUG > 0)

	printf("DBG:fd [%d] non-block open\n", fd);
#endif
#else

    fd = open(ppath, O_RDWR | O_NOCTTY);
#if (DEBUG > 0)

	printf("DBG:fd [%d] block open\n", fd);
#endif
#endif

	return fd;
}

int dev_open(int index){

	return dev_path_open(dev_file_name(index));
}

void dev_block(int fd){
	int flags;

	flags = fcntl(fd,F_GETFL,0);
	flags &= ~O_NONBLOCK;
	fcntl(fd,F_SETFL,flags);
}


void dev_nonblock(int fd){
	int flags;

	flags = fcntl(fd,F_GETFL,0);
	flags |= O_NONBLOCK;
	fcntl(fd,F_SETFL,flags);
}

int dev_close(int fd){

	if (fd < 0){
#if (DEBUG > 0)
		printf("DBG:fd [%d] close\n", fd);
#endif
		return -1;
	}
#if (NONBLOCKING_ACCESS > 0)

	dev_block(fd);
#endif
	tcflush(fd, TCIOFLUSH);
	close(fd);
#if (DEBUG > 0)
	printf("DBG:fd [%d] close\n", fd);
#endif

	return 0;
}

int dev_write(int fd, const char *str, int len){

	int n		= 0;
	int tot_n	= 0;

	while (len){

		n = write(fd, str+tot_n, len);
		if (n < 0){
			if (errno == EINTR){
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

#if (NONBLOCKING_ACCESS > 0)

int char_read(int fd, char *buf, int len, int maxwaittime)	
{
	int		no		= 0;
	int		rt;
	int		loop	= 1;
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
			if (errno == EINTR){
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
					if (errno == EINTR){
						continue;
					}
				#if (DEBUG > 0)

					printf("no = %d, len = %d, read return -1\n", no, len);
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
int dev_read(int fd, char *buf, int *plen, int wait_time){

	int		len				= 0;
	int		rt				= -1;
	int     cnt_per_read	= 10;
	int     interval_time_per_read	= wait_time;    //unit: ms
	
	if (NULL == buf){
		return rt;
	}

#if (DEBUG > 0)

	printf("DBG:non-blocking read called\n");
#endif
	while ((rt = char_read(fd, (char *)&buf[len], cnt_per_read, interval_time_per_read)) > 0){
		len			+= rt;
		interval_time_per_read	= 100;
	}
	if (NULL != plen){
		*plen				= len;
	}

	return len;
}
#else

//blocking read
int dev_read(int fd, char *buf, int *plen, int wait_time){

	int		len				= 0;
	int		rt				= -1;
	(void)wait_time;

	if (NULL == buf){
		return rt;
	}

#if (DEBUG > 0)

	printf("DBG:blocking read called\n");
#endif
	while (1){

		len = read(fd, (char *)buf, WIDTH_TOTAL);
		if (len < 0){
			if (errno == EINTR){
				continue;
			}else {
				break;
			}
		}else {
			break;
		}
	}
	if (NULL != plen){
		*plen				= len;
	}

	return len;
}
#endif

/*----------------------------------------------------------------------*/

int tty_dev_open(const char *pdev, struct termios *ptermios, int baud){

	int fd;

	fd = dev_path_open(pdev);

	if (fd < 0) {
		fprintf(stderr, "Error opening %s: %s\n", pdev, strerror(errno));
		return -1;
	}

	if (dev_conf_save(fd, ptermios) < 0){
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

int tty_dev_close(int fd, struct termios *ptermios){

	if (fd < 0){
		printf("fd[%d] tty dev close failed\n", fd);
		return -1;
	}
	dev_conf_restore(fd, ptermios);
	return dev_close(fd);
}

/*----------------------------------------------------------------------*/

/* The name of this program */
const char * program_name;

const char *const short_options = "hd:t:f:b:l:";

const struct option long_options[] = {
	{ "help",   0, NULL, 'h'},
	{ "src device", 1, NULL, 'd'},
	{ "dest device", 1, NULL, 't'},
	{ "function", 1, NULL, 'f'},
	{ "baud", 1, NULL, 'b'},
	{ "loop", 1, NULL, 'l'},
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
            "\t-l  --loop		  Loop per test, -1 means infinite loop\n"
			);
    exit(exit_code);
}

/*----------------------------------------------------------------------*/

int channel_numb_get(int *ptotal_numb, int *fist_channel){

	int		i= 1;
	int		fd;
	int		first = -1;
	int		total = 0;

	while (1){

		if ((fd = dev_open(i)) < 0){
			break;
		}
		if (-1 == first){
			first= i;
		}
		dev_close(fd);
		total++;
		i++;
	}
	if (NULL != ptotal_numb){
		*ptotal_numb = total;
	}
	if (NULL != fist_channel){
		*fist_channel = first;
	}
#if (DEBUG > 0)

	printf("DBG:total numb:%d, first numb:%d\n", total, first);
#endif

	return 1;
}

/*----------------------------------------------------------------------*/

/*
	*@breif  main()
 */
int main(int argc, char *argv[])
{
	int  next_option, havearg = 0;
    char *src_dev = NULL; /* Default device */
    char *dest_dev = NULL; /* Default device */
	char *send_dev;
	char *recv_dev;
	int send_fd = -1;
	int recv_fd = -1;
	int switch_flg = 0;

	int read_wait_time;
	int loop_cnt	= 1;
	int loop_total  = -1;
	int fail_cnt, success_cnt;
 	int nread, nsend, send;			/* Read the counts of data */
	char xmit[WIDTH_TOTAL];
 	char buff[WIDTH_TOTAL];		/* Recvice data buffer */

#if 0
    /*** ext Uart Test program ***/
    char recv_buff[512];
    unsigned long total = 0;
    unsigned long samecnt = 0;
#endif
	time_t	now_time;
	int function = FUNCTION_SEND_RECV;
	FILE *fs_p = NULL;  
	unsigned int seed = 0;  
	int  width_total = WIDTH_INIT;
	int  index, rt;
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
            case 'l':
                loop_total = atoi(optarg);
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
					goto quit;
				}
				break;
            case -1:
				if (havearg)  break;
            case '?':
                print_usage (stderr, 1);
            default:
                goto quit;
        }
    }while(next_option != -1);

	if ((NULL == src_dev) && (NULL == dest_dev)){
		printf("please assign src_dev or dest_dev\n");
		goto quit;
	}else if ((NULL != src_dev) && (NULL != dest_dev)){
		if (0 == strcmp(src_dev, dest_dev)){
			printf("src_dev can not be dest_dev\n");
			goto quit;
		}
	}
	if (NULL != src_dev){
		src_fd = tty_dev_open((const char *)src_dev, &src_oldtermios, baud);
		if (src_fd < 0){
			goto quit_1;
		}
	}
	if (NULL != dest_dev){
		dest_fd = tty_dev_open((const char *)dest_dev, &dest_oldtermios, baud);
		if (dest_fd < 0){
			goto quit_1;
		}
	}
	stdin_init();
	stdin_flush();
	signal(SIGINT, hand_sig);
	fs_p = fopen ("/dev/urandom", "r");  
	if (NULL == fs_p)   
	{  
		printf("Can not open /dev/urandom\n");  
		goto quit_2;
	}
	
	switch_flg					= 0;
	fail_cnt					= 0;
	success_cnt					= 0;
	read_wait_time				= 1000;      //unit:ms
	while (((loop_cnt <= loop_total) || (-1 == loop_total)) && (TRUE == run)){
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
			read_wait_time		= 0;
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
		if (-1 != loop_total){
			printf("loop tot.: [%d]\n", loop_total);
		}else {
			printf("loop tot.: [infinite loop]\n");
		}
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
			send = dev_write(send_fd, xmit, nsend);
			if (send != nsend){
				printf("send err! continue\n");
				continue;
			}
			printf("send len : [%d]\n", nsend);
			printf("send data: %s\n", xmit);
			tcflush(send, TCIOFLUSH);
			sleep(2);
		//	sleep(15);
		}
		if (recv_fd > 0){
			memset(buff, 0, sizeof(buff));
			rt = dev_read(recv_fd, buff, &nread, read_wait_time);
			if (rt > 0) {
				printf("recv len : [%d]\n", nread);
				printf("recv data: %s\n", buff);
			}else if (rt == 0){
				printf("recv     : [timeout! please check hardware!]\n");
			}else {
			//	printf("recv err : [%s]\n", strerror(errno));
				printf("recv     : [err!]\n");
			}
		}
		if (FUNCTION_SEND_RECV == function){
			if ((nread == nsend) && (0 == strcmp(buff, xmit))){
				printf("result   : [------------ok-----------]\n\n");
				success_cnt++;
			}else {
				printf("result   : [-----------fail----------]\n\n");
				fail_cnt++;
			}
		}
		sleep(1);
	}
	if (TRUE == run){
		if ((FUNCTION_SEND_RECV == function) && (-1 != loop_total)){
			printf("<---------------------summarize--------------------->\n");
			printf("device[%s] <-----> device[%s]\n", src_dev, dest_dev);
			printf("totol   count: [%d]\n", loop_total);
			printf("success count: [%d]\n", success_cnt);
			printf("fail    count: [%d]\n", fail_cnt);
			printf("success rate : [%.2f%%]\n", ((float)success_cnt/loop_total)*100);
		}
	}else {
		printf("SIGINT signal occured!\n");
	}
	fclose(fs_p);
quit_2:
	stdin_uninit();

quit_1:
	tty_dev_close(src_fd, &src_oldtermios);
	tty_dev_close(dest_fd, &dest_oldtermios);

	return 0;
quit:
	return -1;
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

