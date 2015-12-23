ifeq ($(TARGET_BOARD_PLATFORM),bcm2709)
include $(call first-makefiles-under,$(call my-dir))
endif
