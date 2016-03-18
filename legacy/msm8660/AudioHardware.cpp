/*
** Copyright 2008, The Android Open-Source Project
** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <math.h>

#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardwareMSM8660"
#include <utils/Log.h>
#include <utils/String8.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "AudioHardware.h"
#include <media/AudioSystem.h>
#include <cutils/properties.h>

#include <linux/android_pmem.h>
#ifdef QCOM_ACDB_ENABLED
#include <linux/msm_audio_acdb.h>
#endif
#include <sys/mman.h>
#include "control.h"
#include "acdb.h"

#ifdef HTC_ACOUSTIC_AUDIO
    extern "C" {
    #include <linux/spi_aic3254.h>
    #include <linux/tpa2051d3.h>
    }
    #define DSP_EFFECT_KEY "dolby_srs_eq"
#endif

#define VOICE_SESSION_NAME "Voice session"
#define VOIP_SESSION_NAME "VoIP session"

// hardware specific functions

#define LOG_SND_RPC 0  // Set to 1 to log sound RPC's

#define DUALMIC_KEY "dualmic_enabled"
#define BTHEADSET_VGS "bt_headset_vgs"
#define TTY_MODE_KEY "tty_mode"
#define ECHO_SUPRESSION "ec_supported"

#define VOIPRATE_KEY "voip_rate"

#define MVS_DEVICE "/dev/msm_mvs"

#ifdef QCOM_FM_ENABLED
#define FM_DEVICE  "/dev/msm_fm"
#define FM_A2DP_REC 1
#define FM_FILE_REC 2
#endif

#ifdef QCOM_ACDB_ENABLED
#define INVALID_ACDB_ID -1
#endif

namespace android_audio_legacy {

Mutex   mDeviceSwitchLock;
#ifdef HTC_ACOUSTIC_AUDIO
Mutex   mAIC3254ConfigLock;
#endif
static int audpre_index, tx_iir_index;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};
static const uint32_t INVALID_DEVICE                        = 65535;
static const uint32_t SND_DEVICE_CURRENT                    = -1;
static const uint32_t SND_DEVICE_HANDSET                    = 0;
static const uint32_t SND_DEVICE_SPEAKER                    = 1;
static const uint32_t SND_DEVICE_HEADSET                    = 2;
static const uint32_t SND_DEVICE_FM_HANDSET                 = 3;
static const uint32_t SND_DEVICE_FM_SPEAKER                 = 4;
static const uint32_t SND_DEVICE_FM_HEADSET                 = 5;
static const uint32_t SND_DEVICE_BT                         = 6;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER        = 7;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET             = 8;
static const uint32_t SND_DEVICE_IN_S_SADC_OUT_HANDSET      = 9;
static const uint32_t SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE= 10;
static const uint32_t SND_DEVICE_TTY_HEADSET                = 11;
static const uint32_t SND_DEVICE_TTY_HCO                    = 12;
static const uint32_t SND_DEVICE_TTY_VCO                    = 13;
static const uint32_t SND_DEVICE_TTY_FULL                   = 14;
static const uint32_t SND_DEVICE_HDMI                       = 15;
static const uint32_t SND_DEVICE_CARKIT                     = -1;
static const uint32_t SND_DEVICE_HEADPHONE_AND_SPEAKER      = 18;
static const uint32_t SND_DEVICE_FM_TX                      = 19;
static const uint32_t SND_DEVICE_FM_TX_AND_SPEAKER          = 20;
static const uint32_t SND_DEVICE_SPEAKER_TX                 = 21;
static const uint32_t SND_DEVICE_BACK_MIC_CAMCORDER         = 33;
#ifdef HTC_ACOUSTIC_AUDIO
static const uint32_t SND_DEVICE_SPEAKER_BACK_MIC           = 26;
static const uint32_t SND_DEVICE_HANDSET_BACK_MIC           = 27;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET_BACK_MIC    = 28;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC = 30;
static const uint32_t SND_DEVICE_I2S_SPEAKER                = 32;
static const uint32_t SND_DEVICE_BT_EC_OFF                  = 45;
static const uint32_t SND_DEVICE_HAC                        = 252;
static const uint32_t SND_DEVICE_USB_HEADSET                = 253;
#else
static const uint32_t SND_DEVICE_BT_EC_OFF                  = -1;
#endif
#ifdef SAMSUNG_AUDIO
static uint32_t SND_DEVICE_VOIP_HANDSET               = 50;
static uint32_t SND_DEVICE_VOIP_SPEAKER               = 51;
static uint32_t SND_DEVICE_VOIP_HEADSET               = 52;
static uint32_t SND_DEVICE_CALL_HANDSET               = 60;
static uint32_t SND_DEVICE_CALL_SPEAKER               = 61;
static uint32_t SND_DEVICE_CALL_HEADSET               = 62;
static uint32_t SND_DEVICE_VR_SPEAKER                 = 70;
static uint32_t SND_DEVICE_VR_HEADSET                 = 71;
static uint32_t SND_DEVICE_HAC                        = 252;
static uint32_t SND_DEVICE_USB_HEADSET                = 253;
#endif
static const uint32_t DEVICE_HANDSET_RX            = 0; // handset_rx
static const uint32_t DEVICE_HANDSET_TX            = 1;//handset_tx
static const uint32_t DEVICE_SPEAKER_RX            = 2; //speaker_stereo_rx
static const uint32_t DEVICE_SPEAKER_TX            = 3;//speaker_mono_tx
static const uint32_t DEVICE_HEADSET_RX            = 4; //headset_stereo_rx
static const uint32_t DEVICE_HEADSET_TX            = 5; //headset_mono_tx
static const uint32_t DEVICE_FMRADIO_HANDSET_RX    = 6; //fmradio_handset_rx
static const uint32_t DEVICE_FMRADIO_HEADSET_RX    = 7; //fmradio_headset_rx
static const uint32_t DEVICE_FMRADIO_SPEAKER_RX    = 8; //fmradio_speaker_rx
static const uint32_t DEVICE_DUALMIC_HANDSET_TX    = 9; //handset_dual_mic_endfire_tx
static const uint32_t DEVICE_DUALMIC_SPEAKER_TX    = 10; //speaker_dual_mic_endfire_tx
static const uint32_t DEVICE_TTY_HEADSET_MONO_RX   = 11; //tty_headset_mono_rx
static const uint32_t DEVICE_TTY_HEADSET_MONO_TX   = 12; //tty_headset_mono_tx
static const uint32_t DEVICE_SPEAKER_HEADSET_RX    = 13; //headset_stereo_speaker_stereo_rx
static const uint32_t DEVICE_FMRADIO_STEREO_TX     = 14;
static const uint32_t DEVICE_HDMI_STERO_RX         = 15; //hdmi_stereo_rx
static const uint32_t DEVICE_BT_SCO_RX             = 17; //bt_sco_rx
static const uint32_t DEVICE_BT_SCO_TX             = 18; //bt_sco_tx
static const uint32_t DEVICE_FMRADIO_STEREO_RX     = 19;
#ifdef SAMSUNG_AUDIO
// Samsung devices
static uint32_t DEVICE_HANDSET_VOIP_RX       = 40; // handset_voip_rx
static uint32_t DEVICE_HANDSET_VOIP_TX       = 41; // handset_voip_tx
static uint32_t DEVICE_SPEAKER_VOIP_RX       = 42; // speaker_voip_rx
static uint32_t DEVICE_SPEAKER_VOIP_TX       = 43; // speaker_voip_tx
static uint32_t DEVICE_HEADSET_VOIP_RX       = 44; // headset_voip_rx
static uint32_t DEVICE_HEADSET_VOIP_TX       = 45; // headset_voip_tx
static uint32_t DEVICE_HANDSET_CALL_RX       = 60; // handset_call_rx
static uint32_t DEVICE_HANDSET_CALL_TX       = 61; // handset_call_tx
static uint32_t DEVICE_SPEAKER_CALL_RX       = 62; // speaker_call_rx
static uint32_t DEVICE_SPEAKER_CALL_TX       = 63; // speaker_call_tx
static uint32_t DEVICE_HEADSET_CALL_RX       = 64; // headset_call_rx
static uint32_t DEVICE_HEADSET_CALL_TX       = 65; // headset_call_tx
static uint32_t DEVICE_SPEAKER_VR_TX         = 82; // speaker_vr_tx
static uint32_t DEVICE_HEADSET_VR_TX         = 83; // headset_vr_tx
#endif
static uint32_t DEVICE_CAMCORDER_TX          = 105; // camcoder_tx (misspelled by Samsung)
                                                    // secondary_mic_tx (sony)

static uint32_t FLUENCE_MODE_ENDFIRE   = 0;
static uint32_t FLUENCE_MODE_BROADSIDE = 1;
static int vr_enable = 0;

int dev_cnt = 0;
const char ** name = NULL;
int mixer_cnt = 0;
static uint32_t cur_tx = INVALID_DEVICE;
static uint32_t cur_rx = INVALID_DEVICE;
int voice_session_id = 0;
int voice_session_mute = 0;
static bool dualmic_enabled = false;
bool vMicMute = false;

#ifdef QCOM_ACDB_ENABLED
static bool bInitACDB = false;
#endif
#ifdef HTC_ACOUSTIC_AUDIO
int rx_htc_acdb = 0;
int tx_htc_acdb = 0;
static bool support_aic3254 = true;
static bool aic3254_enabled = true;
int (*set_sound_effect)(const char* effect);
static bool support_tpa2051 = true;
static bool support_htc_backmic = true;
static bool fm_enabled = false;
static int alt_enable = 0;
static int hac_enable = 0;
static uint32_t cur_aic_tx = UPLINK_OFF;
static uint32_t cur_aic_rx = DOWNLINK_OFF;
static int cur_tpa_mode = 0;
#endif

typedef struct routing_table
{
    unsigned short dec_id;
    int dev_id;
    int dev_id_tx;
    int stream_type;
    bool active;
    struct routing_table *next;
} Routing_table;
Routing_table* head;
Mutex       mRoutingTableLock;

typedef struct device_table
{
    int dev_id;
    int acdb_id;
    int class_id;
    int capability;
}Device_table;
Device_table* device_list;

enum STREAM_TYPES {
    PCM_PLAY=1,
    PCM_REC,
    VOICE_CALL,
#ifdef QCOM_FM_ENABLED
    FM_RADIO,
    FM_REC,
    FM_A2DP,
#endif
    INVALID_STREAM
};

typedef struct ComboDeviceType
{
    uint32_t DeviceId;
    STREAM_TYPES StreamType;
}CurrentComboDeviceStruct;
CurrentComboDeviceStruct CurrentComboDeviceData;
Mutex   mComboDeviceLock;

#ifdef QCOM_FM_ENABLED
enum FM_STATE {
    FM_INVALID=1,
    FM_OFF,
    FM_ON
};

FM_STATE fmState = FM_INVALID;
#endif

static uint32_t fmDevice = INVALID_DEVICE;

#define MAX_DEVICE_COUNT 200
#define DEV_ID(X) device_list[X].dev_id
#ifdef QCOM_ACDB_ENABLED
#define ACDB_ID(X) device_list[X].acdb_id
#endif
#define CAPABILITY(X) device_list[X].capability

void addToTable(int decoder_id,int device_id,int device_id_tx,int stream_type,bool active) {
    Routing_table* temp_ptr;
    ALOGD("addToTable stream %d",stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = (Routing_table* ) malloc(sizeof(Routing_table));
    temp_ptr->next = NULL;
    temp_ptr->dec_id = decoder_id;
    temp_ptr->dev_id = device_id;
    temp_ptr->dev_id_tx = device_id_tx;
    temp_ptr->stream_type = stream_type;
    temp_ptr->active = active;
    //make sure Voice node is always on top.
    //For voice call device Switching, there a limitation
    //Routing must happen before disabling/Enabling device.
    if(head->next != NULL){
       if(head->next->stream_type == VOICE_CALL){
          temp_ptr->next = head->next->next;
          head->next->next = temp_ptr;
          return;
       }
    }
    //add new Node to head.
    temp_ptr->next =head->next;
    head->next = temp_ptr;
}

bool isStreamOn(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type)
                return true;
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndActive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == true) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndInactive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == false) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

Routing_table*  getNodeByStreamType(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            return temp_ptr;
        }
        temp_ptr=temp_ptr->next;
    }
    return NULL;
}

void modifyActiveStateOfStream(int Stream_type, bool Active) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->active = Active;
        }
        temp_ptr=temp_ptr->next;
    }
}

void modifyActiveDeviceOfStream(int Stream_type,int Device_id,int Device_id_tx) {
    Routing_table* temp_ptr;
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->dev_id = Device_id;
            temp_ptr->dev_id_tx = Device_id_tx;
        }
        temp_ptr=temp_ptr->next;
    }
}

void printTable()
{
    Routing_table * temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        printf("%d %d %d %d %d\n",temp_ptr->dec_id,temp_ptr->dev_id,temp_ptr->dev_id_tx,temp_ptr->stream_type,temp_ptr->active);
        temp_ptr = temp_ptr->next;
    }
}

void deleteFromTable(int Stream_type) {
    Routing_table *temp_ptr,*temp1;
    ALOGD("deleteFromTable stream %d",Stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head;
    while(temp_ptr->next!=NULL) {
        if(temp_ptr->next->stream_type == Stream_type) {
            temp1 = temp_ptr->next;
            temp_ptr->next = temp_ptr->next->next;
            free(temp1);
            return;
        }
        temp_ptr=temp_ptr->next;
    }

}

bool isDeviceListEmpty() {
    if(head->next == NULL)
        return true;
    else
        return false;
}

#ifdef QCOM_ACDB_ENABLED
static void initACDB() {
    while(bInitACDB == false) {
        ALOGD("Calling acdb_loader_init_ACDB()");
        if(acdb_loader_init_ACDB() == 0){
            ALOGD("acdb_loader_init_ACDB() successful");
            bInitACDB = true;
        }
    }
}
#endif

int enableDevice(int device,short enable) {

    // prevent disabling of a device if it doesn't exist
    //Temporaray hack till speaker_tx device is mainlined
    if(DEV_ID(device) == INVALID_DEVICE) {
        return 0;
    }
#ifdef QCOM_ACDB_ENABLED
    if(bInitACDB == false) {
        initACDB();
    }
#endif
    ALOGV("value of device and enable is %d %d ALSA dev id:%d",device,enable,DEV_ID(device));
    if( msm_en_device(DEV_ID(device), enable)) {
        ALOGE("msm_en_device(%d,%d) failed errno = %d",DEV_ID(device),enable, errno);
        return -1;
    }
    return 0;
}

static status_t updateDeviceInfo(int rx_device,int tx_device) {
    bool isRxDeviceEnabled = false,isTxDeviceEnabled = false;
    Routing_table *temp_ptr,*temp_head;
    int tx_dev_prev = INVALID_DEVICE;
    temp_head = head;

    ALOGD("updateDeviceInfo: E");
    Mutex::Autolock lock(mDeviceSwitchLock);

    if(temp_head->next == NULL) {
        ALOGD("simple device switch");
        if(cur_rx!=INVALID_DEVICE)
            enableDevice(cur_rx,0);
        if(cur_tx != INVALID_DEVICE)
            enableDevice(cur_tx,0);
        cur_rx = rx_device;
        cur_tx = tx_device;
        return NO_ERROR;
    }

    Mutex::Autolock lock_1(mRoutingTableLock);

    while(temp_head->next != NULL) {
        temp_ptr = temp_head->next;
        switch(temp_ptr->stream_type) {
            case PCM_PLAY:
#ifdef QCOM_FM_ENABLED
            case FM_RADIO:
            case FM_A2DP:
#endif
                if(rx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("The node type is %d", temp_ptr->stream_type);
                ALOGV("rx_device = %d,temp_ptr->dev_id = %d",rx_device,temp_ptr->dev_id);
                if(rx_device != temp_ptr->dev_id) {
                    enableDevice(temp_ptr->dev_id,0);
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                     ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(isRxDeviceEnabled == false) {
                    enableDevice(rx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(rx_device), CAPABILITY(rx_device));
#endif
                    isRxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(rx_device),1)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(rx_device));
                }
                modifyActiveDeviceOfStream(temp_ptr->stream_type,rx_device,INVALID_DEVICE);
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                break;

            case PCM_REC:

                if(tx_device == INVALID_DEVICE)
                    return -1;

                // If dual  mic is enabled in QualComm settings then that takes preference.
                if ( dualmic_enabled && (DEV_ID(DEVICE_DUALMIC_SPEAKER_TX) != INVALID_DEVICE))
                {
                   tx_device = DEVICE_DUALMIC_SPEAKER_TX;
                }

                ALOGD("case PCM_REC");
                if(isTxDeviceEnabled == false) {
                    enableDevice(temp_ptr->dev_id,0);
                    enableDevice(tx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(tx_device), CAPABILITY(tx_device));
#endif
                    isTxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(tx_device),1)) {
                    ALOGV("msm_route_stream(PCM_REC,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(tx_device));
                }
                modifyActiveDeviceOfStream(PCM_REC,tx_device,INVALID_DEVICE);
                tx_dev_prev = cur_tx;
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                if((vMicMute == true) && (tx_dev_prev != cur_tx)) {
                    ALOGD("REC:device switch with mute enabled :tx_dev_prev %d cur_tx: %d",tx_dev_prev, cur_tx);
                    msm_device_mute(DEV_ID(cur_tx), true);
                }
                break;
            case VOICE_CALL:
                if(rx_device == INVALID_DEVICE || tx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("case VOICE_CALL/VOIP CALL %d",temp_ptr->stream_type);
#ifdef QCOM_ACDB_ENABLED
    #ifdef HTC_ACOUSTIC_AUDIO
                if (rx_htc_acdb == 0)
                    rx_htc_acdb = ACDB_ID(rx_device);
                if (tx_htc_acdb == 0)
                    tx_htc_acdb = ACDB_ID(tx_device);
                ALOGD("acdb_loader_send_voice_cal acdb_rx = %d, acdb_tx = %d", rx_htc_acdb, tx_htc_acdb);
                acdb_loader_send_voice_cal(rx_htc_acdb, tx_htc_acdb);
    #else
                acdb_loader_send_voice_cal(ACDB_ID(rx_device),ACDB_ID(tx_device));
    #endif
#endif
                msm_route_voice(DEV_ID(rx_device),DEV_ID(tx_device),1);

                // Temporary work around for Speaker mode. The driver is not
                // supporting Speaker Rx and Handset Tx combo
                if(isRxDeviceEnabled == false) {
                    if (rx_device != temp_ptr->dev_id)
                    {
                        enableDevice(temp_ptr->dev_id,0);
                    }
                    isRxDeviceEnabled = true;
                }
                if(isTxDeviceEnabled == false) {
                    if (tx_device != temp_ptr->dev_id_tx)
                    {
                        enableDevice(temp_ptr->dev_id_tx,0);
                    }
                    isTxDeviceEnabled = true;
                }

                if (rx_device != temp_ptr->dev_id)
                {
                    enableDevice(rx_device,1);
                }

                if (tx_device != temp_ptr->dev_id_tx)
                {
                    enableDevice(tx_device,1);
                }

                cur_rx = rx_device;
                cur_tx = tx_device;
                modifyActiveDeviceOfStream(temp_ptr->stream_type,cur_rx,cur_tx);
                break;
            default:
                break;
        }
        temp_head = temp_head->next;
    }
    ALOGV("updateDeviceInfo: X");
    return NO_ERROR;
}

void freeMemory() {
    Routing_table *temp;
    while(head != NULL) {
        temp = head->next;
        free(head);
        head = temp;
    }
free(device_list);
}

//
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mFmFd(-1), mBluetoothNrec(true), mBluetoothVGS(false), mBluetoothId(0),
#ifdef HTC_ACOUSTIC_AUDIO
    mHACSetting(false), mBluetoothIdTx(0), mBluetoothIdRx(0),
#endif
    mOutput(0),
    mVoipBitRate(0),
    mCurSndDevice(-1),
    mTtyMode(TTY_OFF), mNumPcmRec(0)
#ifdef HTC_ACOUSTIC_AUDIO
    , mRecordState(false), mEffectEnabled(false)
#endif
{

    int control;
    int i = 0,index = 0;
#ifdef QCOM_ACDB_ENABLED
    int acdb_id = INVALID_ACDB_ID;
#endif
    int fluence_mode = FLUENCE_MODE_ENDFIRE;
    char value[128];
#ifdef HTC_ACOUSTIC_AUDIO
    int (*snd_get_num)();
    int (*snd_get_bt_endpoint)(msm_bt_endpoint *);
    int (*set_acoustic_parameters)();
    int (*set_tpa2051_parameters)();
    int (*set_aic3254_parameters)();
    int (*support_back_mic)();

    struct msm_bt_endpoint *ept;
#endif
        head = (Routing_table* ) malloc(sizeof(Routing_table));
        head->next = NULL;

#ifdef HTC_ACOUSTIC_AUDIO
        acoustic =:: dlopen("/system/lib/libhtc_acoustic.so", RTLD_NOW);
        if (acoustic == NULL ) {
            ALOGD("Could not open libhtc_acoustic.so");
            /* this is not really an error on non-htc devices... */
            mNumBTEndpoints = 0;
            support_aic3254 = false;
            support_tpa2051 = false;
            support_htc_backmic = false;
        }
#endif

        ALOGD("msm_mixer_open: Opening the device");
        control = msm_mixer_open("/dev/snd/controlC0", 0);
        if(control< 0)
                ALOGE("ERROR opening the device");


        mixer_cnt = msm_mixer_count();
        ALOGD("msm_mixer_count:mixer_cnt =%d",mixer_cnt);

        dev_cnt = msm_get_device_count();
        ALOGV("got device_count %d",dev_cnt);
        if (dev_cnt <= 0) {
           ALOGE("NO devices registered\n");
           return;
        }

        //End any voice call if it exists. This is to ensure the next request
        //to voice call after a mediaserver crash or sub system restart
        //is not ignored by the voice driver.
        if (msm_end_voice() < 0)
            ALOGE("msm_end_voice() failed");

        if(msm_reset_all_device() < 0)
            ALOGE("msm_reset_all_device() failed");

        name = msm_get_device_list();
        device_list = (Device_table* )malloc(sizeof(Device_table)*MAX_DEVICE_COUNT);
        if(device_list == NULL) {
            ALOGE("malloc failed for device list");
            return;
        }
        property_get("persist.audio.fluence.mode",value,"0");
        if (!strcmp("broadside", value)) {
              fluence_mode = FLUENCE_MODE_BROADSIDE;
        }

    property_get("persist.audio.vr.enable",value,"Unknown");
    if (!strcmp("true", value))
        vr_enable = 1;

        for(i = 0;i<MAX_DEVICE_COUNT;i++)
            device_list[i].dev_id = INVALID_DEVICE;

        for(i = 0; i < dev_cnt;i++) {
            if(strcmp((char* )name[i],"handset_rx") == 0) {
                index = DEVICE_HANDSET_RX;
            }
            else if(strcmp((char* )name[i],"handset_tx") == 0) {
                index = DEVICE_HANDSET_TX;
            }
            else if((strcmp((char* )name[i],"speaker_stereo_rx") == 0) || 
                    (strcmp((char* )name[i],"speaker_stereo_rx_playback") == 0) ||
                    (strcmp((char* )name[i],"speaker_rx") == 0)) {
                index = DEVICE_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"speaker_mono_tx") == 0) || (strcmp((char* )name[i],"speaker_tx") == 0)) {
                index = DEVICE_SPEAKER_TX;
            }
            else if((strcmp((char* )name[i],"headset_stereo_rx") == 0) || (strcmp((char* )name[i],"headset_rx") == 0)) {
                index = DEVICE_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"headset_mono_tx") == 0) || (strcmp((char* )name[i],"headset_tx") == 0)) {
                index = DEVICE_HEADSET_TX;
            }
            else if(strcmp((char* )name[i],"fmradio_handset_rx") == 0) {
                index = DEVICE_FMRADIO_HANDSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_headset_rx") == 0) || (strcmp((char* )name[i],"fm_radio_headset_rx") == 0)) {
                index = DEVICE_FMRADIO_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_speaker_rx") == 0) || (strcmp((char* )name[i],"fm_radio_speaker_rx") == 0)) {
                index = DEVICE_FMRADIO_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"handset_dual_mic_endfire_tx") == 0) || (strcmp((char* )name[i],"dualmic_handset_ef_tx") == 0)) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Endfire handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if((strcmp((char* )name[i],"speaker_dual_mic_endfire_tx") == 0)|| (strcmp((char* )name[i],"dualmic_speaker_ef_tx") == 0)) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Endfire speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"handset_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Broadside handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"speaker_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Broadside speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_rx") == 0) || (strcmp((char* )name[i],"tty_headset_rx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_RX;
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_tx") == 0) || (strcmp((char* )name[i],"tty_headset_tx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_TX;
            }
            else if((strcmp((char* )name[i],"bt_sco_rx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_rx") == 0)) {
                index = DEVICE_BT_SCO_RX;
            }
            else if((strcmp((char* )name[i],"bt_sco_tx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_tx") == 0)) {
                index = DEVICE_BT_SCO_TX;
            }
            else if((strcmp((char*)name[i],"headset_stereo_speaker_stereo_rx") == 0) ||
                    (strcmp((char*)name[i],"headset_stereo_rx_playback") == 0) ||
                    (strcmp((char*)name[i],"headset_speaker_stereo_rx") == 0) || (strcmp((char*)name[i],"speaker_headset_rx") == 0)) {
                index = DEVICE_SPEAKER_HEADSET_RX;
            }
            else if((strcmp((char*)name[i],"fmradio_stereo_tx") == 0) || (strcmp((char*)name[i],"fm_radio_tx") == 0)) {
                index = DEVICE_FMRADIO_STEREO_TX;
            }
            else if((strcmp((char*)name[i],"hdmi_stereo_rx") == 0) || (strcmp((char*)name[i],"hdmi_rx") == 0)) {
                index = DEVICE_HDMI_STERO_RX;
            }
            else if(strcmp((char*)name[i],"fmradio_stereo_rx") == 0)
                index = DEVICE_FMRADIO_STEREO_RX;
#ifdef SAMSUNG_AUDIO
            else if(strcmp((char* )name[i], "handset_voip_rx") == 0)
                index = DEVICE_HANDSET_VOIP_RX;
            else if(strcmp((char* )name[i], "handset_voip_tx") == 0)
                index = DEVICE_HANDSET_VOIP_TX;
            else if(strcmp((char* )name[i], "speaker_voip_rx") == 0)
                index = DEVICE_SPEAKER_VOIP_RX;
            else if(strcmp((char* )name[i], "speaker_voip_tx") == 0)
                index = DEVICE_SPEAKER_VOIP_TX;
            else if(strcmp((char* )name[i], "headset_voip_rx") == 0)
                index = DEVICE_HEADSET_VOIP_RX;
            else if(strcmp((char* )name[i], "headset_voip_tx") == 0)
                index = DEVICE_HEADSET_VOIP_TX;
            else if(strcmp((char* )name[i], "handset_call_rx") == 0)
                index = DEVICE_HANDSET_CALL_RX;
            else if(strcmp((char* )name[i], "handset_call_tx") == 0)
                index = DEVICE_HANDSET_CALL_TX;
            else if(strcmp((char* )name[i], "speaker_call_rx") == 0)
                index = DEVICE_SPEAKER_CALL_RX;
            else if(strcmp((char* )name[i], "speaker_call_tx") == 0)
                index = DEVICE_SPEAKER_CALL_TX;
            else if(strcmp((char* )name[i], "headset_call_rx") == 0)
                index = DEVICE_HEADSET_CALL_RX;
            else if(strcmp((char* )name[i], "headset_call_tx") == 0)
                index = DEVICE_HEADSET_CALL_TX;
            else if(strcmp((char* )name[i], "speaker_vr_tx") == 0)
                index = DEVICE_SPEAKER_VR_TX;
            else if(strcmp((char* )name[i], "headset_vr_tx") == 0)
                index = DEVICE_HEADSET_VR_TX;
#endif
            else if((strcmp((char* )name[i], "camcoder_tx") == 0) ||
#ifdef SONY_AUDIO
                    (strcmp((char* )name[i], "secondary_mic_tx") == 0))
#else
                    (strcmp((char* )name[i], "camcorder_tx") == 0) ||
                    (strcmp((char* )name[i], "handset_lgcam_tx") == 0))
#endif
                index = DEVICE_CAMCORDER_TX;
            else {
                ALOGI("Not used device: %s", ( char* )name[i]);
                continue;
            }
            ALOGI("index = %d",index);

            device_list[index].dev_id = msm_get_device((char* )name[i]);
            if(device_list[index].dev_id >= 0) {
                    ALOGI("Found device: %s:index = %d,dev_id: %d",( char* )name[i], index,device_list[index].dev_id);
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_mapper_get_acdb_id_from_dev_name((char* )name[i], &device_list[index].acdb_id);
            device_list[index].class_id = msm_get_device_class(device_list[index].dev_id);
            device_list[index].capability = msm_get_device_capability(device_list[index].dev_id);
            ALOGI("acdb ID = %d,class ID = %d,capablity = %d for device %d",device_list[index].acdb_id,
            device_list[index].class_id,device_list[index].capability,device_list[index].dev_id);
#endif
        }

        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
#ifdef HTC_ACOUSTIC_AUDIO
    set_acoustic_parameters = (int (*)(void))::dlsym(acoustic, "set_acoustic_parameters");
    if ((*set_acoustic_parameters) == 0 ) {
        ALOGE("Could not open set_acoustic_parameters()");
        return;
    }

    int rc = set_acoustic_parameters();
    if (rc < 0) {
        ALOGD("Could not set acoustic parameters to share memory: %d", rc);
    }

    /* Check the system property for enable or not the ALT function */
    property_get("htc.audio.alt.enable", value, "0");
    alt_enable = atoi(value);
    ALOGV("Enable ALT function: %d", alt_enable);

    /* Check the system property for enable or not the HAC function */
    property_get("htc.audio.hac.enable", value, "0");
    hac_enable = atoi(value);
    ALOGV("Enable HAC function: %d", hac_enable);

    set_tpa2051_parameters = (int (*)(void))::dlsym(acoustic, "set_tpa2051_parameters");
    if ((*set_tpa2051_parameters) == 0) {
        ALOGI("set_tpa2051_parameters() not present");
        support_tpa2051 = false;
    }

    if (support_tpa2051) {
        if (set_tpa2051_parameters() < 0) {
            ALOGI("Speaker amplifies tpa2051 is not supported");
            support_tpa2051 = false;
        }
    }

    set_aic3254_parameters = (int (*)(void))::dlsym(acoustic, "set_aic3254_parameters");
    if ((*set_aic3254_parameters) == 0 ) {
        ALOGI("set_aic3254_parameters() not present");
        support_aic3254 = false;
    }

    if (support_aic3254) {
        if (set_aic3254_parameters() < 0) {
            ALOGI("AIC3254 DSP is not supported");
            support_aic3254 = false;
        }
    }

    if (support_aic3254) {
        set_sound_effect = (int (*)(const char*))::dlsym(acoustic, "set_sound_effect");
        if ((*set_sound_effect) == 0 ) {
            ALOGI("set_sound_effect() not present");
            ALOGI("AIC3254 DSP is not supported");
            support_aic3254 = false;
        } else
            strcpy(mEffect, "\0");
    }

    support_back_mic = (int (*)(void))::dlsym(acoustic, "support_back_mic");
    if ((*support_back_mic) == 0 ) {
        ALOGI("support_back_mic() not present");
        support_htc_backmic = false;
    }

    if (support_htc_backmic) {
        if (support_back_mic() != 1) {
            ALOGI("HTC DualMic is not supported");
            support_htc_backmic = false;
        }
    }

    snd_get_num = (int (*)(void))::dlsym(acoustic, "snd_get_num");
    if ((*snd_get_num) == 0 ) {
        ALOGD("Could not open snd_get_num()");
    }

    mNumBTEndpoints = snd_get_num();
    ALOGV("mNumBTEndpoints = %d", mNumBTEndpoints);
    mBTEndpoints = new msm_bt_endpoint[mNumBTEndpoints];
    ALOGV("constructed %d SND endpoints)", mNumBTEndpoints);
    ept = mBTEndpoints;
    snd_get_bt_endpoint = (int (*)(msm_bt_endpoint *))::dlsym(acoustic, "snd_get_bt_endpoint");
    if ((*snd_get_bt_endpoint) == 0 ) {
        mInit = true;
        ALOGE("Could not open snd_get_bt_endpoint()");
        return;
    }
    snd_get_bt_endpoint(mBTEndpoints);

    for (int i = 0; i < mNumBTEndpoints; i++) {
        ALOGV("BT name %s (tx,rx)=(%d,%d)", mBTEndpoints[i].name, mBTEndpoints[i].tx, mBTEndpoints[i].rx);
    }
#endif
    mInit = true;
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
    closeOutputStream((AudioStreamOut*)mOutput);
    if (acoustic) {
        ::dlclose(acoustic);
        acoustic = 0;
    }
    msm_mixer_close();
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_deallocate_ACDB();
#endif
    freeMemory();

    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}
// default implementation calls its "without flags" counterpart
AudioStreamOut* AudioHardware::openOutputStreamWithFlags(uint32_t devices,
                                          audio_output_flags_t flags __unused,
                                          int *format,
                                          uint32_t *channels,
                                          uint32_t *sampleRate,
                                          status_t *status)
{
    return openOutputStream(devices, format, channels, sampleRate, status);
}

AudioStreamOut* AudioHardware::openOutputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status)
{
     audio_output_flags_t flags = static_cast<audio_output_flags_t> (*status);

     ALOGD("AudioHardware::openOutputStream devices %x format %d channels %d samplerate %d flags %d",
        devices, *format, *channels, *sampleRate, flags);

    { // scope for the lock
        status_t lStatus;

        Mutex::Autolock lock(mLock);
        {
            // create new output stream
            AudioStreamOutMSM8x60* out = new AudioStreamOutMSM8x60();
            lStatus = out->set(this, devices, format, channels, sampleRate);
            if (status) {
                *status = lStatus;
            }
            if (lStatus == NO_ERROR) {
                mOutput = out;
            } else {
                delete out;
            }
            return mOutput;
        }
    }
    return NULL;
}


void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    ALOGD("closeOutputStream called");

    Mutex::Autolock lock(mLock);
    if (mOutput == 0 || mOutput != out) {
        ALOGW("Attempt to close invalid output stream");
    } else {
        delete mOutput;
        mOutput = 0;
    }
}

AudioStreamIn* AudioHardware::openInputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    ALOGD("AudioHardware::openInputStream devices %x format %d channels %d samplerate %d in_p=%x lin_p=%x in_v=%x lin_v=%x",
        devices, *format, *channels, *sampleRate, AUDIO_DEVICE_IN_VOICE_CALL, AudioSystem::DEVICE_IN_VOICE_CALL, AUDIO_DEVICE_IN_COMMUNICATION, AudioSystem::DEVICE_IN_COMMUNICATION);

    // check for valid input source
    if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
        return 0;
    }

    mLock.lock();
    {
       if ( (mMode == AudioSystem::MODE_IN_CALL) &&
            (getInputSampleRate(*sampleRate) > AUDIO_HW_IN_SAMPLERATE) &&
            (*format == AUDIO_HW_IN_FORMAT) )
        {
              ALOGE("PCM recording, in a voice call, with sample rate more than 8K not supported \
                   re-configure with 8K and try software re-sampler ");
              *status = -EINVAL;
              *sampleRate = AUDIO_HW_IN_SAMPLERATE;
              mLock.unlock();
              return 0;
        }
        AudioStreamInMSM8x60* in8x60 = new AudioStreamInMSM8x60();
        status_t lStatus = in8x60->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            ALOGE("Error creating Audio stream AudioStreamInMSM8x60 \n");
            mLock.unlock();
            delete in8x60;
            return 0;
        }
        mInputs.add(in8x60);
        mLock.unlock();
        return in8x60;
    }
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    Mutex::Autolock lock(mLock);

    ssize_t index = -1;
    if((index = mInputs.indexOf((AudioStreamInMSM8x60 *)in)) >= 0) {
        ALOGV("closeInputStream AudioStreamInMSM8x60");
        mLock.unlock();
        delete mInputs[index];
        mLock.lock();
        mInputs.removeAt(index);
    }
    else {
        ALOGE("Attempt to close invalid input stream");
    }
}

status_t AudioHardware::setMode(int mode)
{
    status_t status = AudioHardwareBase::setMode(mode);
    if (status == NO_ERROR) {
        // make sure that doAudioRouteOrMute() is called by doRouting()
        // even if the new device selected is the same as current one.
        clearCurDevice();
    }
    return status;
}

status_t AudioHardware::setMasterMute(bool muted) {
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

int AudioHardware::createAudioPatch(unsigned int num_sources,
        const struct audio_port_config *sources,
        unsigned int num_sinks,
        const struct audio_port_config *sinks,
        audio_patch_handle_t *handle) {
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

int AudioHardware::releaseAudioPatch(audio_patch_handle_t handle) {
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

int AudioHardware::getAudioPort(struct audio_port *port) {
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

int AudioHardware::setAudioPortConfig(
        const struct audio_port_config *config) {
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

bool AudioHardware::checkOutputStandby()
{
    if (mOutput)
        if (!mOutput->checkStandby())
            return false;

    return true;
}

status_t AudioHardware::setMicMute(bool state)
{
    Mutex::Autolock lock(mLock);
    return setMicMute_nosync(state);
}

// always call with mutex held
status_t AudioHardware::setMicMute_nosync(bool state)
{
    int session_id = 0;
    if (mMicMute != state) {
        mMicMute = state;
        ALOGD("setMicMute_nosync calling voice mute with the mMicMute %d", mMicMute);
        if(isStreamOnAndActive(VOICE_CALL)) {
             session_id = voice_session_id;
             voice_session_mute = mMicMute;
        } else {
            ALOGE(" unknown voice stream");
            return -1;
        }
#ifdef LEGACY_QCOM_VOICE
        msm_set_voice_tx_mute(mMicMute);
#else
        msm_set_voice_tx_mute_ext(mMicMute,session_id);
#endif
    }
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    int session_id = 0;
    if(isStreamOnAndActive(VOICE_CALL)) {
          session_id = voice_session_id;
          *state = mMicMute = voice_session_mute;
    } else
         *state = mMicMute;
    return NO_ERROR;
}

#ifdef QCOM_FM_ENABLED
void AudioHardware::handleFm(int device)
{
    int sndDevice = -1;

    if ((device & AUDIO_DEVICE_OUT_FM) && (mFmFd == -1)){
        enableFM(sndDevice);
    }
    if ((mFmFd != -1) && !(device & AUDIO_DEVICE_OUT_FM)){
        disableFM();
    }

    if ((CurrentComboDeviceData.DeviceId == INVALID_DEVICE) &&
        (sndDevice == SND_DEVICE_FM_TX_AND_SPEAKER )){
        /* speaker rx is already enabled change snd device to the fm tx
         * device and let the flow take the regular route to
         * updatedeviceinfo().
         */
        Mutex::Autolock lock_1(mComboDeviceLock);

        CurrentComboDeviceData.DeviceId = SND_DEVICE_FM_TX_AND_SPEAKER;
        sndDevice = DEVICE_FMRADIO_STEREO_RX;
    }
    else
    if(CurrentComboDeviceData.DeviceId != INVALID_DEVICE){
        /* time to disable the combo device */
        enableComboDevice(CurrentComboDeviceData.DeviceId,0);
        Mutex::Autolock lock_2(mComboDeviceLock);
        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
    }

    if (sndDevice != -1 && sndDevice != mCurSndDevice) {
        doAudioRouteOrMute(sndDevice);
        mCurSndDevice = sndDevice;
    }
}
#endif

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    int rc = 0;
    String8 value;
    String8 key;
    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NAME_KEY[] = "bt_headset_name";
    const char BT_NREC_VALUE_ON[] = "on";
#ifdef QCOM_FM_ENABLED
    const char FM_NAME_KEY[] = "FMRadioOn";
    const char FM_VALUE_HANDSET[] = "handset";
    const char FM_VALUE_SPEAKER[] = "speaker";
    const char FM_VALUE_HEADSET[] = "headset";
    const char FM_VALUE_FALSE[] = "false";
    float fm_volume;
    int fm_device;
#endif
#ifdef HTC_ACOUSTIC_AUDIO
    const char ACTIVE_AP[] = "active_ap";
    const char EFFECT_ENABLED[] = "sound_effect_enable";
#endif

    ALOGV("setParameters() %s", keyValuePairs.string());

    if (keyValuePairs.length() == 0) return BAD_VALUE;

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            ALOGI("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothVGS = true;
        } else {
            mBluetoothVGS = false;
        }
    }
    key = String8(BT_NAME_KEY);
    if (param.get(key, value) == NO_ERROR) {
#ifdef HTC_ACOUSTIC_AUDIO
        mBluetoothIdTx = 0;
        mBluetoothIdRx = 0;
        for (int i = 0; i < mNumBTEndpoints; i++) {
            if (!strcasecmp(value.string(), mBTEndpoints[i].name)) {
                mBluetoothIdTx = mBTEndpoints[i].tx;
                mBluetoothIdRx = mBTEndpoints[i].rx;
                ALOGD("Using custom acoustic parameters for %s", value.string());
                break;
            }
        }
        if (mBluetoothIdTx == 0) {
            ALOGD("Using default acoustic parameters "
                 "(%s not in acoustic database)", value.string());
        }
#endif
        doRouting(NULL, 0);
    }

    key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            dualmic_enabled = true;
            ALOGI("DualMic feature Enabled");
        } else {
            dualmic_enabled = false;
            ALOGI("DualMic feature Disabled");
        }
        doRouting(NULL, 0);
    }

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "full" || value == "tty_full") {
            mTtyMode = TTY_FULL;
        } else if (value == "hco" || value == "tty_hco") {
            mTtyMode = TTY_HCO;
        } else if (value == "vco" || value == "tty_vco") {
            mTtyMode = TTY_VCO;
        } else {
            mTtyMode = TTY_OFF;
        }
        if(mMode != AUDIO_MODE_IN_CALL){
           return NO_ERROR;
        }
        ALOGI("Changed TTY Mode=%s", value.string());
        if((mMode == AUDIO_MODE_IN_CALL) &&
           (cur_rx == DEVICE_HEADSET_RX) &&
           (cur_tx == DEVICE_HEADSET_TX))
           doRouting(NULL, 0);
    }
#ifdef HTC_ACOUSTIC_AUDIO
    key = String8(ACTIVE_AP);
    if (param.get(key, value) == NO_ERROR) {
        const char* active_ap = value.string();
        ALOGD("Active AP = %s", active_ap);
        strcpy(mActiveAP, active_ap);

        const char* dsp_effect = "\0";
        key = String8(DSP_EFFECT_KEY);
        if (param.get(key, value) == NO_ERROR) {
            ALOGD("DSP Effect = %s", value.string());
            dsp_effect = value.string();
            strcpy(mEffect, dsp_effect);
        }

        key = String8(EFFECT_ENABLED);
        if (param.get(key, value) == NO_ERROR) {
            const char* sound_effect_enable = value.string();
            ALOGD("Sound Effect Enabled = %s", sound_effect_enable);
            if (value == "on") {
                mEffectEnabled = true;
                if (support_aic3254)
                    aic3254_config(get_snd_dev());
            } else {
                strcpy(mEffect, "\0");
                mEffectEnabled = false;
            }
        }
    }
#endif

#ifdef QCOM_FM_ENABLED
    key = String8(AUDIO_PARAMETER_KEY_FM_VOLUME);

    if (param.getFloat(key, fm_volume) == NO_ERROR) {
        if (fm_volume < 0.0) {
            ALOGW("set Fm Volume(%f) under 0.0, assuming 0.0\n", fm_volume);
            fm_volume = 0.0;
        } else if (fm_volume > 1.0) {
            ALOGW("set Fm Volume(%f) over 1.0, assuming 1.0\n", fm_volume);
            fm_volume = 1.0;
        }
        fm_volume = lrint((fm_volume * 0x2000) + 0.5);

        ALOGV("set Fm Volume(%f)\n", fm_volume);
        ALOGV("Setting FM volume to %d (available range is 0 to 0x2000)\n", fm_volume);

        Routing_table* temp = NULL;
        temp = getNodeByStreamType(FM_RADIO);
        if(temp == NULL){
            ALOGV("No Active FM stream is running");
            return NO_ERROR;
        }
        if(msm_set_volume(temp->dec_id, fm_volume)) {
            ALOGE("msm_set_volume(%d) failed for FM errno = %d", fm_volume, errno);
            return -1;
        }
        ALOGV("msm_set_volume(%d) for FM succeeded", fm_volume);

        param.remove(key);
    }

    key = String8("connect");

    if(param.getInt(key, fm_device) == NO_ERROR) {
        if((fm_device & AUDIO_DEVICE_OUT_FM)) {
            if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
               ALOGE("setParameters() enableDevice 1 failed for device %d", DEVICE_FMRADIO_STEREO_TX);
            }
        }
    }

    key = String8("disconnect");

    if(param.getInt(key, fm_device) == NO_ERROR) {
        if((fm_device & AUDIO_DEVICE_OUT_FM)) {
            if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
               ALOGE("setParameters() enableDevice 0 failed for device %d", DEVICE_FMRADIO_STEREO_TX);
            }
        }
    }


#endif /*QCOM_FM_ENABLED*/

    return NO_ERROR;

}

String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;

    String8 key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        value = String8(dualmic_enabled ? "true" : "false");
        param.add(key, value);
    }
#ifdef QCOM_FM_ENABLED
    key = String8("Fm-radio");
    if ( param.get(key,value) == NO_ERROR ) {
        if ( getNodeByStreamType(FM_RADIO) ) {
            param.addInt(String8("isFMON"), true );
        }
    }
#endif
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if(mBluetoothVGS)
           param.addInt(String8("isVGS"), true);
    }
    key = String8(ECHO_SUPRESSION);
    if (param.get(key, value) == NO_ERROR) {
        value = String8("yes");
        param.add(key, value);
    }

#ifdef HTC_ACOUSTIC_AUDIO
    key = String8(DSP_EFFECT_KEY);
    if (param.get(key, value) == NO_ERROR) {
        value = String8(mCurDspProfile);
        param.add(key, value);
    }
#endif
    ALOGV("AudioHardware::getParameters() %s", param.toString().string());
    return param.toString();
}


static unsigned calculate_audpre_table_index(unsigned index)
{
    switch (index) {
        case 48000:    return SAMP_RATE_INDX_48000;
        case 44100:    return SAMP_RATE_INDX_44100;
        case 32000:    return SAMP_RATE_INDX_32000;
        case 24000:    return SAMP_RATE_INDX_24000;
        case 22050:    return SAMP_RATE_INDX_22050;
        case 16000:    return SAMP_RATE_INDX_16000;
        case 12000:    return SAMP_RATE_INDX_12000;
        case 11025:    return SAMP_RATE_INDX_11025;
        case 8000:    return SAMP_RATE_INDX_8000;
        default:     return -1;
    }
}
size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    ALOGD("AudioHardware::getInputBufferSize sampleRate %d format %d channelCount %d"
            ,sampleRate, format, channelCount);
    if ( (format != AUDIO_FORMAT_PCM_16_BIT) &&
         (format != AUDIO_FORMAT_AMR_NB)     &&
         (format != AUDIO_FORMAT_AMR_WB)     &&
         (format != AUDIO_FORMAT_EVRC)       &&
         (format != AUDIO_FORMAT_EVRCB)      &&
         (format != AUDIO_FORMAT_EVRCWB)     &&
         (format != AUDIO_FORMAT_QCELP)      &&
         (format != AUDIO_FORMAT_AAC)) {
        ALOGW("getInputBufferSize bad format: 0x%x", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        ALOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

    size_t bufferSize = 0;

    if (format == AUDIO_FORMAT_AMR_NB) {
       bufferSize = 320*channelCount;
    } else if (format == AUDIO_FORMAT_EVRC) {
       bufferSize = 230*channelCount;
    } else if (format == AUDIO_FORMAT_QCELP) {
       bufferSize = 350*channelCount;
    } else if (format == AUDIO_FORMAT_AAC) {
       bufferSize = 2048;
    } else if (sampleRate == 8000 || sampleRate == 16000 || sampleRate == 32000) {
       bufferSize = (sampleRate * channelCount * 20 * sizeof(int16_t)) / 1000;
    }
    else if (sampleRate == 11025 || sampleRate == 12000) {
       bufferSize = 256 * sizeof(int16_t) * channelCount;
    }
    else if (sampleRate == 22050 || sampleRate == 24000) {
       bufferSize = 512 * sizeof(int16_t) * channelCount;
    }
    else if (sampleRate == 44100 || sampleRate == 48000) {
       bufferSize = 1024 * sizeof(int16_t) * channelCount;
    }

    ALOGD("getInputBufferSize: sampleRate: %d channelCount: %d bufferSize: %d", sampleRate, channelCount, bufferSize);

    return bufferSize;
}

static status_t set_volume_rpc(uint32_t device,
                               uint32_t method,
                               uint32_t volume)
{
    ALOGV("set_volume_rpc(%d, %d, %d)\n", device, method, volume);

    if (device == -1UL) return NO_ERROR;
     return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    int session_id = 0;
    if (v < 0.0) {
        ALOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

#ifdef HTC_ACOUSTIC_AUDIO
    mVoiceVolume = v;
#endif

    if(isStreamOnAndActive(VOICE_CALL)) {
        session_id = voice_session_id;
    } else {
        ALOGE(" unknown stream ");
        return -1;
    }
    int vol = lrint(v * 100.0);
    // Voice volume levels from android are mapped to driver volume levels as follows.
    // 0 -> 5, 20 -> 4, 40 ->3, 60 -> 2, 80 -> 1, 100 -> 0
    vol = 5 - (vol/20);
    ALOGD("setVoiceVolume(%f)\n", v);
    ALOGI("Setting in-call volume to %d (available range is 5(MIN VOLUME)  to 0(MAX VOLUME)\n", vol);

#ifdef LEGACY_QCOM_VOICE
    if (msm_set_voice_rx_vol(vol)) {
        ALOGE("msm_set_voice_rx_vol(%d) failed errno = %d", vol, errno);
        return -1;
    }
#else
    if(msm_set_voice_rx_vol_ext(vol,session_id)) {
        ALOGE("msm_set_voice_rx_vol(%d) failed errno = %d",vol,errno);
        return -1;
    }
#endif
    ALOGV("msm_set_voice_rx_vol(%d) succeeded session_id %d",vol,session_id);
    return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float v)
{
    Mutex::Autolock lock(mLock);
    int vol = ceil(v * 7.0);
    ALOGI("Set master volume to %d.\n", vol);

    set_volume_rpc(SND_DEVICE_HANDSET, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_SPEAKER, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_BT,      SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_HEADSET, SND_METHOD_VOICE, vol);
    //TBD - does HDMI require this handling

    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

#ifdef HTC_ACOUSTIC_AUDIO
status_t get_batt_temp(int *batt_temp) {
    ALOGD("Enable ALT for speaker");

    int i, fd, len;
    char get_batt_temp[6] = { 0 };
    const char *fn = "/sys/class/power_supply/battery/batt_temp";

    if ((fd = open(fn, O_RDONLY)) < 0) {
        ALOGE("Couldn't open sysfs file batt_temp");
        return UNKNOWN_ERROR;
    }

    if ((len = read(fd, get_batt_temp, sizeof(get_batt_temp))) <= 1) {
        ALOGE("read battery temp fail: %s", strerror(errno));
        close(fd);
        return BAD_VALUE;
    }

    *batt_temp = strtol(get_batt_temp, NULL, 10);
    ALOGD("ALT batt_temp = %d", *batt_temp);

    close(fd);
    return NO_ERROR;
}

status_t do_tpa2051_control(int mode)
{
    int fd, rc;
    int tpa_mode = 0;

    if (mode) {
        if (cur_rx == DEVICE_HEADSET_RX)
            tpa_mode = TPA2051_MODE_VOICECALL_HEADSET;
        else if (cur_rx == DEVICE_SPEAKER_RX)
            tpa_mode = TPA2051_MODE_VOICECALL_SPKR;
    } else {
        switch (cur_rx) {
            case DEVICE_FMRADIO_HEADSET_RX:
                tpa_mode = TPA2051_MODE_FM_HEADSET;
                break;
            case DEVICE_FMRADIO_SPEAKER_RX:
                tpa_mode = TPA2051_MODE_FM_SPKR;
                break;
            case DEVICE_SPEAKER_HEADSET_RX:
                tpa_mode = TPA2051_MODE_RING;
                break;
            case DEVICE_HEADSET_RX:
                tpa_mode = TPA2051_MODE_PLAYBACK_HEADSET;
                break;
            case DEVICE_SPEAKER_RX:
                tpa_mode = TPA2051_MODE_PLAYBACK_SPKR;
                break;
            default:
                break;
        }
    }

    fd = open("/dev/tpa2051d3", O_RDWR);
    if (fd < 0) {
        ALOGE("can't open /dev/tpa2051d3 %d", fd);
        return -1;
    }

    if (tpa_mode != cur_tpa_mode) {
        cur_tpa_mode = tpa_mode;
        if (tpa_mode > 0) {
            rc = ioctl(fd, TPA2051_SET_MODE, &tpa_mode);
            if (rc < 0)
                ALOGE("ioctl TPA2051_SET_MODE to mode %d failed: %s", tpa_mode, strerror(errno));
            else
                ALOGD("update TPA2051_SET_MODE to mode %d success", tpa_mode);
        }
    }

    close(fd);
    return 0;
}
#endif

static status_t do_route_audio_rpc(uint32_t device,
                                   int mode, bool mic_mute)
{
    if(device == INVALID_DEVICE)
        return 0;

    int new_rx_device = INVALID_DEVICE,new_tx_device = INVALID_DEVICE,fm_device = INVALID_DEVICE;
    Routing_table* temp = NULL;
    ALOGV("do_route_audio_rpc(%d, %d, %d)", device, mode, mic_mute);

    if(device == SND_DEVICE_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In HANDSET");
    }
    else if(device == SND_DEVICE_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGV("In SPEAKER");
    }
    else if(device == SND_DEVICE_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In HEADSET");
    }
    else if(device == SND_DEVICE_NO_MIC_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In NO MIC HEADSET");
    }
#ifdef QCOM_FM_ENABLED
    else if (device == SND_DEVICE_FM_HANDSET) {
        fm_device = DEVICE_FMRADIO_HANDSET_RX;
        ALOGV("In FM HANDSET");
    }
    else if(device == SND_DEVICE_FM_SPEAKER) {
        fm_device = DEVICE_FMRADIO_SPEAKER_RX;
        ALOGV("In FM SPEAKER");
    }
    else if(device == SND_DEVICE_FM_HEADSET) {
        fm_device = DEVICE_FMRADIO_HEADSET_RX;
        ALOGV("In FM HEADSET");
    }
#endif
#ifdef SAMSUNG_AUDIO
    else if(device == SND_DEVICE_IN_S_SADC_OUT_HANDSET) {
        new_rx_device = DEVICE_HANDSET_CALL_RX;
        new_tx_device = DEVICE_DUALMIC_HANDSET_TX;
        ALOGV("In DUALMIC_CALL_HANDSET");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_HANDSET_CALL_TX;
            ALOGV("Falling back to HANDSET_CALL_RX AND HANDSET_CALL_TX as no DUALMIC_HANDSET_TX support found");
        }
    }
#else
    else if(device == SND_DEVICE_IN_S_SADC_OUT_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_DUALMIC_HANDSET_TX;
        ALOGV("In DUALMIC_HANDSET");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_HANDSET_TX;
            ALOGV("Falling back to HANDSET_RX AND HANDSET_TX as no DUALMIC_HANDSET_TX support found");
        }
    }
#endif
    else if(device == SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_DUALMIC_SPEAKER_TX;
        ALOGV("In DUALMIC_SPEAKER");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_SPEAKER_TX;
            ALOGV("Falling back to SPEAKER_RX AND SPEAKER_TX as no DUALMIC_SPEAKER_TX support found");
        }
    }
    else if(device == SND_DEVICE_TTY_FULL) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_FULL");
    }
    else if(device == SND_DEVICE_TTY_VCO) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In TTY_VCO");
    }
    else if(device == SND_DEVICE_TTY_HCO) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_HCO");
    }
#ifdef HTC_ACOUSTIC_AUDIO
    else if((device == SND_DEVICE_BT) ||
            (device == SND_DEVICE_BT_EC_OFF)) {
#else
    else if(device == SND_DEVICE_BT) {
#endif
        new_rx_device = DEVICE_BT_SCO_RX;
        new_tx_device = DEVICE_BT_SCO_TX;
        ALOGV("In BT_HCO");
    }
    else if(device == SND_DEVICE_HEADSET_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HEADSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HEADSET_TX as no combo device support found");
        }
    }
    else if(device == SND_DEVICE_HEADPHONE_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HANDSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HANDSET_TX as no combo device support found");
        }
    }
    else if (device == SND_DEVICE_HDMI) {
        new_rx_device = DEVICE_HDMI_STERO_RX;
        new_tx_device = cur_tx;
        ALOGI("In DEVICE_HDMI_STERO_RX and cur_tx");
    }
#ifdef QCOM_FM_ENABLED
    else if(device == SND_DEVICE_FM_TX){
        new_rx_device = DEVICE_FMRADIO_STEREO_RX;
        ALOGI("In DEVICE_FMRADIO_STEREO_RX and cur_tx");
    }
#endif
    else if(device == SND_DEVICE_SPEAKER_TX) {
        new_rx_device = cur_rx;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGI("In SPEAKER_TX cur_rx = %d\n", cur_rx);
    }
#ifdef SAMSUNG_AUDIO
    else if (device == SND_DEVICE_VOIP_HANDSET) {
        new_rx_device = DEVICE_HANDSET_VOIP_RX;
        new_tx_device = DEVICE_HANDSET_VOIP_TX;
        ALOGD("In VOIP HANDSET");
    }
    else if (device == SND_DEVICE_VOIP_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_VOIP_RX;
        new_tx_device = DEVICE_SPEAKER_VOIP_TX;
        ALOGD("In VOIP SPEAKER");
    }
    else if (device == SND_DEVICE_VOIP_HEADSET) {
        new_rx_device = DEVICE_HEADSET_VOIP_RX;
        new_tx_device = DEVICE_HEADSET_VOIP_TX;
        ALOGD("In VOIP HEADSET");
    }
    else if (device == SND_DEVICE_CALL_HANDSET) {
        new_rx_device = DEVICE_HANDSET_CALL_RX;
        new_tx_device = DEVICE_HANDSET_CALL_TX;
        ALOGD("In CALL HANDSET");
    }
    else if (device == SND_DEVICE_CALL_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_CALL_RX;
        new_tx_device = DEVICE_SPEAKER_CALL_TX;
        ALOGD("In CALL SPEAKER");
    }
    else if (device == SND_DEVICE_CALL_HEADSET) {
        new_rx_device = DEVICE_HEADSET_CALL_RX;
        new_tx_device = DEVICE_HEADSET_CALL_TX;
        ALOGD("In CALL HEADSET");
    }
    else if(device == SND_DEVICE_VR_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_SPEAKER_VR_TX;
        ALOGV("In VR SPEAKER");
    }
    else if(device == SND_DEVICE_VR_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_VR_TX;
        ALOGV("In VR HEADSET");
    }
#endif
#ifdef BACK_MIC_CAMCORDER
    else if (device == SND_DEVICE_BACK_MIC_CAMCORDER) {
        new_rx_device = cur_rx;
        new_tx_device = DEVICE_CAMCORDER_TX;
        ALOGV("In BACK_MIC_CAMCORDER");
    }
#endif
    if(new_rx_device != INVALID_DEVICE)
        ALOGD("new_rx = %d", DEV_ID(new_rx_device));
    if(new_tx_device != INVALID_DEVICE)
        ALOGD("new_tx = %d", DEV_ID(new_tx_device));

    if ((mode == AUDIO_MODE_IN_CALL) && !isStreamOn(VOICE_CALL)) {
#ifdef LEGACY_QCOM_VOICE
        msm_start_voice();
#endif
        ALOGV("Going to enable RX/TX device for voice stream");
            // Routing Voice
            if ( (new_rx_device != INVALID_DEVICE) && (new_tx_device != INVALID_DEVICE))
            {
#ifdef QCOM_ACDB_ENABLED
                initACDB();
                acdb_loader_send_voice_cal(ACDB_ID(new_rx_device),ACDB_ID(new_tx_device));
#endif
                ALOGD("Starting voice on Rx %d and Tx %d device", DEV_ID(new_rx_device), DEV_ID(new_tx_device));
                msm_route_voice(DEV_ID(new_rx_device),DEV_ID(new_tx_device), 1);
            }
            else
            {
                return -1;
            }

            if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,0) == -1))
                    return -1;

            if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,0) == -1))
                    return -1;

           //Enable RX device
           if(new_rx_device !=INVALID_DEVICE && (enableDevice(new_rx_device,1) == -1))
               return -1;
            //Enable TX device
           if(new_tx_device !=INVALID_DEVICE && (enableDevice(new_tx_device,1) == -1))
               return -1;
#ifdef LEGACY_QCOM_VOICE
           msm_set_voice_tx_mute(0);
#else
           voice_session_id = msm_get_voc_session(VOICE_SESSION_NAME);
           if(voice_session_id <=0) {
                ALOGE("voice session invalid");
                return 0;
           }
           msm_start_voice_ext(voice_session_id);
           msm_set_voice_tx_mute_ext(voice_session_mute,voice_session_id);
#endif

           if(!isDeviceListEmpty())
               updateDeviceInfo(new_rx_device,new_tx_device);
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
            addToTable(0,cur_rx,cur_tx,VOICE_CALL,true);
    }
    else if ((mode == AUDIO_MODE_NORMAL) && isStreamOnAndActive(VOICE_CALL)) {
        ALOGV("Going to disable RX/TX device during end of voice call");
        temp = getNodeByStreamType(VOICE_CALL);
        if(temp == NULL)
            return 0;

        // Ending voice call
        ALOGD("Ending Voice call");
#ifdef LEGACY_QCOM_VOICE
        msm_end_voice();
#else
        msm_end_voice_ext(voice_session_id);
        voice_session_id = 0;
        voice_session_mute = 0;
#endif

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)) {
           enableDevice(temp->dev_id,0);
           enableDevice(temp->dev_id_tx,0);
        }
        deleteFromTable(VOICE_CALL);
        updateDeviceInfo(new_rx_device,new_tx_device);
        if(new_rx_device != INVALID_DEVICE && new_tx_device != INVALID_DEVICE) {
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
        }
    }
    else {
        ALOGD("updateDeviceInfo() called for default case");
        updateDeviceInfo(new_rx_device,new_tx_device);
    }
#ifdef HTC_ACOUSTIC_AUDIO
    if (support_tpa2051)
        do_tpa2051_control(mode ^1);
#endif
    return NO_ERROR;
}

// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
// BT acoustics is not supported. This might be used by OEMs. Hence commenting
// the code and not removing it.
#if 0
    if (device == (uint32_t)SND_DEVICE_BT || device == (uint32_t)SND_DEVICE_CARKIT) {
        if (mBluetoothId) {
            device = mBluetoothId;
        } else if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }
#endif

#ifdef HTC_ACOUSTIC_AUDIO
    if (device == SND_DEVICE_BT) {
        if (!mBluetoothNrec)
            device = SND_DEVICE_BT_EC_OFF;
    }

    if (support_aic3254) {
        aic3254_config(device);
        do_aic3254_control(device);
    }

    getACDB(device);
#endif

    if (isStreamOnAndActive(VOICE_CALL) && mMicMute == false)
        msm_set_voice_tx_mute(0);

#ifdef HTC_ACOUSTIC_AUDIO
    if (isInCall())
        setVoiceVolume(mVoiceVolume);
#endif
    ALOGV("doAudioRouteOrMute() device %x, mMode %d, mMicMute %d", device, mMode, mMicMute);
    return do_route_audio_rpc(device, mMode, mMicMute);
}

#ifdef HTC_ACOUSTIC_AUDIO
status_t AudioHardware::set_mRecordState(bool onoff) {
    mRecordState = onoff;
    return 0;
}

status_t AudioHardware::get_mRecordState(void) {
    return mRecordState;
}

status_t AudioHardware::get_snd_dev(void) {
    return mCurSndDevice;
}

void AudioHardware::getACDB(uint32_t device) {
    rx_htc_acdb = 0;
    tx_htc_acdb = 0;

    if (device == SND_DEVICE_BT) {
        if (mBluetoothIdTx != 0) {
            rx_htc_acdb = mBluetoothIdRx;
            tx_htc_acdb = mBluetoothIdTx;
        } else {
            /* use default BT entry defined in AudioBTID.csv */
            rx_htc_acdb = mBTEndpoints[0].rx;
            tx_htc_acdb = mBTEndpoints[0].tx;
        }
    } else if (device == SND_DEVICE_CARKIT ||
               device == SND_DEVICE_BT_EC_OFF) {
        if (mBluetoothIdTx != 0) {
            rx_htc_acdb = mBluetoothIdRx;
            tx_htc_acdb = mBluetoothIdTx;
        } else {
            /* use default carkit entry defined in AudioBTID.csv */
            rx_htc_acdb = mBTEndpoints[1].rx;
            tx_htc_acdb = mBTEndpoints[1].tx;
        }
    }

    ALOGV("getACDB: device = %d, HTC RX ACDB ID = %d, HTC TX ACDB ID = %d",
         device, rx_htc_acdb, tx_htc_acdb);
}

status_t AudioHardware::do_aic3254_control(uint32_t device) {
    ALOGD("do_aic3254_control device: %d mode: %d record: %d", device, mMode, mRecordState);

    uint32_t new_aic_txmode = UPLINK_OFF;
    uint32_t new_aic_rxmode = DOWNLINK_OFF;

    Mutex::Autolock lock(mAIC3254ConfigLock);

    if (isInCall()) {
        switch (device) {
            case SND_DEVICE_HEADSET:
                new_aic_rxmode = CALL_DOWNLINK_EMIC_HEADSET;
                new_aic_txmode = CALL_UPLINK_EMIC_HEADSET;
                break;
            case SND_DEVICE_SPEAKER:
            case SND_DEVICE_SPEAKER_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_SPEAKER;
                new_aic_txmode = CALL_UPLINK_IMIC_SPEAKER;
                break;
            case SND_DEVICE_HEADSET_AND_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                new_aic_rxmode = RING_HEADSET_SPEAKER;
                break;
            case SND_DEVICE_NO_MIC_HEADSET:
            case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_HEADSET;
                new_aic_txmode = CALL_UPLINK_IMIC_HEADSET;
                break;
            case SND_DEVICE_HANDSET:
            case SND_DEVICE_HANDSET_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_RECEIVER;
                new_aic_txmode = CALL_UPLINK_IMIC_RECEIVER;
                break;
            default:
                break;
        }
    } else {
        if (checkOutputStandby()) {
            if (device == SND_DEVICE_FM_HEADSET) {
                new_aic_rxmode = FM_OUT_HEADSET;
                new_aic_txmode = FM_IN_HEADSET;
            } else if (device == SND_DEVICE_FM_SPEAKER) {
                new_aic_rxmode = FM_OUT_SPEAKER;
                new_aic_txmode = FM_IN_SPEAKER;
            }
        } else {
            switch (device) {
                case SND_DEVICE_HEADSET_AND_SPEAKER:
                case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                case SND_DEVICE_HEADPHONE_AND_SPEAKER:
                    new_aic_rxmode = RING_HEADSET_SPEAKER;
                    break;
                case SND_DEVICE_SPEAKER:
                case SND_DEVICE_SPEAKER_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_SPEAKER;
                    break;
                case SND_DEVICE_HANDSET:
                case SND_DEVICE_HANDSET_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_RECEIVER;
                    break;
                case SND_DEVICE_HEADSET:
                case SND_DEVICE_NO_MIC_HEADSET:
                case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_HEADSET;
                    break;
                default:
                    break;
            }
        }

        if (mRecordState) {
            switch (device) {
                case SND_DEVICE_HEADSET:
                    new_aic_txmode = VOICERECORD_EMIC;
                    break;
                case SND_DEVICE_HANDSET_BACK_MIC:
                case SND_DEVICE_SPEAKER_BACK_MIC:
                case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                    new_aic_txmode = VIDEORECORD_IMIC;
                    break;
                case SND_DEVICE_HANDSET:
                case SND_DEVICE_SPEAKER:
                case SND_DEVICE_NO_MIC_HEADSET:
                case SND_DEVICE_HEADSET_AND_SPEAKER:
                    new_aic_txmode = VOICERECORD_IMIC;
                    break;
                default:
                    break;
            }
        }
    }

    if (new_aic_rxmode != cur_aic_rx)
        ALOGD("do_aic3254_control: set aic3254 rx to %d", new_aic_rxmode);
    if (aic3254_ioctl(AIC3254_CONFIG_RX, new_aic_rxmode) >= 0)
        cur_aic_rx = new_aic_rxmode;

    if (new_aic_txmode != cur_aic_tx)
        ALOGD("do_aic3254_control: set aic3254 tx to %d", new_aic_txmode);
    if (aic3254_ioctl(AIC3254_CONFIG_TX, new_aic_txmode) >= 0)
        cur_aic_tx = new_aic_txmode;

    if (cur_aic_tx == UPLINK_OFF && cur_aic_rx == DOWNLINK_OFF && aic3254_enabled) {
        strcpy(mCurDspProfile, "\0");
        aic3254_enabled = false;
        aic3254_powerdown();
    } else if (cur_aic_tx != UPLINK_OFF || cur_aic_rx != DOWNLINK_OFF)
        aic3254_enabled = true;

    return NO_ERROR;
}

bool AudioHardware::isAic3254Device(uint32_t device) {
    switch(device) {
        case SND_DEVICE_HANDSET:
        case SND_DEVICE_SPEAKER:
        case SND_DEVICE_HEADSET:
        case SND_DEVICE_NO_MIC_HEADSET:
        case SND_DEVICE_FM_HEADSET:
        case SND_DEVICE_HEADSET_AND_SPEAKER:
        case SND_DEVICE_FM_SPEAKER:
        case SND_DEVICE_HEADPHONE_AND_SPEAKER:
        case SND_DEVICE_HANDSET_BACK_MIC:
        case SND_DEVICE_SPEAKER_BACK_MIC:
        case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
        case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
            return true;
            break;
        default:
            return false;
            break;
    }
}

status_t AudioHardware::aic3254_config(uint32_t device) {
    ALOGD("aic3254_config: device %d enabled %d", device, aic3254_enabled);
    char name[22] = "\0";
    char aap[9] = "\0";

    if ((!isAic3254Device(device) ||
         !aic3254_enabled) &&
        strlen(mCurDspProfile) != 0)
        return NO_ERROR;

    Mutex::Autolock lock(mAIC3254ConfigLock);

    if (mMode == AUDIO_MODE_IN_CALL) {
        strcpy(name, "Phone_Default");
        switch (device) {
            case SND_DEVICE_HANDSET:
            case SND_DEVICE_HANDSET_BACK_MIC:
                strcpy(name, "Phone_Handset_Dualmic");
                break;
            case SND_DEVICE_HEADSET:
            case SND_DEVICE_HEADSET_AND_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
            case SND_DEVICE_NO_MIC_HEADSET:
                strcpy(name, "Phone_Headset");
                break;
            case SND_DEVICE_SPEAKER:
                strcpy(name, "Phone_Speaker_Dualmic");
                break;
            default:
                break;
        }
    } else {
        if ((strcasecmp(mActiveAP, "Camcorder") == 0)) {
            if (strlen(mEffect) != 0) {
                strcpy(name, "Recording_");
                strcat(name, mEffect);
            } else
                strcpy(name, "Playback_Default");
        } else if (mRecordState) {
            strcpy(name, "Record_Default");
        } else if (strlen(mEffect) == 0 && !mEffectEnabled)
            strcpy(name, "Playback_Default");
        else {
            if (mEffectEnabled)
                strcpy(name, mEffect);

            if ((strcasecmp(name, "Srs") == 0) ||
                (strcasecmp(name, "Dolby") == 0)) {
                strcpy(mEffect, name);
                if (strcasecmp(mActiveAP, "Music") == 0)
                    strcat(name, "_a");
                else if (strcasecmp(mActiveAP, "Video") == 0)
                    strcat(name, "_v");
                if (device == SND_DEVICE_SPEAKER)
                    strcat(name, "_spk");
                else
                    strcat(name, "_hp");
            }
        }
    }

    if (strcasecmp(mCurDspProfile, name)) {
        ALOGD("aic3254_config: loading effect %s", name);
        strcpy(mCurDspProfile, name);
    } else {
        ALOGD("aic3254_config: effect %s already loaded", name);
        return NO_ERROR;
    }

    int rc = set_sound_effect(name);
    if (rc < 0) {
        ALOGE("Could not set sound effect %s: %d", name, rc);
        return rc;
    }

    return NO_ERROR;
}

int AudioHardware::aic3254_ioctl(int cmd, const int argc) {
    int rc = -1;
    int (*set_aic3254_ioctl)(int, const int*);

    set_aic3254_ioctl = (int (*)(int, const int*))::dlsym(acoustic, "set_aic3254_ioctl");
    if ((*set_aic3254_ioctl) == 0) {
        ALOGE("Could not open set_aic3254_ioctl()");
        return rc;
    }

    rc = set_aic3254_ioctl(cmd, &argc);
    if (rc < 0)
        ALOGE("aic3254_ioctl failed");

    return rc;
}

void AudioHardware::aic3254_powerdown() {
    int rc = aic3254_ioctl(AIC3254_POWERDOWN, 0);
    if (rc < 0)
        ALOGE("aic3254_powerdown failed");
    else
        ALOGI("aic3254 powered off");
}
#endif

status_t AudioHardware::doRouting(AudioStreamInMSM8x60 *input, uint32_t outputDevices)
{
    Mutex::Autolock lock(mLock);
    status_t ret = NO_ERROR;
    int audProcess = (ADRC_DISABLE | EQ_DISABLE | RX_IIR_DISABLE);
    int sndDevice = -1;

    if (!outputDevices)
        outputDevices = mOutput->devices();

    ALOGD("outputDevices = %x", outputDevices);

    if (input != NULL) {
        uint32_t inputDevice = input->devices();
        ALOGI("do input routing device %x\n", inputDevice);
        // ignore routing device information when we start a recording in voice
        // call
        // Recording will happen through currently active tx device
        if((inputDevice == AUDIO_DEVICE_IN_VOICE_CALL)
#ifdef QCOM_FM_ENABLED
           || (inputDevice == AUDIO_DEVICE_IN_FM_RX)
           || (inputDevice == AUDIO_DEVICE_IN_FM_RX_A2DP)
#endif
           || (inputDevice == AUDIO_DEVICE_IN_COMMUNICATION)
        ) {
            ALOGV("Ignoring routing for FM/INCALL/VOIP recording");
            return NO_ERROR;
        }
        if (inputDevice & AUDIO_DEVICE_BIT_IN) {
            if (inputDevice & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                ALOGI("Routing audio to Bluetooth PCM\n");
                sndDevice = SND_DEVICE_BT;
#ifdef BACK_MIC_CAMCORDER
            } else if (inputDevice & AUDIO_DEVICE_IN_BACK_MIC) {
                ALOGI("Routing audio to back mic (camcorder)");
                sndDevice = SND_DEVICE_BACK_MIC_CAMCORDER;
#endif
            } else if (inputDevice & AUDIO_DEVICE_IN_WIRED_HEADSET) {
                if ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                    (outputDevices & AUDIO_DEVICE_OUT_SPEAKER)) {
                    ALOGI("Routing audio to Wired Headset and Speaker\n");
                    sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
                    audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
                } else {
                    ALOGI("Routing audio to Wired Headset\n");
                    sndDevice = SND_DEVICE_HEADSET;
                }
            } else {
                ALOGI("Routing audio to Speaker (default)\n");
                sndDevice = SND_DEVICE_SPEAKER;
            }
#ifdef SAMSUNG_AUDIO
            if (input->isForVR()) {
                if (sndDevice == SND_DEVICE_SPEAKER)
                    sndDevice = SND_DEVICE_VR_SPEAKER;
                else if (sndDevice == SND_DEVICE_HEADSET)
                    sndDevice = SND_DEVICE_VR_HEADSET;
            }
#endif
        }
        // if inputDevice == 0, restore output routing
    }

    if (sndDevice == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AUDIO_DEVICE_OUT_SPEAKER) == 0) {
                ALOGW("Hardware does not support requested route combination (%#X),"
                     " picking closest possible route...", outputDevices);
            }
        }
        if ((mTtyMode != TTY_OFF) && (mMode == AUDIO_MODE_IN_CALL) &&
                ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET)
            )) {
            if (mTtyMode == TTY_FULL) {
                ALOGI("Routing audio to TTY FULL Mode\n");
                sndDevice = SND_DEVICE_TTY_FULL;
            } else if (mTtyMode == TTY_VCO) {
                ALOGI("Routing audio to TTY VCO Mode\n");
                sndDevice = SND_DEVICE_TTY_VCO;
            } else if (mTtyMode == TTY_HCO) {
                ALOGI("Routing audio to TTY HCO Mode\n");
                sndDevice = SND_DEVICE_TTY_HCO;
            }
        } else if (outputDevices &
                   (AUDIO_DEVICE_OUT_BLUETOOTH_SCO | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_BT;
        } else if (outputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_CARKIT;
        } else if (outputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            ALOGI("Routing audio to HDMI\n");
            sndDevice = SND_DEVICE_HDMI;
        } else if ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AUDIO_DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to Wired Headset and Speaker\n");
            sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else 
          if (outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
            if (outputDevices & AUDIO_DEVICE_OUT_SPEAKER) {
                ALOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_HEADPHONE_AND_SPEAKER;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            } else {
                ALOGI("Routing audio to No microphone Wired Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_HEADSET;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            }
        }
         else if (outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
             ALOGI("Routing audio to Wired Headset\n");
             sndDevice = SND_DEVICE_HEADSET;
             audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
          else if (outputDevices & AUDIO_DEVICE_OUT_SPEAKER) {
#ifdef SAMSUNG_AUDIO
            if (mMode == AUDIO_MODE_IN_CALL) {
              ALOGD("Routing audio to Call Speaker\n");
              sndDevice = SND_DEVICE_CALL_SPEAKER;
            } else {
#endif
            ALOGI("Routing audio to Speakerphone (out_speaker case)\n");
            sndDevice = SND_DEVICE_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
#ifdef SAMSUNG_AUDIO
            }
#endif
        } else
          if(outputDevices & AUDIO_DEVICE_OUT_EARPIECE){
#ifdef SAMSUNG_AUDIO
            if (mMode == AUDIO_MODE_IN_CALL) {
                if (dualmic_enabled) {
                    ALOGI("Routing audio to Handset with DualMike enabled\n");
                    sndDevice = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
                } else {
                    ALOGD("Routing audio to Call Handset\n");
                    sndDevice = SND_DEVICE_CALL_HANDSET;
                }
            } else {
#endif
            ALOGI("Routing audio to Handset\n");
            sndDevice = SND_DEVICE_HANDSET;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
#ifdef SAMSUNG_AUDIO
            }
#endif
        }
    }

#ifndef SAMSUNG_AUDIO
    if (dualmic_enabled) {
        if (sndDevice == SND_DEVICE_HANDSET) {
            ALOGI("Routing audio to Handset with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
        } else if (sndDevice == SND_DEVICE_SPEAKER) {
            ALOGI("Routing audio to Speakerphone with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE;
        }
    }
#endif

#ifdef SAMSUNG_AUDIO
    if ((mMode == AUDIO_MODE_IN_CALL) && (sndDevice == SND_DEVICE_HEADSET)) {
            ALOGD("Routing audio to Call Headset\n");
            sndDevice = SND_DEVICE_CALL_HEADSET;
    }
#endif

#ifdef QCOM_FM_ENABLED
    if ((outputDevices & AUDIO_DEVICE_OUT_FM) && (mFmFd == -1)){
        enableFM(sndDevice);
    }
    if ((mFmFd != -1) && !(outputDevices & AUDIO_DEVICE_OUT_FM)){
        disableFM();
    }

    if ((CurrentComboDeviceData.DeviceId == INVALID_DEVICE) &&
        (sndDevice == SND_DEVICE_FM_TX_AND_SPEAKER )){
        /* speaker rx is already enabled change snd device to the fm tx
         * device and let the flow take the regular route to
         * updatedeviceinfo().
         */
        Mutex::Autolock lock_1(mComboDeviceLock);

        CurrentComboDeviceData.DeviceId = SND_DEVICE_FM_TX_AND_SPEAKER;
        sndDevice = DEVICE_FMRADIO_STEREO_RX;
    }
    else
#endif
    if(CurrentComboDeviceData.DeviceId != INVALID_DEVICE){
        /* time to disable the combo device */
        enableComboDevice(CurrentComboDeviceData.DeviceId,0);
        Mutex::Autolock lock_2(mComboDeviceLock);
        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
    }

    if (sndDevice != -1 && sndDevice != mCurSndDevice) {
        ret = doAudioRouteOrMute(sndDevice);
        mCurSndDevice = sndDevice;
    }
    return ret;
}

status_t AudioHardware::enableComboDevice(uint32_t sndDevice, bool enableOrDisable)
{
    ALOGD("enableComboDevice %u",enableOrDisable);
    status_t status = NO_ERROR;
    Routing_table *PcmNode = getNodeByStreamType(PCM_PLAY);

    if(enableDevice(DEVICE_SPEAKER_RX, enableOrDisable)) {
         ALOGE("enableDevice failed for device %d", DEVICE_SPEAKER_RX);
         return -1;
    }
#ifdef QCOM_FM_ENABLED
    if(SND_DEVICE_FM_TX_AND_SPEAKER == sndDevice){

        if(getNodeByStreamType(VOICE_CALL) || getNodeByStreamType(FM_RADIO) ||
           getNodeByStreamType(FM_A2DP)){
            ALOGE("voicecall/FM radio active bailing out");
            return NO_ERROR;
        }

        if(!PcmNode) {
            ALOGE("No active playback session active bailing out ");
            cur_rx = DEVICE_FMRADIO_STEREO_RX;
            return NO_ERROR;
        }

        Mutex::Autolock lock_1(mComboDeviceLock);

        Routing_table* temp = NULL;

        if (enableOrDisable == 1) {
            if(CurrentComboDeviceData.StreamType == INVALID_STREAM){
                if (PcmNode){
                    temp = PcmNode;
                    CurrentComboDeviceData.StreamType = PCM_PLAY;
                    ALOGD("PCM_PLAY session Active ");
                } else {
                    ALOGE("no PLAYback session Active ");
                    return -1;
                }
            }else
                temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);

            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:msm_route_stream(%d,%d,1)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                1)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }

        }else if(enableOrDisable == 0) {
            temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);


            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:de-route msm_route_stream(%d,%d,0)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX), 0)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }
        }

    }
#endif

    return status;
}
#ifdef QCOM_FM_ENABLED
status_t AudioHardware::enableFM(int sndDevice)
{
    ALOGD("enableFM");
    status_t status = NO_INIT;
    unsigned short session_id = INVALID_DEVICE;
    status = ::open(FM_DEVICE, O_RDWR);
    if (status < 0) {
           ALOGE("Cannot open FM_DEVICE errno: %d", errno);
           goto Error;
    }
    mFmFd = status;
    if(ioctl(mFmFd, AUDIO_GET_SESSION_ID, &session_id)) {
           ALOGE("AUDIO_GET_SESSION_ID failed*********");
           goto Error;
    }

    if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
           ALOGE("enableDevice failed for device %d", DEVICE_FMRADIO_STEREO_TX);
           goto Error;
    }
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
    CAPABILITY(DEVICE_FMRADIO_STEREO_TX));
#endif
    if(msm_route_stream(PCM_PLAY, session_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
           ALOGE("msm_route_stream failed");
           goto Error;
    }
    addToTable(session_id,cur_rx,INVALID_DEVICE,FM_RADIO,true);
    if(sndDevice == mCurSndDevice || mCurSndDevice == -1) {
        enableDevice(cur_rx, 1);
#ifdef QCOM_ACDB_ENABLED
        acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
        msm_route_stream(PCM_PLAY,session_id,DEV_ID(cur_rx),1);
    }
    status = ioctl(mFmFd, AUDIO_START, 0);
    if (status < 0) {
            ALOGE("Cannot do AUDIO_START");
            goto Error;
    }
    return NO_ERROR;
    Error:
    if (mFmFd >= 0) {
        ::close(mFmFd);
        mFmFd = -1;
    }
    return NO_ERROR;
}

status_t AudioHardware::disableFM()
{
    ALOGD("disableFM");
    Routing_table* temp = NULL;
    temp = getNodeByStreamType(FM_RADIO);
    if(temp == NULL)
        return 0;
    if (mFmFd >= 0) {
            ::close(mFmFd);
            mFmFd = -1;
    }
    if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
    }
    if(!getNodeByStreamType(FM_A2DP)) {
       if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
          ALOGE("Device disable failed for device %d", DEVICE_FMRADIO_STEREO_TX);
       }
    }
    deleteFromTable(FM_RADIO);
    if(!getNodeByStreamType(VOICE_CALL)
        && !getNodeByStreamType(PCM_PLAY)
        ) {
        if(enableDevice(cur_rx, 0)) {
            ALOGV("Disable device[%d] failed errno = %d",DEV_ID(cur_rx),errno);
            return 0;
        }
    }
    return NO_ERROR;
}
#endif

status_t AudioHardware::dumpInternals(int fd, const Vector<String16>& args __unused)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioHardware::dumpInternals\n");
    snprintf(buffer, SIZE, "\tmInit: %s\n", mInit? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmMicMute: %s\n", mMicMute? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothNrec: %s\n", mBluetoothNrec? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothId: %d\n", mBluetoothId);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    dumpInternals(fd, args);
    for (size_t index = 0; index < mInputs.size(); index++) {
        mInputs[index]->dump(fd, args);
    }

    if (mOutput) {
        mOutput->dump(fd, args);
    }
    return NO_ERROR;
}

uint32_t AudioHardware::getInputSampleRate(uint32_t sampleRate)
{
    uint32_t i;
    uint32_t prevDelta;
    uint32_t delta;

    for (i = 0, prevDelta = 0xFFFFFFFF; i < sizeof(inputSamplingRates)/sizeof(uint32_t); i++, prevDelta = delta) {
        delta = abs(sampleRate - inputSamplingRates[i]);
        if (delta > prevDelta) break;
    }
    // i is always > 0 here
    return inputSamplingRates[i-1];
}

AudioHardware::AudioStreamOutMSM8x60::AudioStreamOutMSM8x60() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
{
}

status_t AudioHardware::AudioStreamOutMSM8x60::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    mHardware = hw;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    if ((lFormat != format()) ||
        (lChannels != channels()) ||
        (lRate != sampleRate())) {
        if (pFormat) *pFormat = format();
        if (pChannels) *pChannels = channels();
        if (pRate) *pRate = sampleRate();
        ALOGE("%s: Setting up correct values", __func__);
        return NO_ERROR;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;

    mDevices = devices;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutMSM8x60::~AudioStreamOutMSM8x60()
{
    if (mFd >= 0) close(mFd);
}

ssize_t AudioHardware::AudioStreamOutMSM8x60::write(const void* buffer, size_t bytes)
{
    //ALOGE("AudioStreamOutMSM8x60::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    unsigned short dec_id = INVALID_DEVICE;

    if (mStandby) {

        // open driver
        ALOGV("open driver");
        status = ::open("/dev/msm_pcm_out", O_WRONLY/*O_RDWR*/);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_out errno: %d", errno);
            goto Error;
        }
        mFd = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = sampleRate();
        config.buffer_size = bufferSize();
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            goto Error;
        }

        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        // fill 2 buffers before AUDIO_START
        mStartCount = AUDIO_HW_NUM_OUT_BUF;
        mStandby = false;
#ifdef HTC_ACOUSTIC_AUDIO
        if (support_tpa2051)
            do_tpa2051_control(0);
#endif
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written > 0) {
            count -= written;
            p += written;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            ALOGW("EAGAIN - retry");
        }
    }

    // start audio after we fill 2 buffers
    if (mStartCount) {
        if (--mStartCount == 0) {
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return 0;
            }
            ALOGE("write(): dec_id = %d cur_rx = %d\n",dec_id,cur_rx);
            if(cur_rx == INVALID_DEVICE) {
                //return 0; //temporary fix until team upmerges code to froyo tip
                cur_rx = 0;
                cur_tx = 1;
            }

            Mutex::Autolock lock(mDeviceSwitchLock);

#ifdef HTC_ACOUSTIC_AUDIO
            int snd_dev = mHardware->get_snd_dev();
            if (support_aic3254)
                mHardware->do_aic3254_control(snd_dev);
#endif
            ALOGE("cur_rx for pcm playback = %d",cur_rx);
            if(enableDevice(cur_rx, 1)) {
                ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
                return 0;
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
            if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(cur_rx), 1)) {
                ALOGE("msm_route_stream failed");
                return 0;
            }
            Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
            if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
                ALOGD("Routing PCM stream to speaker for combo device");
                ALOGD("combo:msm_route_stream(PCM_PLAY,session id:%d,dev id:%d,1)",dec_id,
                    DEV_ID(DEVICE_SPEAKER_RX));
                if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                    1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                }
                CurrentComboDeviceData.StreamType = PCM_PLAY;
            }
#endif
            addToTable(dec_id,cur_rx,INVALID_DEVICE,PCM_PLAY,true);
            ioctl(mFd, AUDIO_START, 0);
        }
    }
    return bytes;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());
    return status;
}

status_t AudioHardware::AudioStreamOutMSM8x60::standby()
{
    Routing_table* temp = NULL;
    //ALOGV("AudioStreamOutMSM8x60::standby()");
    status_t status = NO_ERROR;

    temp = getNodeByStreamType(PCM_PLAY);

    if(temp == NULL)
        return NO_ERROR;

    ALOGV("Deroute pcm stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("could not set stream routing\n");
        deleteFromTable(PCM_PLAY);
        return -1;
    }
    deleteFromTable(PCM_PLAY);
    updateDeviceInfo(cur_rx, cur_tx);
    if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_FM_ENABLED
       && !getNodeByStreamType(FM_RADIO)
#endif
     ) {
#if 0
        if(enableDevice(cur_rx, 0)) {
            ALOGE("Disabling device failed for cur_rx %d", cur_rx);
            return 0;
        }
#endif
    }

    if (!mStandby && mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }

    mStandby = true;
#ifdef HTC_ACOUSTIC_AUDIO
    if (support_aic3254)
        mHardware->do_aic3254_control(mHardware->get_snd_dev());
#endif
    return status;
}

status_t AudioHardware::AudioStreamOutMSM8x60::dump(int fd, const Vector<String16>& args __unused)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutMSM8x60::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStartCount: %d\n", mStartCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStandby: %s\n", mStandby? "true": "false");
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

bool AudioHardware::AudioStreamOutMSM8x60::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL, device);
        param.remove(key);
    }

#ifdef QCOM_FM_ENABLED
    key = String8(AUDIO_PARAMETER_KEY_HANDLE_FM);
    ALOGI("checking Handle FM");
    if (param.getInt(key, device) == NO_ERROR) {
        ALOGI("calling Handle FM");
        mHardware->handleFm(device);
        param.remove(key);
    }
#endif

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutMSM8x60::getRenderPosition(uint32_t *dspFrames __unused)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

status_t AudioHardware::AudioStreamOutMSM8x60::getPresentationPosition(uint64_t *frames, struct timespec *timestamp)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

int mFdin = -1;
AudioHardware::AudioStreamInMSM8x60::AudioStreamInMSM8x60() :
    mHardware(0), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFFERSIZE),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0), mForVR(0)
{
}

status_t AudioHardware::AudioStreamInMSM8x60::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if ((pFormat == 0) || (*pFormat != AUDIO_HW_IN_FORMAT))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        return BAD_VALUE;
    }

    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        ALOGE(" sample rate does not match\n");
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO | AudioSystem::CHANNEL_IN_STEREO)) == 0) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channel count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInMSM8x60::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFdin >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }
    status_t status =0;
    struct msm_voicerec_mode voc_rec_cfg;
#ifdef QCOM_FM_ENABLED
    if(devices == AUDIO_DEVICE_IN_FM_RX_A2DP) {
        status = ::open("/dev/msm_pcm_in", O_RDONLY);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mFdin = status;
        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount(*pChannels);
        config.sample_rate = *pRate;
        config.buffer_size = bufferSize();
        config.buffer_count = 2;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFdin, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            if (ioctl(mFdin, AUDIO_GET_CONFIG, &config) == 0) {
                if (config.channel_count == 1) {
                    *pChannels = AudioSystem::CHANNEL_IN_MONO;
                } else {
                    *pChannels = AudioSystem::CHANNEL_IN_STEREO;
                }
                *pRate = config.sample_rate;
            }
            goto Error;
        }

        ALOGV("confirm config");
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;
    } else
#endif
      if (*pFormat == AUDIO_HW_IN_FORMAT) {
        if (mHardware->mNumPcmRec > 0) {
            /* Only one PCM recording is allowed at a time */
            ALOGE("Multiple PCM recordings is not allowed");
            status = -1;
            goto Error;
        }
        // open audio input device
        status = ::open("/dev/msm_pcm_in", O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mHardware->mNumPcmRec ++;
        mFdin = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount((*pChannels) &
                              (AudioSystem::CHANNEL_IN_STEREO|
                               AudioSystem::CHANNEL_IN_MONO));

        config.sample_rate = *pRate;

        mBufferSize = mHardware->getInputBufferSize(config.sample_rate,
                                                    AUDIO_HW_IN_FORMAT,
                                                    config.channel_count);
        config.buffer_size = bufferSize();
        // Make buffers to be allocated in driver equal to the number of buffers
        // that AudioFlinger allocates (Shared memory)
        config.buffer_count = 4;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFdin, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            if (ioctl(mFdin, AUDIO_GET_CONFIG, &config) == 0) {
                if (config.channel_count == 1) {
                    *pChannels = AudioSystem::CHANNEL_IN_MONO;
                } else {
                    *pChannels = AudioSystem::CHANNEL_IN_STEREO;
                }
                *pRate = config.sample_rate;
            }
            goto Error;
        }

        ALOGV("confirm config");
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;

        if (mDevices == AUDIO_DEVICE_IN_VOICE_CALL) {
            if ((mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) &&
                (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                ALOGV("Recording Source: Voice Call Both Uplink and Downlink");
                voc_rec_cfg.rec_mode = VOC_REC_BOTH;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) {
                ALOGV("Recording Source: Voice Call DownLink");
                voc_rec_cfg.rec_mode = VOC_REC_DOWNLINK;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK) {
                ALOGV("Recording Source: Voice Call UpLink");
                voc_rec_cfg.rec_mode = VOC_REC_UPLINK;
            }
            if (ioctl(mFdin, AUDIO_SET_INCALL, &voc_rec_cfg)) {
                ALOGE("Error: AUDIO_SET_INCALL failed\n");
                goto  Error;
            }
        }
#ifdef QCOM_ACDB_ENABLED
    if(vr_enable && dualmic_enabled) {
        int audpre_mask = 0;
        audpre_mask = FLUENCE_ENABLE;

            ALOGV("enable fluence");
            if (ioctl(mFdin, AUDIO_ENABLE_AUDPRE, &audpre_mask)) {
                ALOGV("cannot write audio config");
                goto Error;
            }
        }
#endif
    }
    //mHardware->setMicMute_nosync(false);
    mState = AUDIO_INPUT_OPENED;
#ifdef HTC_ACOUSTIC_AUDIO
    mHardware->set_mRecordState(true);
#endif

    if (!acoustic)
    {
        return NO_ERROR;
    }
#ifdef QCOM_ACDB_ENABLED
    int (*msm8x60_set_audpre_params)(int, int);
    msm8x60_set_audpre_params = (int (*)(int, int))::dlsym(acoustic, "msm8x60_set_audpre_params");
    if ((*msm8x60_set_audpre_params) == 0) {
        ALOGI("msm8x60_set_audpre_params not present");
        return NO_ERROR;
    }

    int (*msm8x60_enable_audpre)(int, int, int);
    msm8x60_enable_audpre = (int (*)(int, int, int))::dlsym(acoustic, "msm8x60_enable_audpre");
    if ((*msm8x60_enable_audpre) == 0) {
        ALOGI("msm8x60_enable_audpre not present");
        return NO_ERROR;
    }

    audpre_index = calculate_audpre_table_index(mSampleRate);
    tx_iir_index = (audpre_index * 2) + (hw->checkOutputStandby() ? 0 : 1);
    ALOGD("audpre_index = %d, tx_iir_index = %d\n", audpre_index, tx_iir_index);

    /**
     * If audio-preprocessing failed, we should not block record.
     */
    status = msm8x60_set_audpre_params(audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot set audpre parameters");

    mAcoustics = acoustic_flags;
    status = msm8x60_enable_audpre((int)acoustic_flags, audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot enable audpre");
#endif
    return NO_ERROR;

Error:
    if (mFdin >= 0) {
        ::close(mFdin);
        mFdin = -1;
    }
    return status;
}

AudioHardware::AudioStreamInMSM8x60::~AudioStreamInMSM8x60()
{
    ALOGV("AudioStreamInMSM8x60 destructor");
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM8x60::read( void* buffer, ssize_t bytes)
{
    unsigned short dec_id = INVALID_DEVICE;
    ALOGV("AudioStreamInMSM8x60::read(%p, %zu)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t  aac_framesize= bytes;
    uint8_t* p = static_cast<uint8_t*>(buffer);
    uint32_t* recogPtr = (uint32_t *)p;
    uint16_t* frameCountPtr;
    uint16_t* frameSizePtr;

    if (mState < AUDIO_INPUT_OPENED) {
        AudioHardware *hw = mHardware;
        hw->mLock.lock();
        status_t status = set(hw, mDevices, &mFormat, &mChannels, &mSampleRate, mAcoustics);
        if (status != NO_ERROR) {
            hw->mLock.unlock();
            return -1;
        }
#ifdef QCOM_FM_ENABLED
        if((mDevices == AUDIO_DEVICE_IN_FM_RX) || (mDevices == AUDIO_DEVICE_IN_FM_RX_A2DP) ){
            if(ioctl(mFdin, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                hw->mLock.unlock();
                return -1;
            }

            if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
                ALOGE("enableDevice DEVICE_FMRADIO_STEREO_TX failed");
                hw->mLock.unlock();
                return -1;
             }

            acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
            CAPABILITY(DEVICE_FMRADIO_STEREO_TX));

            ALOGV("route FM");
            if(msm_route_stream(PCM_REC, dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
                ALOGE("msm_route_stream failed");
                hw->mLock.unlock();
                return -1;
            }

            //addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            mFirstread = false;
            if (mDevices == AUDIO_DEVICE_IN_FM_RX_A2DP) {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_A2DP,true);
                mFmRec = FM_A2DP_REC;
            } else {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_REC,true);
                mFmRec = FM_FILE_REC;
            }
            hw->mLock.unlock();
        } else
#endif
        {
            hw->mLock.unlock();
            if(ioctl(mFdin, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return -1;
            }
            Mutex::Autolock lock(mDeviceSwitchLock);
            if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
                  mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                 ALOGV("dec_id = %d,cur_tx= %d",dec_id,cur_tx);
                 if(cur_tx == INVALID_DEVICE)
                     cur_tx = DEVICE_HANDSET_TX;
                 if(enableDevice(cur_tx, 1)) {
                     ALOGE("enableDevice failed for device cur_rx %d",cur_rx);
                     return -1;
                 }
#ifdef QCOM_ACDB_ENABLED
                 acdb_loader_send_audio_cal(ACDB_ID(cur_tx), CAPABILITY(cur_tx));
#endif
                 if(msm_route_stream(PCM_REC, dec_id, DEV_ID(cur_tx), 1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                 }
                 addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            }
            mFirstread = false;
        }
    }


    if (mState < AUDIO_INPUT_STARTED) {
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
#ifdef QCOM_FM_ENABLED
            // force routing to input device
            // for FM recording, no need to reconfigure afe loopback path
            if (mFmRec != FM_FILE_REC) {
#endif
                mHardware->clearCurDevice();
                mHardware->doRouting(this, 0);
#ifdef HTC_ACOUSTIC_AUDIO
                if (support_aic3254) {
                    int snd_dev = mHardware->get_snd_dev();
                    mHardware->aic3254_config(snd_dev);
                    mHardware->do_aic3254_control(snd_dev);
                }
#endif
#ifdef QCOM_FM_ENABLED
            }
#endif
        }
        if (ioctl(mFdin, AUDIO_START, 0)) {
            ALOGE("Error starting record");
            standby();
            return -1;
        }
        mState = AUDIO_INPUT_STARTED;
    }

    bytes = 0;
    if(mFormat == AUDIO_HW_IN_FORMAT)
    {
        if(count < mBufferSize) {
            ALOGE("read:: read size requested is less than min input buffer size");
            return 0;
        }
        while (count >= mBufferSize) {
            ssize_t bytesRead = ::read(mFdin, buffer, count);
            usleep(1);
            if (bytesRead >= 0) {
                count -= bytesRead;
                p += bytesRead;
                bytes += bytesRead;
                if(!mFirstread)
                {
                   mFirstread = true;
                   ALOGE(" FirstRead Done bytesRead = %d count = %d",bytesRead,count);
                   break;
                }
            } else {
                if (errno != EAGAIN) return bytesRead;
                mRetryCount++;
                ALOGW("EAGAIN - retrying");
            }
        }
    }

    return bytes;
}

status_t AudioHardware::AudioStreamInMSM8x60::standby()
{
    bool isDriverClosed = false;
    ALOGD("AudioStreamInMSM8x60::standby()");
    Routing_table* temp = NULL;
    if (!mHardware) return -1;
#ifdef HTC_ACOUSTIC_AUDIO
    mHardware->set_mRecordState(false);
    if (support_aic3254) {
        int snd_dev = mHardware->get_snd_dev();
        mHardware->aic3254_config(snd_dev);
        mHardware->do_aic3254_control(snd_dev);
    }
#endif
    if (mState > AUDIO_INPUT_CLOSED) {
        if (mFdin >= 0) {
            ::close(mFdin);
            mFdin = -1;
            ALOGV("driver closed");
            isDriverClosed = true;
            if(mHardware->mNumPcmRec && mFormat == AUDIO_HW_IN_FORMAT) {
                mHardware->mNumPcmRec --;
            }
        }
        mState = AUDIO_INPUT_CLOSED;
    }
#ifdef QCOM_FM_ENABLED
       if (mFmRec == FM_A2DP_REC) {
        //A2DP Recording
        temp = getNodeByStreamType(FM_A2DP);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_A2DP);
        if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
            ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
        }
    }
    if (mFmRec == FM_FILE_REC) {
        //FM Recording
        temp = getNodeByStreamType(FM_REC);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_REC);
    }
#endif
    temp = getNodeByStreamType(PCM_REC);
    if(temp == NULL)
        return NO_ERROR;

    if(isDriverClosed){
        ALOGV("Deroute pcm stream");
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
            if(msm_route_stream(PCM_REC, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
               ALOGE("could not set stream routing\n");
               deleteFromTable(PCM_REC);
               return -1;
            }
        }
        ALOGV("Disable device");
        deleteFromTable(PCM_REC);
        if(!getNodeByStreamType(VOICE_CALL)) {
            if(enableDevice(cur_tx, 0)) {
                ALOGE("Disabling device failed for cur_tx %d", cur_tx);
                return 0;
            }
        }
    } //isDriverClosed condition
    // restore output routing if necessary
    mHardware->clearCurDevice();
    mHardware->doRouting(this, 0);
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM8x60::dump(int fd, const Vector<String16>& args __unused)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInMSM8x60::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd count: %d\n", mFdin);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmState: %d\n", mState);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(String8("vr_mode"), mForVR) == NO_ERROR)
        ALOGV("voice_recognition=%d", mForVR);

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else if (device) {
            mDevices = device;
            status = mHardware->doRouting(this, 0);
        }
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamInMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInMSM8x60 *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mInputs[i]->state() > AudioStreamInMSM8x60::AUDIO_INPUT_CLOSED) {
            return mInputs[i];
        }
    }

    return NULL;
}
extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android_audio_legacy
