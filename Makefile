RELEASE ?= $(shell uname -r)

all:
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/jbd2 modules
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/ext4 modules
 
clean:
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/jbd2 clean
	make -C /lib/modules/$(RELEASE)/build M=$(PWD)/kernel/fs/ext4 clean
