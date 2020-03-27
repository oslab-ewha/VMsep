RELEASE ?= $(shell uname -r)

all:
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/jbd2
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/ext4
 
clean:
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/jbd2 clean
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/ext4 clean
