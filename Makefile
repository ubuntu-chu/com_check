src := $(wildcard *.c)
objs := $(patsubst %.c, %.o, $(src))
target := com_check
cc := arm-linux-gcc
cflag := -Wall

all:$(target)

$(target):$(objs)
	$(cc) $(cflag) -o $@ $^
	cp $(target) /home/barnard/work/board_9G25/rootfs/home/bsp/

%.o:%.c
	$(cc) $(cflag) -c -o $@ $<

copy:
	cp run_com_check.sh /home/barnard/work/board_9G25/rootfs/home/bsp/

clean:
	rm -rf $(target) $(objs)


.PHONY:all clean copy


