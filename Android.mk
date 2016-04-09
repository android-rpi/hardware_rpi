ifeq ($(TARGET_BOARD_PLATFORM),bcm2710)
include $(call first-makefiles-under,$(call my-dir))
endif
