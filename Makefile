src := $(wildcard *.c)
objs := $(patsubst %.c, %.o, $(src))
target := com
cc := arm-linux-gcc

all:$(target)

$(target):$(objs)
	$(cc) -o $@ $^
	cp $(target) run_com_check.sh /home/barnard/work/board_9G25/rootfs/home/bsp/

%.o:%.c
	$(cc) -c -o $@ $<

clean:
	rm -rf $(target) $(objs)


.PHONY:all clean


