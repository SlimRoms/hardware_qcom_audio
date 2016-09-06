#AUDIO_POLICY_TEST := true
#ENABLE_AUDIO_DUMP := true

LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  common_cflags += -DWITH_A2DP
endif

ifeq ($(QCOM_FM_ENABLED),true)
  common_cflags += -DQCOM_FM_ENABLED
endif

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
  common_cflags += -DQCOM_ACDB_ENABLED
endif

ifeq ($(BOARD_HAVE_SAMSUNG_AUDIO),true)
  common_cflags += -DSAMSUNG_AUDIO
endif

ifeq ($(BOARD_HAVE_SONY_AUDIO),true)
  common_cflags += -DSONY_AUDIO
endif

ifeq ($(BOARD_HAVE_BACK_MIC_CAMCORDER),true)
  common_cflags += -DBACK_MIC_CAMCORDER
endif

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_CFLAGS := -D_POSIX_SOURCE

LOCAL_SRC_FILES := \
    AudioHardware.cpp \
    audio_hw_hal.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils       \
    libutils        \
    libmedia        \
    libaudioalsa    \
    libacdbloader   \
    libacdbmapper

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper \
    libaudiohw_legacy

LOCAL_MODULE := audio.primary.msm8660
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -fno-short-enums

LOCAL_C_INCLUDES := $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audcal
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-acdb-util
LOCAL_C_INCLUDES += hardware/libhardware/include
LOCAL_C_INCLUDES += hardware/libhardware_legacy/include
LOCAL_C_INCLUDES += frameworks/base/include
LOCAL_C_INCLUDES += system/core/include

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_CFLAGS += $(common_cflags) -Wno-error

include $(BUILD_SHARED_LIBRARY)
