#
# Makefile for storage/ipc
#
# src/backend/storage/ipc/Makefile
#

subdir = src/backend/storage/ipc
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	barrier.o \
	dsm.o \
	dsm_impl.o \
	dsm_registry.o \
	ipc.o \
	ipci.o \
	latch.o \
	pmsignal.o \
	procarray.o \
	procsignal.o \
	shm_mq.o \
	shm_toc.o \
	shmem.o \
	signalfuncs.o \
	sinval.o \
	sinvaladt.o \
	standby.o \
	waiteventset.o

include $(top_srcdir)/src/backend/common.mk
