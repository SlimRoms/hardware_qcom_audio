<<<<<<< HEAD
ifneq ($(TARGET_PROVIDES_LIBAUDIO),true)
ifneq ($(BOARD_USES_AUDIO_LEGACY),true)
ifeq ($(BOARD_USES_QCOM_HARDWARE),true)

AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
    include $(AUDIO_HW_ROOT)/alsa_sound/Android.mk
    include $(AUDIO_HW_ROOT)/libalsa-intf/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm7x27a)
    include $(AUDIO_HW_ROOT)/msm7627a/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm7x30)
    include $(AUDIO_HW_ROOT)/msm7x30/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
    include $(AUDIO_HW_ROOT)/msm8660/Android.mk
    include $(AUDIO_HW_ROOT)/mm-audio/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
    include $(AUDIO_HW_ROOT)/mm-audio/Android.mk
endif

endif
endif
=======
QCOM_MEDIA_ROOT := $(call my-dir)
ifneq ($(filter msm8974 msm8960 msm8660 msm7x27a msm7x30,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk
endif

ifneq ($(filter msm8974 msm8960 msm8660 msm7x30,$(TARGET_BOARD_PLATFORM)),)
include $(QCOM_MEDIA_ROOT)/mm-video/Android.mk
include $(QCOM_MEDIA_ROOT)/libI420colorconvert/Android.mk
>>>>>>> 266277eaf42a139afa7cff9a57986109e72eeaca
endif
