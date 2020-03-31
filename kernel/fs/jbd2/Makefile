#
# Makefile for the linux journaling routines.
#

#obj-$(CONFIG_JBD2) += jbd2.o
obj-m += jbd2_vmsep.o

jbd2_vmsep-objs := transaction.o commit.o recovery.o checkpoint.o revoke.o journal.o
