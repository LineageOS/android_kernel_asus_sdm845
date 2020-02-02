#
# Makefile for the focaltech touchscreen drivers.
#

# Each configuration option enables a list of files.
obj-$(CONFIG_TOUCHSCREEN_FTS) += focaltech_ft8716f.o

focaltech_ft8716f-objs	 := focaltech_core.o \
							focaltech_ex_fun.o \
							focaltech_ex_mode.o \
							focaltech_flash.o \
							focaltech_gesture.o \
							focaltech_esdcheck.o \
							focaltech_i2c.o \
							focaltech_point_report_check.o
							
focaltech_ft8716f-objs	 += focaltech_flash/focaltech_upgrade_ft8716.o

focaltech_ft8716f-objs	 += focaltech_test/focaltech_test.o \
							focaltech_test/focaltech_test_ini.o \
							focaltech_test/focaltech_test_detail_threshold.o \
							focaltech_test/supported_ic/focaltech_test_ft8716.o
							

#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_core.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_ex_fun.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_ex_mode.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_flash.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_gesture.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)	+=  focaltech_esdcheck.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)   +=  focaltech_i2c.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)   +=  focaltech_point_report_check.o
#obj-$(CONFIG_TOUCHSCREEN_FTS)   +=  focaltech_flash/
#obj-$(CONFIG_TOUCHSCREEN_FTS)   +=  focaltech_test/
