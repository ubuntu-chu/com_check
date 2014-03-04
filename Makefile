src := $(wildcard *.c)
objs := $(patsubst %.c, %.o, $(src))
target := com
cc := arm-linux-gcc

all:$(target)

$(target):$(objs)
	$(cc) -o $@ $^

%.o:%.c
	$(cc) -c -o $@ $<

clean:
	rm -rf $(target) $(objs)


.PHONY:all clean


