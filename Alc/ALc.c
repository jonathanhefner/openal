/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <ctype.h>
#include <signal.h>

#include "alMain.h"
#include "alSource.h"
#include "alListener.h"
#include "alThunk.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "bs2b.h"
#include "alu.h"

#include "compat.h"
#include "threads.h"
#include "alstring.h"

#include "backends/base.h"


/************************************************
 * Backends
 ************************************************/
struct BackendInfo {
    const char *name;
    ALCbackendFactory* (*getFactory)(void);
    ALCboolean (*Init)(BackendFuncs*);
    void (*Deinit)(void);
    void (*Probe)(enum DevProbe);
    BackendFuncs Funcs;
};

#define EmptyFuncs { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
static struct BackendInfo BackendList[] = {
#ifdef HAVE_JACK
    { "jack", ALCjackBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_PULSEAUDIO
    { "pulse", ALCpulseBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_ALSA
    { "alsa", ALCalsaBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_COREAUDIO
    { "core", NULL, alc_ca_init, alc_ca_deinit, alc_ca_probe, EmptyFuncs },
#endif
#ifdef HAVE_OSS
    { "oss", ALCossBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_SOLARIS
    { "solaris", ALCsolarisBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_SNDIO
    { "sndio", NULL, alc_sndio_init, alc_sndio_deinit, alc_sndio_probe, EmptyFuncs },
#endif
#ifdef HAVE_QSA
    { "qsa", NULL, alc_qsa_init, alc_qsa_deinit, alc_qsa_probe, EmptyFuncs },
#endif
#ifdef HAVE_MMDEVAPI
    { "mmdevapi", ALCmmdevBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_DSOUND
    { "dsound", ALCdsoundBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_WINMM
    { "winmm", ALCwinmmBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_PORTAUDIO
    { "port", ALCportBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif
#ifdef HAVE_OPENSL
    { "opensl", NULL, alc_opensl_init, alc_opensl_deinit, alc_opensl_probe, EmptyFuncs },
#endif

    { "null", ALCnullBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#ifdef HAVE_WAVE
    { "wave", ALCwaveBackendFactory_getFactory, NULL, NULL, NULL, EmptyFuncs },
#endif

    { NULL, NULL, NULL, NULL, NULL, EmptyFuncs }
};
#undef EmptyFuncs

static struct BackendInfo PlaybackBackend;
static struct BackendInfo CaptureBackend;


/************************************************
 * Functions, enums, and errors
 ************************************************/
typedef struct ALCfunction {
    const ALCchar *funcName;
    ALCvoid *address;
} ALCfunction;

typedef struct ALCenums {
    const ALCchar *enumName;
    ALCenum value;
} ALCenums;

#define DECL(x) { #x, (ALCvoid*)(x) }
static const ALCfunction alcFunctions[] = {
    DECL(alcCreateContext),
    DECL(alcMakeContextCurrent),
    DECL(alcProcessContext),
    DECL(alcSuspendContext),
    DECL(alcDestroyContext),
    DECL(alcGetCurrentContext),
    DECL(alcGetContextsDevice),
    DECL(alcOpenDevice),
    DECL(alcCloseDevice),
    DECL(alcGetError),
    DECL(alcIsExtensionPresent),
    DECL(alcGetProcAddress),
    DECL(alcGetEnumValue),
    DECL(alcGetString),
    DECL(alcGetIntegerv),
    DECL(alcCaptureOpenDevice),
    DECL(alcCaptureCloseDevice),
    DECL(alcCaptureStart),
    DECL(alcCaptureStop),
    DECL(alcCaptureSamples),

    DECL(alcSetThreadContext),
    DECL(alcGetThreadContext),

    DECL(alcLoopbackOpenDeviceSOFT),
    DECL(alcIsRenderFormatSupportedSOFT),
    DECL(alcRenderSamplesSOFT),

    DECL(alcDevicePauseSOFT),
    DECL(alcDeviceResumeSOFT),

    DECL(alcGetStringiSOFT),
    DECL(alcResetDeviceSOFT),

    DECL(alcGetInteger64vSOFT),

    DECL(alEnable),
    DECL(alDisable),
    DECL(alIsEnabled),
    DECL(alGetString),
    DECL(alGetBooleanv),
    DECL(alGetIntegerv),
    DECL(alGetFloatv),
    DECL(alGetDoublev),
    DECL(alGetBoolean),
    DECL(alGetInteger),
    DECL(alGetFloat),
    DECL(alGetDouble),
    DECL(alGetError),
    DECL(alIsExtensionPresent),
    DECL(alGetProcAddress),
    DECL(alGetEnumValue),
    DECL(alListenerf),
    DECL(alListener3f),
    DECL(alListenerfv),
    DECL(alListeneri),
    DECL(alListener3i),
    DECL(alListeneriv),
    DECL(alGetListenerf),
    DECL(alGetListener3f),
    DECL(alGetListenerfv),
    DECL(alGetListeneri),
    DECL(alGetListener3i),
    DECL(alGetListeneriv),
    DECL(alGenSources),
    DECL(alDeleteSources),
    DECL(alIsSource),
    DECL(alSourcef),
    DECL(alSource3f),
    DECL(alSourcefv),
    DECL(alSourcei),
    DECL(alSource3i),
    DECL(alSourceiv),
    DECL(alGetSourcef),
    DECL(alGetSource3f),
    DECL(alGetSourcefv),
    DECL(alGetSourcei),
    DECL(alGetSource3i),
    DECL(alGetSourceiv),
    DECL(alSourcePlayv),
    DECL(alSourceStopv),
    DECL(alSourceRewindv),
    DECL(alSourcePausev),
    DECL(alSourcePlay),
    DECL(alSourceStop),
    DECL(alSourceRewind),
    DECL(alSourcePause),
    DECL(alSourceQueueBuffers),
    DECL(alSourceUnqueueBuffers),
    DECL(alGenBuffers),
    DECL(alDeleteBuffers),
    DECL(alIsBuffer),
    DECL(alBufferData),
    DECL(alBufferf),
    DECL(alBuffer3f),
    DECL(alBufferfv),
    DECL(alBufferi),
    DECL(alBuffer3i),
    DECL(alBufferiv),
    DECL(alGetBufferf),
    DECL(alGetBuffer3f),
    DECL(alGetBufferfv),
    DECL(alGetBufferi),
    DECL(alGetBuffer3i),
    DECL(alGetBufferiv),
    DECL(alDopplerFactor),
    DECL(alDopplerVelocity),
    DECL(alSpeedOfSound),
    DECL(alDistanceModel),

    DECL(alGenFilters),
    DECL(alDeleteFilters),
    DECL(alIsFilter),
    DECL(alFilteri),
    DECL(alFilteriv),
    DECL(alFilterf),
    DECL(alFilterfv),
    DECL(alGetFilteri),
    DECL(alGetFilteriv),
    DECL(alGetFilterf),
    DECL(alGetFilterfv),
    DECL(alGenEffects),
    DECL(alDeleteEffects),
    DECL(alIsEffect),
    DECL(alEffecti),
    DECL(alEffectiv),
    DECL(alEffectf),
    DECL(alEffectfv),
    DECL(alGetEffecti),
    DECL(alGetEffectiv),
    DECL(alGetEffectf),
    DECL(alGetEffectfv),
    DECL(alGenAuxiliaryEffectSlots),
    DECL(alDeleteAuxiliaryEffectSlots),
    DECL(alIsAuxiliaryEffectSlot),
    DECL(alAuxiliaryEffectSloti),
    DECL(alAuxiliaryEffectSlotiv),
    DECL(alAuxiliaryEffectSlotf),
    DECL(alAuxiliaryEffectSlotfv),
    DECL(alGetAuxiliaryEffectSloti),
    DECL(alGetAuxiliaryEffectSlotiv),
    DECL(alGetAuxiliaryEffectSlotf),
    DECL(alGetAuxiliaryEffectSlotfv),

    DECL(alBufferSubDataSOFT),

    DECL(alBufferSamplesSOFT),
    DECL(alBufferSubSamplesSOFT),
    DECL(alGetBufferSamplesSOFT),
    DECL(alIsBufferFormatSupportedSOFT),

    DECL(alDeferUpdatesSOFT),
    DECL(alProcessUpdatesSOFT),

    DECL(alSourcedSOFT),
    DECL(alSource3dSOFT),
    DECL(alSourcedvSOFT),
    DECL(alGetSourcedSOFT),
    DECL(alGetSource3dSOFT),
    DECL(alGetSourcedvSOFT),
    DECL(alSourcei64SOFT),
    DECL(alSource3i64SOFT),
    DECL(alSourcei64vSOFT),
    DECL(alGetSourcei64SOFT),
    DECL(alGetSource3i64SOFT),
    DECL(alGetSourcei64vSOFT),

    { NULL, NULL }
};
#undef DECL

#define DECL(x) { #x, (x) }
static const ALCenums enumeration[] = {
    DECL(ALC_INVALID),
    DECL(ALC_FALSE),
    DECL(ALC_TRUE),

    DECL(ALC_MAJOR_VERSION),
    DECL(ALC_MINOR_VERSION),
    DECL(ALC_ATTRIBUTES_SIZE),
    DECL(ALC_ALL_ATTRIBUTES),
    DECL(ALC_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_DEVICE_SPECIFIER),
    DECL(ALC_ALL_DEVICES_SPECIFIER),
    DECL(ALC_DEFAULT_ALL_DEVICES_SPECIFIER),
    DECL(ALC_EXTENSIONS),
    DECL(ALC_FREQUENCY),
    DECL(ALC_REFRESH),
    DECL(ALC_SYNC),
    DECL(ALC_MONO_SOURCES),
    DECL(ALC_STEREO_SOURCES),
    DECL(ALC_CAPTURE_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_SAMPLES),
    DECL(ALC_CONNECTED),

    DECL(ALC_EFX_MAJOR_VERSION),
    DECL(ALC_EFX_MINOR_VERSION),
    DECL(ALC_MAX_AUXILIARY_SENDS),

    DECL(ALC_FORMAT_CHANNELS_SOFT),
    DECL(ALC_FORMAT_TYPE_SOFT),

    DECL(ALC_MONO_SOFT),
    DECL(ALC_STEREO_SOFT),
    DECL(ALC_QUAD_SOFT),
    DECL(ALC_5POINT1_SOFT),
    DECL(ALC_6POINT1_SOFT),
    DECL(ALC_7POINT1_SOFT),

    DECL(ALC_BYTE_SOFT),
    DECL(ALC_UNSIGNED_BYTE_SOFT),
    DECL(ALC_SHORT_SOFT),
    DECL(ALC_UNSIGNED_SHORT_SOFT),
    DECL(ALC_INT_SOFT),
    DECL(ALC_UNSIGNED_INT_SOFT),
    DECL(ALC_FLOAT_SOFT),

    DECL(ALC_HRTF_SOFT),
    DECL(ALC_DONT_CARE_SOFT),
    DECL(ALC_HRTF_STATUS_SOFT),
    DECL(ALC_HRTF_DISABLED_SOFT),
    DECL(ALC_HRTF_ENABLED_SOFT),
    DECL(ALC_HRTF_DENIED_SOFT),
    DECL(ALC_HRTF_REQUIRED_SOFT),
    DECL(ALC_HRTF_HEADPHONES_DETECTED_SOFT),
    DECL(ALC_HRTF_UNSUPPORTED_FORMAT_SOFT),
    DECL(ALC_NUM_HRTF_SPECIFIERS_SOFT),
    DECL(ALC_HRTF_SPECIFIER_SOFT),
    DECL(ALC_HRTF_ID_SOFT),

    DECL(ALC_NO_ERROR),
    DECL(ALC_INVALID_DEVICE),
    DECL(ALC_INVALID_CONTEXT),
    DECL(ALC_INVALID_ENUM),
    DECL(ALC_INVALID_VALUE),
    DECL(ALC_OUT_OF_MEMORY),


    DECL(AL_INVALID),
    DECL(AL_NONE),
    DECL(AL_FALSE),
    DECL(AL_TRUE),

    DECL(AL_SOURCE_RELATIVE),
    DECL(AL_CONE_INNER_ANGLE),
    DECL(AL_CONE_OUTER_ANGLE),
    DECL(AL_PITCH),
    DECL(AL_POSITION),
    DECL(AL_DIRECTION),
    DECL(AL_VELOCITY),
    DECL(AL_LOOPING),
    DECL(AL_BUFFER),
    DECL(AL_GAIN),
    DECL(AL_MIN_GAIN),
    DECL(AL_MAX_GAIN),
    DECL(AL_ORIENTATION),
    DECL(AL_REFERENCE_DISTANCE),
    DECL(AL_ROLLOFF_FACTOR),
    DECL(AL_CONE_OUTER_GAIN),
    DECL(AL_MAX_DISTANCE),
    DECL(AL_SEC_OFFSET),
    DECL(AL_SAMPLE_OFFSET),
    DECL(AL_SAMPLE_RW_OFFSETS_SOFT),
    DECL(AL_BYTE_OFFSET),
    DECL(AL_BYTE_RW_OFFSETS_SOFT),
    DECL(AL_SOURCE_TYPE),
    DECL(AL_STATIC),
    DECL(AL_STREAMING),
    DECL(AL_UNDETERMINED),
    DECL(AL_METERS_PER_UNIT),
    DECL(AL_LOOP_POINTS_SOFT),
    DECL(AL_DIRECT_CHANNELS_SOFT),

    DECL(AL_DIRECT_FILTER),
    DECL(AL_AUXILIARY_SEND_FILTER),
    DECL(AL_AIR_ABSORPTION_FACTOR),
    DECL(AL_ROOM_ROLLOFF_FACTOR),
    DECL(AL_CONE_OUTER_GAINHF),
    DECL(AL_DIRECT_FILTER_GAINHF_AUTO),
    DECL(AL_AUXILIARY_SEND_FILTER_GAIN_AUTO),
    DECL(AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO),

    DECL(AL_SOURCE_STATE),
    DECL(AL_INITIAL),
    DECL(AL_PLAYING),
    DECL(AL_PAUSED),
    DECL(AL_STOPPED),

    DECL(AL_BUFFERS_QUEUED),
    DECL(AL_BUFFERS_PROCESSED),

    DECL(AL_FORMAT_MONO8),
    DECL(AL_FORMAT_MONO16),
    DECL(AL_FORMAT_MONO_FLOAT32),
    DECL(AL_FORMAT_MONO_DOUBLE_EXT),
    DECL(AL_FORMAT_STEREO8),
    DECL(AL_FORMAT_STEREO16),
    DECL(AL_FORMAT_STEREO_FLOAT32),
    DECL(AL_FORMAT_STEREO_DOUBLE_EXT),
    DECL(AL_FORMAT_MONO_IMA4),
    DECL(AL_FORMAT_STEREO_IMA4),
    DECL(AL_FORMAT_MONO_MSADPCM_SOFT),
    DECL(AL_FORMAT_STEREO_MSADPCM_SOFT),
    DECL(AL_FORMAT_QUAD8_LOKI),
    DECL(AL_FORMAT_QUAD16_LOKI),
    DECL(AL_FORMAT_QUAD8),
    DECL(AL_FORMAT_QUAD16),
    DECL(AL_FORMAT_QUAD32),
    DECL(AL_FORMAT_51CHN8),
    DECL(AL_FORMAT_51CHN16),
    DECL(AL_FORMAT_51CHN32),
    DECL(AL_FORMAT_61CHN8),
    DECL(AL_FORMAT_61CHN16),
    DECL(AL_FORMAT_61CHN32),
    DECL(AL_FORMAT_71CHN8),
    DECL(AL_FORMAT_71CHN16),
    DECL(AL_FORMAT_71CHN32),
    DECL(AL_FORMAT_REAR8),
    DECL(AL_FORMAT_REAR16),
    DECL(AL_FORMAT_REAR32),
    DECL(AL_FORMAT_MONO_MULAW),
    DECL(AL_FORMAT_MONO_MULAW_EXT),
    DECL(AL_FORMAT_STEREO_MULAW),
    DECL(AL_FORMAT_STEREO_MULAW_EXT),
    DECL(AL_FORMAT_QUAD_MULAW),
    DECL(AL_FORMAT_51CHN_MULAW),
    DECL(AL_FORMAT_61CHN_MULAW),
    DECL(AL_FORMAT_71CHN_MULAW),
    DECL(AL_FORMAT_REAR_MULAW),
    DECL(AL_FORMAT_MONO_ALAW_EXT),
    DECL(AL_FORMAT_STEREO_ALAW_EXT),

    DECL(AL_MONO8_SOFT),
    DECL(AL_MONO16_SOFT),
    DECL(AL_MONO32F_SOFT),
    DECL(AL_STEREO8_SOFT),
    DECL(AL_STEREO16_SOFT),
    DECL(AL_STEREO32F_SOFT),
    DECL(AL_QUAD8_SOFT),
    DECL(AL_QUAD16_SOFT),
    DECL(AL_QUAD32F_SOFT),
    DECL(AL_REAR8_SOFT),
    DECL(AL_REAR16_SOFT),
    DECL(AL_REAR32F_SOFT),
    DECL(AL_5POINT1_8_SOFT),
    DECL(AL_5POINT1_16_SOFT),
    DECL(AL_5POINT1_32F_SOFT),
    DECL(AL_6POINT1_8_SOFT),
    DECL(AL_6POINT1_16_SOFT),
    DECL(AL_6POINT1_32F_SOFT),
    DECL(AL_7POINT1_8_SOFT),
    DECL(AL_7POINT1_16_SOFT),
    DECL(AL_7POINT1_32F_SOFT),
    DECL(AL_FORMAT_BFORMAT2D_8),
    DECL(AL_FORMAT_BFORMAT2D_16),
    DECL(AL_FORMAT_BFORMAT2D_FLOAT32),
    DECL(AL_FORMAT_BFORMAT2D_MULAW),
    DECL(AL_FORMAT_BFORMAT3D_8),
    DECL(AL_FORMAT_BFORMAT3D_16),
    DECL(AL_FORMAT_BFORMAT3D_FLOAT32),
    DECL(AL_FORMAT_BFORMAT3D_MULAW),

    DECL(AL_MONO_SOFT),
    DECL(AL_STEREO_SOFT),
    DECL(AL_QUAD_SOFT),
    DECL(AL_REAR_SOFT),
    DECL(AL_5POINT1_SOFT),
    DECL(AL_6POINT1_SOFT),
    DECL(AL_7POINT1_SOFT),

    DECL(AL_BYTE_SOFT),
    DECL(AL_UNSIGNED_BYTE_SOFT),
    DECL(AL_SHORT_SOFT),
    DECL(AL_UNSIGNED_SHORT_SOFT),
    DECL(AL_INT_SOFT),
    DECL(AL_UNSIGNED_INT_SOFT),
    DECL(AL_FLOAT_SOFT),
    DECL(AL_DOUBLE_SOFT),
    DECL(AL_BYTE3_SOFT),
    DECL(AL_UNSIGNED_BYTE3_SOFT),

    DECL(AL_FREQUENCY),
    DECL(AL_BITS),
    DECL(AL_CHANNELS),
    DECL(AL_SIZE),
    DECL(AL_INTERNAL_FORMAT_SOFT),
    DECL(AL_BYTE_LENGTH_SOFT),
    DECL(AL_SAMPLE_LENGTH_SOFT),
    DECL(AL_SEC_LENGTH_SOFT),
    DECL(AL_UNPACK_BLOCK_ALIGNMENT_SOFT),
    DECL(AL_PACK_BLOCK_ALIGNMENT_SOFT),

    DECL(AL_UNUSED),
    DECL(AL_PENDING),
    DECL(AL_PROCESSED),

    DECL(AL_NO_ERROR),
    DECL(AL_INVALID_NAME),
    DECL(AL_INVALID_ENUM),
    DECL(AL_INVALID_VALUE),
    DECL(AL_INVALID_OPERATION),
    DECL(AL_OUT_OF_MEMORY),

    DECL(AL_VENDOR),
    DECL(AL_VERSION),
    DECL(AL_RENDERER),
    DECL(AL_EXTENSIONS),

    DECL(AL_DOPPLER_FACTOR),
    DECL(AL_DOPPLER_VELOCITY),
    DECL(AL_DISTANCE_MODEL),
    DECL(AL_SPEED_OF_SOUND),
    DECL(AL_SOURCE_DISTANCE_MODEL),
    DECL(AL_DEFERRED_UPDATES_SOFT),

    DECL(AL_INVERSE_DISTANCE),
    DECL(AL_INVERSE_DISTANCE_CLAMPED),
    DECL(AL_LINEAR_DISTANCE),
    DECL(AL_LINEAR_DISTANCE_CLAMPED),
    DECL(AL_EXPONENT_DISTANCE),
    DECL(AL_EXPONENT_DISTANCE_CLAMPED),

    DECL(AL_FILTER_TYPE),
    DECL(AL_FILTER_NULL),
    DECL(AL_FILTER_LOWPASS),
    DECL(AL_FILTER_HIGHPASS),
    DECL(AL_FILTER_BANDPASS),

    DECL(AL_LOWPASS_GAIN),
    DECL(AL_LOWPASS_GAINHF),

    DECL(AL_HIGHPASS_GAIN),
    DECL(AL_HIGHPASS_GAINLF),

    DECL(AL_BANDPASS_GAIN),
    DECL(AL_BANDPASS_GAINHF),
    DECL(AL_BANDPASS_GAINLF),

    DECL(AL_EFFECT_TYPE),
    DECL(AL_EFFECT_NULL),
    DECL(AL_EFFECT_REVERB),
    DECL(AL_EFFECT_EAXREVERB),
    DECL(AL_EFFECT_CHORUS),
    DECL(AL_EFFECT_DISTORTION),
    DECL(AL_EFFECT_ECHO),
    DECL(AL_EFFECT_FLANGER),
#if 0
    DECL(AL_EFFECT_FREQUENCY_SHIFTER),
    DECL(AL_EFFECT_VOCAL_MORPHER),
    DECL(AL_EFFECT_PITCH_SHIFTER),
#endif
    DECL(AL_EFFECT_RING_MODULATOR),
#if 0
    DECL(AL_EFFECT_AUTOWAH),
#endif
    DECL(AL_EFFECT_COMPRESSOR),
    DECL(AL_EFFECT_EQUALIZER),
    DECL(AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT),
    DECL(AL_EFFECT_DEDICATED_DIALOGUE),

    DECL(AL_EAXREVERB_DENSITY),
    DECL(AL_EAXREVERB_DIFFUSION),
    DECL(AL_EAXREVERB_GAIN),
    DECL(AL_EAXREVERB_GAINHF),
    DECL(AL_EAXREVERB_GAINLF),
    DECL(AL_EAXREVERB_DECAY_TIME),
    DECL(AL_EAXREVERB_DECAY_HFRATIO),
    DECL(AL_EAXREVERB_DECAY_LFRATIO),
    DECL(AL_EAXREVERB_REFLECTIONS_GAIN),
    DECL(AL_EAXREVERB_REFLECTIONS_DELAY),
    DECL(AL_EAXREVERB_REFLECTIONS_PAN),
    DECL(AL_EAXREVERB_LATE_REVERB_GAIN),
    DECL(AL_EAXREVERB_LATE_REVERB_DELAY),
    DECL(AL_EAXREVERB_LATE_REVERB_PAN),
    DECL(AL_EAXREVERB_ECHO_TIME),
    DECL(AL_EAXREVERB_ECHO_DEPTH),
    DECL(AL_EAXREVERB_MODULATION_TIME),
    DECL(AL_EAXREVERB_MODULATION_DEPTH),
    DECL(AL_EAXREVERB_AIR_ABSORPTION_GAINHF),
    DECL(AL_EAXREVERB_HFREFERENCE),
    DECL(AL_EAXREVERB_LFREFERENCE),
    DECL(AL_EAXREVERB_ROOM_ROLLOFF_FACTOR),
    DECL(AL_EAXREVERB_DECAY_HFLIMIT),

    DECL(AL_REVERB_DENSITY),
    DECL(AL_REVERB_DIFFUSION),
    DECL(AL_REVERB_GAIN),
    DECL(AL_REVERB_GAINHF),
    DECL(AL_REVERB_DECAY_TIME),
    DECL(AL_REVERB_DECAY_HFRATIO),
    DECL(AL_REVERB_REFLECTIONS_GAIN),
    DECL(AL_REVERB_REFLECTIONS_DELAY),
    DECL(AL_REVERB_LATE_REVERB_GAIN),
    DECL(AL_REVERB_LATE_REVERB_DELAY),
    DECL(AL_REVERB_AIR_ABSORPTION_GAINHF),
    DECL(AL_REVERB_ROOM_ROLLOFF_FACTOR),
    DECL(AL_REVERB_DECAY_HFLIMIT),

    DECL(AL_CHORUS_WAVEFORM),
    DECL(AL_CHORUS_PHASE),
    DECL(AL_CHORUS_RATE),
    DECL(AL_CHORUS_DEPTH),
    DECL(AL_CHORUS_FEEDBACK),
    DECL(AL_CHORUS_DELAY),

    DECL(AL_DISTORTION_EDGE),
    DECL(AL_DISTORTION_GAIN),
    DECL(AL_DISTORTION_LOWPASS_CUTOFF),
    DECL(AL_DISTORTION_EQCENTER),
    DECL(AL_DISTORTION_EQBANDWIDTH),

    DECL(AL_ECHO_DELAY),
    DECL(AL_ECHO_LRDELAY),
    DECL(AL_ECHO_DAMPING),
    DECL(AL_ECHO_FEEDBACK),
    DECL(AL_ECHO_SPREAD),

    DECL(AL_FLANGER_WAVEFORM),
    DECL(AL_FLANGER_PHASE),
    DECL(AL_FLANGER_RATE),
    DECL(AL_FLANGER_DEPTH),
    DECL(AL_FLANGER_FEEDBACK),
    DECL(AL_FLANGER_DELAY),

    DECL(AL_RING_MODULATOR_FREQUENCY),
    DECL(AL_RING_MODULATOR_HIGHPASS_CUTOFF),
    DECL(AL_RING_MODULATOR_WAVEFORM),

#if 0
    DECL(AL_AUTOWAH_ATTACK_TIME),
    DECL(AL_AUTOWAH_PEAK_GAIN),
    DECL(AL_AUTOWAH_RELEASE_TIME),
    DECL(AL_AUTOWAH_RESONANCE),
#endif

    DECL(AL_COMPRESSOR_ONOFF),

    DECL(AL_EQUALIZER_LOW_GAIN),
    DECL(AL_EQUALIZER_LOW_CUTOFF),
    DECL(AL_EQUALIZER_MID1_GAIN),
    DECL(AL_EQUALIZER_MID1_CENTER),
    DECL(AL_EQUALIZER_MID1_WIDTH),
    DECL(AL_EQUALIZER_MID2_GAIN),
    DECL(AL_EQUALIZER_MID2_CENTER),
    DECL(AL_EQUALIZER_MID2_WIDTH),
    DECL(AL_EQUALIZER_HIGH_GAIN),
    DECL(AL_EQUALIZER_HIGH_CUTOFF),

    DECL(AL_DEDICATED_GAIN),

    { NULL, (ALCenum)0 }
};
#undef DECL

static const ALCchar alcNoError[] = "No Error";
static const ALCchar alcErrInvalidDevice[] = "Invalid Device";
static const ALCchar alcErrInvalidContext[] = "Invalid Context";
static const ALCchar alcErrInvalidEnum[] = "Invalid Enum";
static const ALCchar alcErrInvalidValue[] = "Invalid Value";
static const ALCchar alcErrOutOfMemory[] = "Out of Memory";


/************************************************
 * Global variables
 ************************************************/

/* Enumerated device names */
static const ALCchar alcDefaultName[] = "OpenAL Soft\0";

static al_string alcAllDevicesList;
static al_string alcCaptureDeviceList;

/* Default is always the first in the list */
static ALCchar *alcDefaultAllDevicesSpecifier;
static ALCchar *alcCaptureDefaultDeviceSpecifier;

/* Default context extensions */
static const ALchar alExtList[] =
    "AL_EXT_ALAW AL_EXT_BFORMAT AL_EXT_DOUBLE AL_EXT_EXPONENT_DISTANCE "
    "AL_EXT_FLOAT32 AL_EXT_IMA4 AL_EXT_LINEAR_DISTANCE AL_EXT_MCFORMATS "
    "AL_EXT_MULAW AL_EXT_MULAW_BFORMAT AL_EXT_MULAW_MCFORMATS AL_EXT_OFFSET "
    "AL_EXT_source_distance_model AL_LOKI_quadriphonic AL_SOFT_block_alignment "
    "AL_SOFT_buffer_samples AL_SOFT_buffer_sub_data AL_SOFT_deferred_updates "
    "AL_SOFT_direct_channels AL_SOFT_loop_points AL_SOFT_MSADPCM "
    "AL_SOFT_source_latency AL_SOFT_source_length";

static ATOMIC(ALCenum) LastNullDeviceError = ATOMIC_INIT_STATIC(ALC_NO_ERROR);

/* Thread-local current context */
static altss_t LocalContext;
/* Process-wide current context */
static ATOMIC(ALCcontext*) GlobalContext = ATOMIC_INIT_STATIC(NULL);

/* Mixing thread piority level */
ALint RTPrioLevel;

FILE *LogFile;
#ifdef _DEBUG
enum LogLevel LogLevel = LogWarning;
#else
enum LogLevel LogLevel = LogError;
#endif

/* Flag to trap ALC device errors */
static ALCboolean TrapALCError = ALC_FALSE;

/* One-time configuration init control */
static alonce_flag alc_config_once = AL_ONCE_FLAG_INIT;

/* Default effect that applies to sources that don't have an effect on send 0 */
static ALeffect DefaultEffect;

/* Flag to specify if alcSuspendContext/alcProcessContext should defer/process
 * updates.
 */
static ALCboolean SuspendDefers = ALC_TRUE;


/************************************************
 * ALC information
 ************************************************/
static const ALCchar alcNoDeviceExtList[] =
    "ALC_ENUMERATE_ALL_EXT ALC_ENUMERATION_EXT ALC_EXT_CAPTURE "
    "ALC_EXT_thread_local_context ALC_SOFT_loopback";
static const ALCchar alcExtensionList[] =
    "ALC_ENUMERATE_ALL_EXT ALC_ENUMERATION_EXT ALC_EXT_CAPTURE "
    "ALC_EXT_DEDICATED ALC_EXT_disconnect ALC_EXT_EFX "
    "ALC_EXT_thread_local_context ALC_SOFTX_device_clock ALC_SOFT_HRTF "
    "ALC_SOFT_loopback ALC_SOFT_pause_device";
static const ALCint alcMajorVersion = 1;
static const ALCint alcMinorVersion = 1;

static const ALCint alcEFXMajorVersion = 1;
static const ALCint alcEFXMinorVersion = 0;


/************************************************
 * Device lists
 ************************************************/
static ATOMIC(ALCdevice*) DeviceList = ATOMIC_INIT_STATIC(NULL);

static almtx_t ListLock;
static inline void LockLists(void)
{
    int lockret = almtx_lock(&ListLock);
    assert(lockret == althrd_success);
}
static inline void UnlockLists(void)
{
    int unlockret = almtx_unlock(&ListLock);
    assert(unlockret == althrd_success);
}

/************************************************
 * Library initialization
 ************************************************/
#if defined(_WIN32)
static void alc_init(void);
static void alc_deinit(void);
static void alc_deinit_safe(void);

#ifndef AL_LIBTYPE_STATIC
BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD reason, LPVOID lpReserved)
{
    switch(reason)
    {
        case DLL_PROCESS_ATTACH:
            /* Pin the DLL so we won't get unloaded until the process terminates */
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (WCHAR*)hModule, &hModule);
            alc_init();
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            if(!lpReserved)
                alc_deinit();
            else
                alc_deinit_safe();
            break;
    }
    return TRUE;
}
#elif defined(_MSC_VER)
#pragma section(".CRT$XCU",read)
static void alc_constructor(void);
static void alc_destructor(void);
__declspec(allocate(".CRT$XCU")) void (__cdecl* alc_constructor_)(void) = alc_constructor;

static void alc_constructor(void)
{
    atexit(alc_destructor);
    alc_init();
}

static void alc_destructor(void)
{
    alc_deinit();
}
#elif defined(HAVE_GCC_DESTRUCTOR)
static void alc_init(void) __attribute__((constructor));
static void alc_deinit(void) __attribute__((destructor));
#else
#error "No static initialization available on this platform!"
#endif

#elif defined(HAVE_GCC_DESTRUCTOR)

static void alc_init(void) __attribute__((constructor));
static void alc_deinit(void) __attribute__((destructor));

#else
#error "No global initialization available on this platform!"
#endif

static void ReleaseThreadCtx(void *ptr);
static void alc_init(void)
{
    const char *str;
    int ret;

    LogFile = stderr;

    AL_STRING_INIT(alcAllDevicesList);
    AL_STRING_INIT(alcCaptureDeviceList);

    str = getenv("__ALSOFT_HALF_ANGLE_CONES");
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, NULL, 0) == 1))
        ConeScale *= 0.5f;

    str = getenv("__ALSOFT_REVERSE_Z");
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, NULL, 0) == 1))
        ZScale *= -1.0f;

    ret = altss_create(&LocalContext, ReleaseThreadCtx);
    assert(ret == althrd_success);

    ret = almtx_init(&ListLock, almtx_recursive);
    assert(ret == althrd_success);

    ThunkInit();
}

static void alc_initconfig(void)
{
    const char *devs, *str;
    ALuint capfilter;
    float valf;
    int i, n;

    str = getenv("ALSOFT_LOGLEVEL");
    if(str)
    {
        long lvl = strtol(str, NULL, 0);
        if(lvl >= NoLog && lvl <= LogRef)
            LogLevel = lvl;
    }

    str = getenv("ALSOFT_LOGFILE");
    if(str && str[0])
    {
        FILE *logfile = al_fopen(str, "wt");
        if(logfile) LogFile = logfile;
        else ERR("Failed to open log file '%s'\n", str);
    }

    {
        char buf[1024] = "";
        int len = snprintf(buf, sizeof(buf), "%s", BackendList[0].name);
        for(i = 1;BackendList[i].name;i++)
            len += snprintf(buf+len, sizeof(buf)-len, ", %s", BackendList[i].name);
        TRACE("Supported backends: %s\n", buf);
    }
    ReadALConfig();

    str = getenv("__ALSOFT_SUSPEND_CONTEXT");
    if(str && *str)
    {
        if(strcasecmp(str, "ignore") == 0)
        {
            SuspendDefers = ALC_FALSE;
            TRACE("Selected context suspend behavior, \"ignore\"\n");
        }
        else
            ERR("Unhandled context suspend behavior setting: \"%s\"\n", str);
    }

    capfilter = 0;
#if defined(HAVE_SSE4_1)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3 | CPU_CAP_SSE4_1;
#elif defined(HAVE_SSE3)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3;
#elif defined(HAVE_SSE2)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2;
#elif defined(HAVE_SSE)
    capfilter |= CPU_CAP_SSE;
#endif
#ifdef HAVE_NEON
    capfilter |= CPU_CAP_NEON;
#endif
    if(ConfigValueStr(NULL, NULL, "disable-cpu-exts", &str))
    {
        if(strcasecmp(str, "all") == 0)
            capfilter = 0;
        else
        {
            size_t len;
            const char *next = str;

            do {
                str = next;
                while(isspace(str[0]))
                    str++;
                next = strchr(str, ',');

                if(!str[0] || str[0] == ',')
                    continue;

                len = (next ? ((size_t)(next-str)) : strlen(str));
                while(len > 0 && isspace(str[len-1]))
                    len--;
                if(len == 3 && strncasecmp(str, "sse", len) == 0)
                    capfilter &= ~CPU_CAP_SSE;
                else if(len == 4 && strncasecmp(str, "sse2", len) == 0)
                    capfilter &= ~CPU_CAP_SSE2;
                else if(len == 4 && strncasecmp(str, "sse3", len) == 0)
                    capfilter &= ~CPU_CAP_SSE3;
                else if(len == 6 && strncasecmp(str, "sse4.1", len) == 0)
                    capfilter &= ~CPU_CAP_SSE4_1;
                else if(len == 4 && strncasecmp(str, "neon", len) == 0)
                    capfilter &= ~CPU_CAP_NEON;
                else
                    WARN("Invalid CPU extension \"%s\"\n", str);
            } while(next++);
        }
    }
    FillCPUCaps(capfilter);

#ifdef _WIN32
    RTPrioLevel = 1;
#else
    RTPrioLevel = 0;
#endif
    ConfigValueInt(NULL, NULL, "rt-prio", &RTPrioLevel);

    aluInitMixer();

    str = getenv("ALSOFT_TRAP_ERROR");
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, NULL, 0) == 1))
    {
        TrapALError  = AL_TRUE;
        TrapALCError = AL_TRUE;
    }
    else
    {
        str = getenv("ALSOFT_TRAP_AL_ERROR");
        if(str && (strcasecmp(str, "true") == 0 || strtol(str, NULL, 0) == 1))
            TrapALError = AL_TRUE;
        TrapALError = GetConfigValueBool(NULL, NULL, "trap-al-error", TrapALError);

        str = getenv("ALSOFT_TRAP_ALC_ERROR");
        if(str && (strcasecmp(str, "true") == 0 || strtol(str, NULL, 0) == 1))
            TrapALCError = ALC_TRUE;
        TrapALCError = GetConfigValueBool(NULL, NULL, "trap-alc-error", TrapALCError);
    }

    if(ConfigValueFloat(NULL, "reverb", "boost", &valf))
        ReverbBoost *= powf(10.0f, valf / 20.0f);

    EmulateEAXReverb = GetConfigValueBool(NULL, "reverb", "emulate-eax", AL_FALSE);

    if(((devs=getenv("ALSOFT_DRIVERS")) && devs[0]) ||
       ConfigValueStr(NULL, NULL, "drivers", &devs))
    {
        int n;
        size_t len;
        const char *next = devs;
        int endlist, delitem;

        i = 0;
        do {
            devs = next;
            while(isspace(devs[0]))
                devs++;
            next = strchr(devs, ',');

            delitem = (devs[0] == '-');
            if(devs[0] == '-') devs++;

            if(!devs[0] || devs[0] == ',')
            {
                endlist = 0;
                continue;
            }
            endlist = 1;

            len = (next ? ((size_t)(next-devs)) : strlen(devs));
            while(len > 0 && isspace(devs[len-1]))
                len--;
            for(n = i;BackendList[n].name;n++)
            {
                if(len == strlen(BackendList[n].name) &&
                   strncmp(BackendList[n].name, devs, len) == 0)
                {
                    if(delitem)
                    {
                        do {
                            BackendList[n] = BackendList[n+1];
                            ++n;
                        } while(BackendList[n].name);
                    }
                    else
                    {
                        struct BackendInfo Bkp = BackendList[n];
                        while(n > i)
                        {
                            BackendList[n] = BackendList[n-1];
                            --n;
                        }
                        BackendList[n] = Bkp;

                        i++;
                    }
                    break;
                }
            }
        } while(next++);

        if(endlist)
        {
            BackendList[i].name = NULL;
            BackendList[i].getFactory = NULL;
            BackendList[i].Init = NULL;
            BackendList[i].Deinit = NULL;
            BackendList[i].Probe = NULL;
        }
    }

    for(i = 0;(BackendList[i].Init || BackendList[i].getFactory) && (!PlaybackBackend.name || !CaptureBackend.name);i++)
    {
        if(BackendList[i].getFactory)
        {
            ALCbackendFactory *factory = BackendList[i].getFactory();
            if(!V0(factory,init)())
            {
                WARN("Failed to initialize backend \"%s\"\n", BackendList[i].name);
                continue;
            }

            TRACE("Initialized backend \"%s\"\n", BackendList[i].name);
            if(!PlaybackBackend.name && V(factory,querySupport)(ALCbackend_Playback))
            {
                PlaybackBackend = BackendList[i];
                TRACE("Added \"%s\" for playback\n", PlaybackBackend.name);
            }
            if(!CaptureBackend.name && V(factory,querySupport)(ALCbackend_Capture))
            {
                CaptureBackend = BackendList[i];
                TRACE("Added \"%s\" for capture\n", CaptureBackend.name);
            }

            continue;
        }

        if(!BackendList[i].Init(&BackendList[i].Funcs))
        {
            WARN("Failed to initialize backend \"%s\"\n", BackendList[i].name);
            continue;
        }

        TRACE("Initialized backend \"%s\"\n", BackendList[i].name);
        if(BackendList[i].Funcs.OpenPlayback && !PlaybackBackend.name)
        {
            PlaybackBackend = BackendList[i];
            TRACE("Added \"%s\" for playback\n", PlaybackBackend.name);
        }
        if(BackendList[i].Funcs.OpenCapture && !CaptureBackend.name)
        {
            CaptureBackend = BackendList[i];
            TRACE("Added \"%s\" for capture\n", CaptureBackend.name);
        }
    }
    {
        ALCbackendFactory *factory = ALCloopbackFactory_getFactory();
        V0(factory,init)();
    }

    if(ConfigValueStr(NULL, NULL, "excludefx", &str))
    {
        size_t len;
        const char *next = str;

        do {
            str = next;
            next = strchr(str, ',');

            if(!str[0] || next == str)
                continue;

            len = (next ? ((size_t)(next-str)) : strlen(str));
            for(n = 0;EffectList[n].name;n++)
            {
                if(len == strlen(EffectList[n].name) &&
                   strncmp(EffectList[n].name, str, len) == 0)
                    DisabledEffects[EffectList[n].type] = AL_TRUE;
            }
        } while(next++);
    }

    InitEffectFactoryMap();

    InitEffect(&DefaultEffect);
    str = getenv("ALSOFT_DEFAULT_REVERB");
    if((str && str[0]) || ConfigValueStr(NULL, NULL, "default-reverb", &str))
        LoadReverbPreset(str, &DefaultEffect);
}
#define DO_INITCONFIG() alcall_once(&alc_config_once, alc_initconfig)


/************************************************
 * Library deinitialization
 ************************************************/
static void alc_cleanup(void)
{
    ALCdevice *dev;

    AL_STRING_DEINIT(alcAllDevicesList);
    AL_STRING_DEINIT(alcCaptureDeviceList);

    free(alcDefaultAllDevicesSpecifier);
    alcDefaultAllDevicesSpecifier = NULL;
    free(alcCaptureDefaultDeviceSpecifier);
    alcCaptureDefaultDeviceSpecifier = NULL;

    if((dev=ATOMIC_EXCHANGE(ALCdevice*, &DeviceList, NULL)) != NULL)
    {
        ALCuint num = 0;
        do {
            num++;
        } while((dev=dev->next) != NULL);
        //ERR("%u device%s not closed\n", num, (num>1)?"s":"");
    }

    DeinitEffectFactoryMap();
}

static void alc_deinit_safe(void)
{
    alc_cleanup();

    FreeHrtfs();
    FreeALConfig();

    ThunkExit();
    almtx_destroy(&ListLock);
    altss_delete(LocalContext);

    if(LogFile != stderr)
        fclose(LogFile);
    LogFile = NULL;
}

static void alc_deinit(void)
{
    int i;

    alc_cleanup();

    memset(&PlaybackBackend, 0, sizeof(PlaybackBackend));
    memset(&CaptureBackend, 0, sizeof(CaptureBackend));

    for(i = 0;BackendList[i].Deinit || BackendList[i].getFactory;i++)
    {
        if(!BackendList[i].getFactory)
            BackendList[i].Deinit();
        else
        {
            ALCbackendFactory *factory = BackendList[i].getFactory();
            V0(factory,deinit)();
        }
    }
    {
        ALCbackendFactory *factory = ALCloopbackFactory_getFactory();
        V0(factory,deinit)();
    }

    alc_deinit_safe();
}


/************************************************
 * Device enumeration
 ************************************************/
static void ProbeDevices(al_string *list, struct BackendInfo *backendinfo, enum DevProbe type)
{
    DO_INITCONFIG();

    LockLists();
    al_string_clear(list);

    if(!backendinfo->getFactory)
        backendinfo->Probe(type);
    else
    {
        ALCbackendFactory *factory = backendinfo->getFactory();
        V(factory,probe)(type);
    }

    UnlockLists();
}
static void ProbeAllDevicesList(void)
{ ProbeDevices(&alcAllDevicesList, &PlaybackBackend, ALL_DEVICE_PROBE); }
static void ProbeCaptureDeviceList(void)
{ ProbeDevices(&alcCaptureDeviceList, &CaptureBackend, CAPTURE_DEVICE_PROBE); }

static void AppendDevice(const ALCchar *name, al_string *devnames)
{
    size_t len = strlen(name);
    if(len > 0)
        al_string_append_range(devnames, name, name+len+1);
}
void AppendAllDevicesList(const ALCchar *name)
{ AppendDevice(name, &alcAllDevicesList); }
void AppendCaptureDeviceList(const ALCchar *name)
{ AppendDevice(name, &alcCaptureDeviceList); }


/************************************************
 * Device format information
 ************************************************/
const ALCchar *DevFmtTypeString(enum DevFmtType type)
{
    switch(type)
    {
    case DevFmtByte: return "Signed Byte";
    case DevFmtUByte: return "Unsigned Byte";
    case DevFmtShort: return "Signed Short";
    case DevFmtUShort: return "Unsigned Short";
    case DevFmtInt: return "Signed Int";
    case DevFmtUInt: return "Unsigned Int";
    case DevFmtFloat: return "Float";
    }
    return "(unknown type)";
}
const ALCchar *DevFmtChannelsString(enum DevFmtChannels chans)
{
    switch(chans)
    {
    case DevFmtMono: return "Mono";
    case DevFmtStereo: return "Stereo";
    case DevFmtQuad: return "Quadraphonic";
    case DevFmtX51: return "5.1 Surround";
    case DevFmtX51Rear: return "5.1 Surround (Rear)";
    case DevFmtX61: return "6.1 Surround";
    case DevFmtX71: return "7.1 Surround";
    case DevFmtBFormat3D: return "B-Format 3D";
    }
    return "(unknown channels)";
}

extern inline ALuint FrameSizeFromDevFmt(enum DevFmtChannels chans, enum DevFmtType type);
ALuint BytesFromDevFmt(enum DevFmtType type)
{
    switch(type)
    {
    case DevFmtByte: return sizeof(ALbyte);
    case DevFmtUByte: return sizeof(ALubyte);
    case DevFmtShort: return sizeof(ALshort);
    case DevFmtUShort: return sizeof(ALushort);
    case DevFmtInt: return sizeof(ALint);
    case DevFmtUInt: return sizeof(ALuint);
    case DevFmtFloat: return sizeof(ALfloat);
    }
    return 0;
}
ALuint ChannelsFromDevFmt(enum DevFmtChannels chans)
{
    switch(chans)
    {
    case DevFmtMono: return 1;
    case DevFmtStereo: return 2;
    case DevFmtQuad: return 4;
    case DevFmtX51: return 6;
    case DevFmtX51Rear: return 6;
    case DevFmtX61: return 7;
    case DevFmtX71: return 8;
    case DevFmtBFormat3D: return 4;
    }
    return 0;
}

DECL_CONST static ALboolean DecomposeDevFormat(ALenum format,
  enum DevFmtChannels *chans, enum DevFmtType *type)
{
    static const struct {
        ALenum format;
        enum DevFmtChannels channels;
        enum DevFmtType type;
    } list[] = {
        { AL_FORMAT_MONO8,        DevFmtMono, DevFmtUByte },
        { AL_FORMAT_MONO16,       DevFmtMono, DevFmtShort },
        { AL_FORMAT_MONO_FLOAT32, DevFmtMono, DevFmtFloat },

        { AL_FORMAT_STEREO8,        DevFmtStereo, DevFmtUByte },
        { AL_FORMAT_STEREO16,       DevFmtStereo, DevFmtShort },
        { AL_FORMAT_STEREO_FLOAT32, DevFmtStereo, DevFmtFloat },

        { AL_FORMAT_QUAD8,  DevFmtQuad, DevFmtUByte },
        { AL_FORMAT_QUAD16, DevFmtQuad, DevFmtShort },
        { AL_FORMAT_QUAD32, DevFmtQuad, DevFmtFloat },

        { AL_FORMAT_51CHN8,  DevFmtX51, DevFmtUByte },
        { AL_FORMAT_51CHN16, DevFmtX51, DevFmtShort },
        { AL_FORMAT_51CHN32, DevFmtX51, DevFmtFloat },

        { AL_FORMAT_61CHN8,  DevFmtX61, DevFmtUByte },
        { AL_FORMAT_61CHN16, DevFmtX61, DevFmtShort },
        { AL_FORMAT_61CHN32, DevFmtX61, DevFmtFloat },

        { AL_FORMAT_71CHN8,  DevFmtX71, DevFmtUByte },
        { AL_FORMAT_71CHN16, DevFmtX71, DevFmtShort },
        { AL_FORMAT_71CHN32, DevFmtX71, DevFmtFloat },
    };
    ALuint i;

    for(i = 0;i < COUNTOF(list);i++)
    {
        if(list[i].format == format)
        {
            *chans = list[i].channels;
            *type  = list[i].type;
            return AL_TRUE;
        }
    }

    return AL_FALSE;
}

DECL_CONST static ALCboolean IsValidALCType(ALCenum type)
{
    switch(type)
    {
        case ALC_BYTE_SOFT:
        case ALC_UNSIGNED_BYTE_SOFT:
        case ALC_SHORT_SOFT:
        case ALC_UNSIGNED_SHORT_SOFT:
        case ALC_INT_SOFT:
        case ALC_UNSIGNED_INT_SOFT:
        case ALC_FLOAT_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}

DECL_CONST static ALCboolean IsValidALCChannels(ALCenum channels)
{
    switch(channels)
    {
        case ALC_MONO_SOFT:
        case ALC_STEREO_SOFT:
        case ALC_QUAD_SOFT:
        case ALC_5POINT1_SOFT:
        case ALC_6POINT1_SOFT:
        case ALC_7POINT1_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}


/************************************************
 * Miscellaneous ALC helpers
 ************************************************/
enum HrtfRequestMode {
    Hrtf_Default = 0,
    Hrtf_Enable = 1,
    Hrtf_Disable = 2,
};

extern inline void LockContext(ALCcontext *context);
extern inline void UnlockContext(ALCcontext *context);

void ALCdevice_Lock(ALCdevice *device)
{
    V0(device->Backend,lock)();
}

void ALCdevice_Unlock(ALCdevice *device)
{
    V0(device->Backend,unlock)();
}


/* SetDefaultWFXChannelOrder
 *
 * Sets the default channel order used by WaveFormatEx.
 */
void SetDefaultWFXChannelOrder(ALCdevice *device)
{
    ALuint i;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        device->ChannelName[i] = InvalidChannel;

    switch(device->FmtChans)
    {
    case DevFmtMono:
        device->ChannelName[0] = FrontCenter;
        break;
    case DevFmtStereo:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        break;
    case DevFmtQuad:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = BackLeft;
        device->ChannelName[3] = BackRight;
        break;
    case DevFmtX51:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = FrontCenter;
        device->ChannelName[3] = LFE;
        device->ChannelName[4] = SideLeft;
        device->ChannelName[5] = SideRight;
        break;
    case DevFmtX51Rear:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = FrontCenter;
        device->ChannelName[3] = LFE;
        device->ChannelName[4] = BackLeft;
        device->ChannelName[5] = BackRight;
        break;
    case DevFmtX61:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = FrontCenter;
        device->ChannelName[3] = LFE;
        device->ChannelName[4] = BackCenter;
        device->ChannelName[5] = SideLeft;
        device->ChannelName[6] = SideRight;
        break;
    case DevFmtX71:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = FrontCenter;
        device->ChannelName[3] = LFE;
        device->ChannelName[4] = BackLeft;
        device->ChannelName[5] = BackRight;
        device->ChannelName[6] = SideLeft;
        device->ChannelName[7] = SideRight;
        break;
    case DevFmtBFormat3D:
        device->ChannelName[0] = BFormatW;
        device->ChannelName[1] = BFormatX;
        device->ChannelName[2] = BFormatY;
        device->ChannelName[3] = BFormatZ;
        break;
    }
}

/* SetDefaultChannelOrder
 *
 * Sets the default channel order used by most non-WaveFormatEx-based APIs.
 */
void SetDefaultChannelOrder(ALCdevice *device)
{
    ALuint i;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        device->ChannelName[i] = InvalidChannel;

    switch(device->FmtChans)
    {
    case DevFmtX51Rear:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = BackLeft;
        device->ChannelName[3] = BackRight;
        device->ChannelName[4] = FrontCenter;
        device->ChannelName[5] = LFE;
        return;
    case DevFmtX71:
        device->ChannelName[0] = FrontLeft;
        device->ChannelName[1] = FrontRight;
        device->ChannelName[2] = BackLeft;
        device->ChannelName[3] = BackRight;
        device->ChannelName[4] = FrontCenter;
        device->ChannelName[5] = LFE;
        device->ChannelName[6] = SideLeft;
        device->ChannelName[7] = SideRight;
        return;

    /* Same as WFX order */
    case DevFmtMono:
    case DevFmtStereo:
    case DevFmtQuad:
    case DevFmtX51:
    case DevFmtX61:
    case DevFmtBFormat3D:
        SetDefaultWFXChannelOrder(device);
        break;
    }
}

extern inline ALint GetChannelIdxByName(const ALCdevice *device, enum Channel chan);


/* ALCcontext_DeferUpdates
 *
 * Defers/suspends updates for the given context's listener and sources. This
 * does *NOT* stop mixing, but rather prevents certain property changes from
 * taking effect.
 */
void ALCcontext_DeferUpdates(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    FPUCtl oldMode;

    SetMixerFPUMode(&oldMode);

    V0(device->Backend,lock)();
    if(!context->DeferUpdates)
    {
        context->DeferUpdates = AL_TRUE;

        /* Make sure all pending updates are performed */
        UpdateContextSources(context);
#define UPDATE_SLOT(iter) do {                                   \
    if(ATOMIC_EXCHANGE(ALenum, &(*iter)->NeedsUpdate, AL_FALSE)) \
        V((*iter)->EffectState,update)(device, *iter);           \
} while(0)
        VECTOR_FOR_EACH(ALeffectslot*, context->ActiveAuxSlots, UPDATE_SLOT);
#undef UPDATE_SLOT
    }
    V0(device->Backend,unlock)();

    RestoreFPUMode(&oldMode);
}

/* ALCcontext_ProcessUpdates
 *
 * Resumes update processing after being deferred.
 */
void ALCcontext_ProcessUpdates(ALCcontext *context)
{
    ALCdevice *device = context->Device;

    V0(device->Backend,lock)();
    if(context->DeferUpdates)
    {
        ALsizei pos;

        context->DeferUpdates = AL_FALSE;

        LockUIntMapRead(&context->SourceMap);
        for(pos = 0;pos < context->SourceMap.size;pos++)
        {
            ALsource *Source = context->SourceMap.array[pos].value;
            ALenum new_state;

            if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
               Source->Offset >= 0.0)
            {
                WriteLock(&Source->queue_lock);
                ApplyOffset(Source);
                WriteUnlock(&Source->queue_lock);
            }

            new_state = Source->new_state;
            Source->new_state = AL_NONE;
            if(new_state)
                SetSourceState(Source, context, new_state);
        }
        UnlockUIntMapRead(&context->SourceMap);
    }
    V0(device->Backend,unlock)();
}


/* alcSetError
 *
 * Stores the latest ALC device error
 */
static void alcSetError(ALCdevice *device, ALCenum errorCode)
{
    if(TrapALCError)
    {
#ifdef _WIN32
        /* DebugBreak() will cause an exception if there is no debugger */
        if(IsDebuggerPresent())
            DebugBreak();
#elif defined(SIGTRAP)
        raise(SIGTRAP);
#endif
    }

    if(device)
        ATOMIC_STORE(&device->LastError, errorCode);
    else
        ATOMIC_STORE(&LastNullDeviceError, errorCode);
}


/* UpdateClockBase
 *
 * Updates the device's base clock time with however many samples have been
 * done. This is used so frequency changes on the device don't cause the time
 * to jump forward or back.
 */
static inline void UpdateClockBase(ALCdevice *device)
{
    device->ClockBase += device->SamplesDone * DEVICE_CLOCK_RES / device->Frequency;
    device->SamplesDone = 0;
}

/* UpdateDeviceParams
 *
 * Updates device parameters according to the attribute list (caller is
 * responsible for holding the list lock).
 */
static ALCenum UpdateDeviceParams(ALCdevice *device, const ALCint *attrList)
{
    ALCcontext *context;
    enum HrtfRequestMode hrtf_appreq = Hrtf_Default;
    enum HrtfRequestMode hrtf_userreq = Hrtf_Default;
    enum DevFmtChannels oldChans;
    enum DevFmtType oldType;
    ALCuint oldFreq;
    FPUCtl oldMode;
    ALCsizei hrtf_id = -1;
    size_t size;

    // Check for attributes
    if(device->Type == Loopback)
    {
        enum {
            GotFreq  = 1<<0,
            GotChans = 1<<1,
            GotType  = 1<<2,
            GotAll   = GotFreq|GotChans|GotType
        };
        ALCuint freq, numMono, numStereo, numSends;
        enum DevFmtChannels schans;
        enum DevFmtType stype;
        ALCuint attrIdx = 0;
        ALCint gotFmt = 0;

        if(!attrList)
        {
            WARN("Missing attributes for loopback device\n");
            return ALC_INVALID_VALUE;
        }

        numMono = device->NumMonoSources;
        numStereo = device->NumStereoSources;
        numSends = device->NumAuxSends;
        schans = device->FmtChans;
        stype = device->FmtType;
        freq = device->Frequency;

        while(attrList[attrIdx])
        {
            if(attrList[attrIdx] == ALC_FORMAT_CHANNELS_SOFT)
            {
                ALCint val = attrList[attrIdx + 1];
                if(!IsValidALCChannels(val) || !ChannelsFromDevFmt(val))
                    return ALC_INVALID_VALUE;
                schans = val;
                gotFmt |= GotChans;
            }

            if(attrList[attrIdx] == ALC_FORMAT_TYPE_SOFT)
            {
                ALCint val = attrList[attrIdx + 1];
                if(!IsValidALCType(val) || !BytesFromDevFmt(val))
                    return ALC_INVALID_VALUE;
                stype = val;
                gotFmt |= GotType;
            }

            if(attrList[attrIdx] == ALC_FREQUENCY)
            {
                freq = attrList[attrIdx + 1];
                if(freq < MIN_OUTPUT_RATE)
                    return ALC_INVALID_VALUE;
                gotFmt |= GotFreq;
            }

            if(attrList[attrIdx] == ALC_STEREO_SOURCES)
            {
                numStereo = attrList[attrIdx + 1];
                if(numStereo > device->MaxNoOfSources)
                    numStereo = device->MaxNoOfSources;

                numMono = device->MaxNoOfSources - numStereo;
            }

            if(attrList[attrIdx] == ALC_MAX_AUXILIARY_SENDS)
                numSends = attrList[attrIdx + 1];

            if(attrList[attrIdx] == ALC_HRTF_SOFT)
            {
                if(attrList[attrIdx + 1] == ALC_FALSE)
                    hrtf_appreq = Hrtf_Disable;
                else if(attrList[attrIdx + 1] == ALC_TRUE)
                    hrtf_appreq = Hrtf_Enable;
                else
                    hrtf_appreq = Hrtf_Default;
            }

            if(attrList[attrIdx] == ALC_HRTF_ID_SOFT)
                hrtf_id = attrList[attrIdx + 1];

            attrIdx += 2;
        }

        if(gotFmt != GotAll)
        {
            WARN("Missing format for loopback device\n");
            return ALC_INVALID_VALUE;
        }

        ConfigValueUInt(NULL, NULL, "sends", &numSends);
        numSends = minu(MAX_SENDS, numSends);

        if((device->Flags&DEVICE_RUNNING))
            V0(device->Backend,stop)();
        device->Flags &= ~DEVICE_RUNNING;

        UpdateClockBase(device);

        device->Frequency = freq;
        device->FmtChans = schans;
        device->FmtType = stype;
        device->NumMonoSources = numMono;
        device->NumStereoSources = numStereo;
        device->NumAuxSends = numSends;
    }
    else if(attrList && attrList[0])
    {
        ALCuint freq, numMono, numStereo, numSends;
        ALCuint attrIdx = 0;

        /* If a context is already running on the device, stop playback so the
         * device attributes can be updated. */
        if((device->Flags&DEVICE_RUNNING))
            V0(device->Backend,stop)();
        device->Flags &= ~DEVICE_RUNNING;

        freq = device->Frequency;
        numMono = device->NumMonoSources;
        numStereo = device->NumStereoSources;
        numSends = device->NumAuxSends;

        while(attrList[attrIdx])
        {
            if(attrList[attrIdx] == ALC_FREQUENCY)
            {
                freq = attrList[attrIdx + 1];
                device->Flags |= DEVICE_FREQUENCY_REQUEST;
            }

            if(attrList[attrIdx] == ALC_STEREO_SOURCES)
            {
                numStereo = attrList[attrIdx + 1];
                if(numStereo > device->MaxNoOfSources)
                    numStereo = device->MaxNoOfSources;

                numMono = device->MaxNoOfSources - numStereo;
            }

            if(attrList[attrIdx] == ALC_MAX_AUXILIARY_SENDS)
                numSends = attrList[attrIdx + 1];

            if(attrList[attrIdx] == ALC_HRTF_SOFT)
            {
                if(attrList[attrIdx + 1] == ALC_FALSE)
                    hrtf_appreq = Hrtf_Disable;
                else if(attrList[attrIdx + 1] == ALC_TRUE)
                    hrtf_appreq = Hrtf_Enable;
                else
                    hrtf_appreq = Hrtf_Default;
            }

            if(attrList[attrIdx] == ALC_HRTF_ID_SOFT)
                hrtf_id = attrList[attrIdx + 1];

            attrIdx += 2;
        }

        ConfigValueUInt(al_string_get_cstr(device->DeviceName), NULL, "frequency", &freq);
        freq = maxu(freq, MIN_OUTPUT_RATE);

        ConfigValueUInt(al_string_get_cstr(device->DeviceName), NULL, "sends", &numSends);
        numSends = minu(MAX_SENDS, numSends);

        UpdateClockBase(device);

        device->UpdateSize = (ALuint64)device->UpdateSize * freq /
                             device->Frequency;
        /* SSE and Neon do best with the update size being a multiple of 4 */
        if((CPUCapFlags&(CPU_CAP_SSE|CPU_CAP_NEON)) != 0)
            device->UpdateSize = (device->UpdateSize+3)&~3;

        device->Frequency = freq;
        device->NumMonoSources = numMono;
        device->NumStereoSources = numStereo;
        device->NumAuxSends = numSends;
    }

    if((device->Flags&DEVICE_RUNNING))
        return ALC_NO_ERROR;

    al_free(device->DryBuffer);
    device->DryBuffer = NULL;

    UpdateClockBase(device);

    device->Hrtf_Status = ALC_HRTF_DISABLED_SOFT;
    if(device->Type != Loopback)
    {
        const char *hrtf;
        if(ConfigValueStr(al_string_get_cstr(device->DeviceName), NULL, "hrtf", &hrtf))
        {
            if(strcasecmp(hrtf, "true") == 0)
                hrtf_userreq = Hrtf_Enable;
            else if(strcasecmp(hrtf, "false") == 0)
                hrtf_userreq = Hrtf_Disable;
            else if(strcasecmp(hrtf, "auto") != 0)
                ERR("Unexpected hrtf value: %s\n", hrtf);
        }

        if(hrtf_userreq == Hrtf_Enable || (hrtf_userreq != Hrtf_Disable && hrtf_appreq == Hrtf_Enable))
        {
            if(VECTOR_SIZE(device->Hrtf_List) == 0)
            {
                VECTOR_DEINIT(device->Hrtf_List);
                device->Hrtf_List = EnumerateHrtf(device->DeviceName);
            }
            if(VECTOR_SIZE(device->Hrtf_List) > 0)
            {
                device->FmtChans = DevFmtStereo;
                if(hrtf_id >= 0 && (size_t)hrtf_id < VECTOR_SIZE(device->Hrtf_List))
                    device->Frequency = GetHrtfSampleRate(VECTOR_ELEM(device->Hrtf_List, hrtf_id).hrtf);
                else
                    device->Frequency = GetHrtfSampleRate(VECTOR_ELEM(device->Hrtf_List, 0).hrtf);
                device->Flags |= DEVICE_CHANNELS_REQUEST | DEVICE_FREQUENCY_REQUEST;
            }
            else
            {
                hrtf_userreq = hrtf_appreq = Hrtf_Default;
                device->Hrtf_Status = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;
            }
        }
    }
    else if(hrtf_appreq == Hrtf_Enable)
    {
        size_t i;
        /* Loopback device. We don't need to match to a specific HRTF entry
         * here. If the requested ID matches, we'll pick that later, if not,
         * we'll try to auto-select one anyway. */
        if(device->FmtChans != DevFmtStereo)
            i = VECTOR_SIZE(device->Hrtf_List);
        else
        {
            if(VECTOR_SIZE(device->Hrtf_List) == 0)
            {
                VECTOR_DEINIT(device->Hrtf_List);
                device->Hrtf_List = EnumerateHrtf(device->DeviceName);
            }
            for(i = 0;i < VECTOR_SIZE(device->Hrtf_List);i++)
            {
                const struct Hrtf *hrtf = VECTOR_ELEM(device->Hrtf_List, i).hrtf;
                if(GetHrtfSampleRate(hrtf) == device->Frequency)
                    break;
            }
        }
        if(i == VECTOR_SIZE(device->Hrtf_List))
        {
            ERR("Requested format not HRTF compatible: %s, %uhz\n",
                DevFmtChannelsString(device->FmtChans), device->Frequency);
            hrtf_appreq = Hrtf_Default;
            device->Hrtf_Status = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;
        }
    }

    oldFreq  = device->Frequency;
    oldChans = device->FmtChans;
    oldType  = device->FmtType;

    TRACE("Pre-reset: %s%s, %s%s, %s%uhz, %u update size x%d\n",
        (device->Flags&DEVICE_CHANNELS_REQUEST)?"*":"", DevFmtChannelsString(device->FmtChans),
        (device->Flags&DEVICE_SAMPLE_TYPE_REQUEST)?"*":"", DevFmtTypeString(device->FmtType),
        (device->Flags&DEVICE_FREQUENCY_REQUEST)?"*":"", device->Frequency,
        device->UpdateSize, device->NumUpdates
    );

    if(V0(device->Backend,reset)() == ALC_FALSE)
        return ALC_INVALID_DEVICE;

    if(device->FmtChans != oldChans && (device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        ERR("Failed to set %s, got %s instead\n", DevFmtChannelsString(oldChans),
            DevFmtChannelsString(device->FmtChans));
        device->Flags &= ~DEVICE_CHANNELS_REQUEST;
    }
    if(device->FmtType != oldType && (device->Flags&DEVICE_SAMPLE_TYPE_REQUEST))
    {
        ERR("Failed to set %s, got %s instead\n", DevFmtTypeString(oldType),
            DevFmtTypeString(device->FmtType));
        device->Flags &= ~DEVICE_SAMPLE_TYPE_REQUEST;
    }
    if(device->Frequency != oldFreq && (device->Flags&DEVICE_FREQUENCY_REQUEST))
    {
        ERR("Failed to set %uhz, got %uhz instead\n", oldFreq, device->Frequency);
        device->Flags &= ~DEVICE_FREQUENCY_REQUEST;
    }

    TRACE("Post-reset: %s, %s, %uhz, %u update size x%d\n",
        DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
        device->Frequency, device->UpdateSize, device->NumUpdates
    );

    if((device->UpdateSize&3) != 0)
    {
        if((CPUCapFlags&CPU_CAP_SSE))
            WARN("SSE performs best with multiple of 4 update sizes (%u)\n", device->UpdateSize);
        if((CPUCapFlags&CPU_CAP_NEON))
            WARN("NEON performs best with multiple of 4 update sizes (%u)\n", device->UpdateSize);
    }

    device->Hrtf = NULL;
    device->Hrtf_Mode = DisabledHrtf;
    al_string_clear(&device->Hrtf_Name);
    if(device->FmtChans != DevFmtStereo)
    {
        if(hrtf_appreq == Hrtf_Enable)
            device->Hrtf_Status = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;

        free(device->Bs2b);
        device->Bs2b = NULL;
    }
    else
    {
        bool headphones = device->IsHeadphones;
        enum HrtfMode hrtf_mode = FullHrtf;
        ALCenum hrtf_status = device->Hrtf_Status;
        const char *mode;
        int bs2blevel;
        int usehrtf;

        if(device->Type != Loopback)
        {
            if(ConfigValueStr(al_string_get_cstr(device->DeviceName), NULL, "stereo-mode", &mode))
            {
                if(strcasecmp(mode, "headphones") == 0)
                    headphones = true;
                else if(strcasecmp(mode, "speakers") == 0)
                    headphones = false;
                else if(strcasecmp(mode, "auto") != 0)
                    ERR("Unexpected stereo-mode: %s\n", mode);
            }

            if(ConfigValueStr(al_string_get_cstr(device->DeviceName), NULL, "hrtf-mode", &mode))
            {
                if(strcasecmp(mode, "full") == 0)
                    hrtf_mode = FullHrtf;
                else if(strcasecmp(mode, "basic") == 0)
                    hrtf_mode = BasicHrtf;
                else
                    ERR("Unexpected hrtf-mode: %s\n", mode);
            }
        }


        if(hrtf_userreq == Hrtf_Default)
        {
            usehrtf = (headphones && hrtf_appreq != Hrtf_Disable) ||
                      (hrtf_appreq == Hrtf_Enable);
            if(headphones && hrtf_appreq != Hrtf_Disable)
                hrtf_status = ALC_HRTF_HEADPHONES_DETECTED_SOFT;
            else if(usehrtf)
                hrtf_status = ALC_HRTF_ENABLED_SOFT;
        }
        else
        {
            usehrtf = (hrtf_userreq == Hrtf_Enable);
            if(!usehrtf)
                hrtf_status = ALC_HRTF_DENIED_SOFT;
            else
                hrtf_status = ALC_HRTF_REQUIRED_SOFT;
        }

        if(!usehrtf)
            device->Hrtf_Status = hrtf_status;
        else
        {
            size_t i;

            device->Hrtf_Status = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;
            if(VECTOR_SIZE(device->Hrtf_List) == 0)
            {
                VECTOR_DEINIT(device->Hrtf_List);
                device->Hrtf_List = EnumerateHrtf(device->DeviceName);
            }

            if(hrtf_id >= 0 && (size_t)hrtf_id < VECTOR_SIZE(device->Hrtf_List))
            {
                const HrtfEntry *entry = &VECTOR_ELEM(device->Hrtf_List, hrtf_id);
                if(GetHrtfSampleRate(entry->hrtf) == device->Frequency)
                {
                    device->Hrtf = entry->hrtf;
                    al_string_copy(&device->Hrtf_Name, entry->name);
                }
            }
            if(!device->Hrtf)
            {
                for(i = 0;i < VECTOR_SIZE(device->Hrtf_List);i++)
                {
                    const HrtfEntry *entry = &VECTOR_ELEM(device->Hrtf_List, i);
                    if(GetHrtfSampleRate(entry->hrtf) == device->Frequency)
                    {
                        device->Hrtf = entry->hrtf;
                        al_string_copy(&device->Hrtf_Name, entry->name);
                        break;
                    }
                }
            }
        }
        if(device->Hrtf)
        {
            device->Hrtf_Mode = hrtf_mode;
            device->Hrtf_Status = hrtf_status;
            TRACE("HRTF enabled, \"%s\"\n", al_string_get_cstr(device->Hrtf_Name));
            free(device->Bs2b);
            device->Bs2b = NULL;
        }
        else
        {
            TRACE("HRTF disabled\n");

            bs2blevel = ((headphones && hrtf_appreq != Hrtf_Disable) ||
                         (hrtf_appreq == Hrtf_Enable)) ? 5 : 0;
            if(device->Type != Loopback)
                ConfigValueInt(al_string_get_cstr(device->DeviceName), NULL, "cf_level", &bs2blevel);
            if(bs2blevel > 0 && bs2blevel <= 6)
            {
                if(!device->Bs2b)
                {
                    device->Bs2b = calloc(1, sizeof(*device->Bs2b));
                    bs2b_clear(device->Bs2b);
                }
                bs2b_set_params(device->Bs2b, bs2blevel, device->Frequency);
                TRACE("BS2B enabled\n");
            }
            else
            {
                free(device->Bs2b);
                device->Bs2b = NULL;
                TRACE("BS2B disabled\n");
            }
        }
    }

    aluInitPanning(device);

    /* With HRTF, allocate two extra channels for the post-filter output. */
    size = sizeof(device->DryBuffer[0]) * (device->NumChannels + (device->Hrtf ? 2 : 0));
    device->DryBuffer = al_calloc(16, size);
    if(!device->DryBuffer)
    {
        ERR("Failed to allocate "SZFMT" bytes for mix buffer\n", size);
        return ALC_INVALID_DEVICE;
    }

    SetMixerFPUMode(&oldMode);
    V0(device->Backend,lock)();
    context = ATOMIC_LOAD(&device->ContextList);
    while(context)
    {
        ALsizei pos;

        ATOMIC_STORE(&context->UpdateSources, AL_FALSE);
        LockUIntMapRead(&context->EffectSlotMap);
        for(pos = 0;pos < context->EffectSlotMap.size;pos++)
        {
            ALeffectslot *slot = context->EffectSlotMap.array[pos].value;

            if(V(slot->EffectState,deviceUpdate)(device) == AL_FALSE)
            {
                UnlockUIntMapRead(&context->EffectSlotMap);
                V0(device->Backend,unlock)();
                RestoreFPUMode(&oldMode);
                return ALC_INVALID_DEVICE;
            }
            ATOMIC_STORE(&slot->NeedsUpdate, AL_FALSE);
            V(slot->EffectState,update)(device, slot);
        }
        UnlockUIntMapRead(&context->EffectSlotMap);

        LockUIntMapRead(&context->SourceMap);
        for(pos = 0;pos < context->SourceMap.size;pos++)
        {
            ALsource *source = context->SourceMap.array[pos].value;
            ALuint s = device->NumAuxSends;
            while(s < MAX_SENDS)
            {
                if(source->Send[s].Slot)
                    DecrementRef(&source->Send[s].Slot->ref);
                source->Send[s].Slot = NULL;
                source->Send[s].Gain = 1.0f;
                source->Send[s].GainHF = 1.0f;
                s++;
            }
            ATOMIC_STORE(&source->NeedsUpdate, AL_TRUE);
        }
        UnlockUIntMapRead(&context->SourceMap);

        for(pos = 0;pos < context->VoiceCount;pos++)
        {
            ALvoice *voice = &context->Voices[pos];
            ALsource *source = voice->Source;
            ALuint s = device->NumAuxSends;

            while(s < MAX_SENDS)
            {
                voice->Send[s].Moving = AL_FALSE;
                voice->Send[s].Counter = 0;
                s++;
            }

            if(source)
            {
                ATOMIC_STORE(&source->NeedsUpdate, AL_FALSE);
                voice->Update(voice, source, context);
            }
        }

        context = context->next;
    }
    if(device->DefaultSlot)
    {
        ALeffectslot *slot = device->DefaultSlot;

        if(V(slot->EffectState,deviceUpdate)(device) == AL_FALSE)
        {
            V0(device->Backend,unlock)();
            RestoreFPUMode(&oldMode);
            return ALC_INVALID_DEVICE;
        }
        ATOMIC_STORE(&slot->NeedsUpdate, AL_FALSE);
        V(slot->EffectState,update)(device, slot);
    }
    V0(device->Backend,unlock)();
    RestoreFPUMode(&oldMode);

    if(!(device->Flags&DEVICE_PAUSED))
    {
        if(V0(device->Backend,start)() == ALC_FALSE)
            return ALC_INVALID_DEVICE;
        device->Flags |= DEVICE_RUNNING;
    }

    return ALC_NO_ERROR;
}

/* FreeDevice
 *
 * Frees the device structure, and destroys any objects the app failed to
 * delete. Called once there's no more references on the device.
 */
static ALCvoid FreeDevice(ALCdevice *device)
{
    TRACE("%p\n", device);

    V0(device->Backend,close)();
    DELETE_OBJ(device->Backend);
    device->Backend = NULL;

    if(device->DefaultSlot)
    {
        ALeffectState *state = device->DefaultSlot->EffectState;
        device->DefaultSlot = NULL;
        DELETE_OBJ(state);
    }

    if(device->BufferMap.size > 0)
    {
        WARN("(%p) Deleting %d Buffer(s)\n", device, device->BufferMap.size);
        ReleaseALBuffers(device);
    }
    ResetUIntMap(&device->BufferMap);

    if(device->EffectMap.size > 0)
    {
        WARN("(%p) Deleting %d Effect(s)\n", device, device->EffectMap.size);
        ReleaseALEffects(device);
    }
    ResetUIntMap(&device->EffectMap);

    if(device->FilterMap.size > 0)
    {
        WARN("(%p) Deleting %d Filter(s)\n", device, device->FilterMap.size);
        ReleaseALFilters(device);
    }
    ResetUIntMap(&device->FilterMap);

    AL_STRING_DEINIT(device->Hrtf_Name);
    FreeHrtfList(&device->Hrtf_List);

    free(device->Bs2b);
    device->Bs2b = NULL;

    AL_STRING_DEINIT(device->DeviceName);

    al_free(device->DryBuffer);
    device->DryBuffer = NULL;

    al_free(device);
}


void ALCdevice_IncRef(ALCdevice *device)
{
    uint ref;
    ref = IncrementRef(&device->ref);
    TRACEREF("%p increasing refcount to %u\n", device, ref);
}

void ALCdevice_DecRef(ALCdevice *device)
{
    uint ref;
    ref = DecrementRef(&device->ref);
    TRACEREF("%p decreasing refcount to %u\n", device, ref);
    if(ref == 0) FreeDevice(device);
}

/* VerifyDevice
 *
 * Checks if the device handle is valid, and increments its ref count if so.
 */
static ALCboolean VerifyDevice(ALCdevice **device)
{
    ALCdevice *tmpDevice;

    LockLists();
    tmpDevice = ATOMIC_LOAD(&DeviceList);
    while(tmpDevice)
    {
        if(tmpDevice == *device)
        {
            ALCdevice_IncRef(tmpDevice);
            UnlockLists();
            return ALC_TRUE;
        }
        tmpDevice = tmpDevice->next;
    }
    UnlockLists();

    *device = NULL;
    return ALC_FALSE;
}


/* InitContext
 *
 * Initializes context fields
 */
static ALvoid InitContext(ALCcontext *Context)
{
    ALlistener *listener = Context->Listener;
    //Initialise listener
    listener->Gain = 1.0f;
    listener->MetersPerUnit = 1.0f;
    aluVectorSet(&listener->Position, 0.0f, 0.0f, 0.0f, 1.0f);
    aluVectorSet(&listener->Velocity, 0.0f, 0.0f, 0.0f, 0.0f);
    listener->Forward[0] = 0.0f;
    listener->Forward[1] = 0.0f;
    listener->Forward[2] = -1.0f;
    listener->Up[0] = 0.0f;
    listener->Up[1] = 1.0f;
    listener->Up[2] = 0.0f;
    aluMatrixdSet(&listener->Params.Matrix,
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    aluVectorSet(&listener->Params.Velocity, 0.0f, 0.0f, 0.0f, 0.0f);

    //Validate Context
    ATOMIC_INIT(&Context->LastError, AL_NO_ERROR);
    ATOMIC_INIT(&Context->UpdateSources, AL_FALSE);
    InitUIntMap(&Context->SourceMap, Context->Device->MaxNoOfSources);
    InitUIntMap(&Context->EffectSlotMap, Context->Device->AuxiliaryEffectSlotMax);

    //Set globals
    Context->DistanceModel = DefaultDistanceModel;
    Context->SourceDistanceModel = AL_FALSE;
    Context->DopplerFactor = 1.0f;
    Context->DopplerVelocity = 1.0f;
    Context->SpeedOfSound = SPEEDOFSOUNDMETRESPERSEC;
    Context->DeferUpdates = AL_FALSE;

    Context->ExtensionList = alExtList;
}


/* FreeContext
 *
 * Cleans up the context, and destroys any remaining objects the app failed to
 * delete. Called once there's no more references on the context.
 */
static void FreeContext(ALCcontext *context)
{
    TRACE("%p\n", context);

    if(context->SourceMap.size > 0)
    {
        WARN("(%p) Deleting %d Source(s)\n", context, context->SourceMap.size);
        ReleaseALSources(context);
    }
    ResetUIntMap(&context->SourceMap);

    if(context->EffectSlotMap.size > 0)
    {
        WARN("(%p) Deleting %d AuxiliaryEffectSlot(s)\n", context, context->EffectSlotMap.size);
        ReleaseALAuxiliaryEffectSlots(context);
    }
    ResetUIntMap(&context->EffectSlotMap);

    al_free(context->Voices);
    context->Voices = NULL;
    context->VoiceCount = 0;
    context->MaxVoices = 0;

    VECTOR_DEINIT(context->ActiveAuxSlots);

    ALCdevice_DecRef(context->Device);
    context->Device = NULL;

    //Invalidate context
    memset(context, 0, sizeof(ALCcontext));
    al_free(context);
}

/* ReleaseContext
 *
 * Removes the context reference from the given device and removes it from
 * being current on the running thread or globally.
 */
static void ReleaseContext(ALCcontext *context, ALCdevice *device)
{
    ALCcontext *nextctx;
    ALCcontext *origctx;

    if(altss_get(LocalContext) == context)
    {
        WARN("%p released while current on thread\n", context);
        altss_set(LocalContext, NULL);
        ALCcontext_DecRef(context);
    }

    origctx = context;
    if(ATOMIC_COMPARE_EXCHANGE_STRONG(ALCcontext*, &GlobalContext, &origctx, NULL))
        ALCcontext_DecRef(context);

    ALCdevice_Lock(device);
    origctx = context;
    nextctx = context->next;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(ALCcontext*, &device->ContextList, &origctx, nextctx))
    {
        ALCcontext *list;
        do {
            list = origctx;
            origctx = context;
        } while(!COMPARE_EXCHANGE(&list->next, &origctx, nextctx));
    }
    ALCdevice_Unlock(device);

    ALCcontext_DecRef(context);
}

void ALCcontext_IncRef(ALCcontext *context)
{
    uint ref;
    ref = IncrementRef(&context->ref);
    TRACEREF("%p increasing refcount to %u\n", context, ref);
}

void ALCcontext_DecRef(ALCcontext *context)
{
    uint ref;
    ref = DecrementRef(&context->ref);
    TRACEREF("%p decreasing refcount to %u\n", context, ref);
    if(ref == 0) FreeContext(context);
}

static void ReleaseThreadCtx(void *ptr)
{
    WARN("%p current for thread being destroyed\n", ptr);
    ALCcontext_DecRef(ptr);
}

/* VerifyContext
 *
 * Checks that the given context is valid, and increments its reference count.
 */
static ALCboolean VerifyContext(ALCcontext **context)
{
    ALCdevice *dev;

    LockLists();
    dev = ATOMIC_LOAD(&DeviceList);
    while(dev)
    {
        ALCcontext *ctx = ATOMIC_LOAD(&dev->ContextList);
        while(ctx)
        {
            if(ctx == *context)
            {
                ALCcontext_IncRef(ctx);
                UnlockLists();
                return ALC_TRUE;
            }
            ctx = ctx->next;
        }
        dev = dev->next;
    }
    UnlockLists();

    *context = NULL;
    return ALC_FALSE;
}


/* GetContextRef
 *
 * Returns the currently active context for this thread, and adds a reference
 * without locking it.
 */
ALCcontext *GetContextRef(void)
{
    ALCcontext *context;

    context = altss_get(LocalContext);
    if(context)
        ALCcontext_IncRef(context);
    else
    {
        LockLists();
        context = ATOMIC_LOAD(&GlobalContext);
        if(context)
            ALCcontext_IncRef(context);
        UnlockLists();
    }

    return context;
}


/************************************************
 * Standard ALC functions
 ************************************************/

/* alcGetError
 *
 * Return last ALC generated error code for the given device
*/
ALC_API ALCenum ALC_APIENTRY alcGetError(ALCdevice *device)
{
    ALCenum errorCode;

    if(VerifyDevice(&device))
    {
        errorCode = ATOMIC_EXCHANGE(ALCenum, &device->LastError, ALC_NO_ERROR);
        ALCdevice_DecRef(device);
    }
    else
        errorCode = ATOMIC_EXCHANGE(ALCenum, &LastNullDeviceError, ALC_NO_ERROR);

    return errorCode;
}


/* alcSuspendContext
 *
 * Suspends updates for the given context
 */
ALC_API ALCvoid ALC_APIENTRY alcSuspendContext(ALCcontext *context)
{
    if(!SuspendDefers)
        return;

    if(!VerifyContext(&context))
        alcSetError(NULL, ALC_INVALID_CONTEXT);
    else
    {
        ALCcontext_DeferUpdates(context);
        ALCcontext_DecRef(context);
    }
}

/* alcProcessContext
 *
 * Resumes processing updates for the given context
 */
ALC_API ALCvoid ALC_APIENTRY alcProcessContext(ALCcontext *context)
{
    if(!SuspendDefers)
        return;

    if(!VerifyContext(&context))
        alcSetError(NULL, ALC_INVALID_CONTEXT);
    else
    {
        ALCcontext_ProcessUpdates(context);
        ALCcontext_DecRef(context);
    }
}


/* alcGetString
 *
 * Returns information about the device, and error strings
 */
ALC_API const ALCchar* ALC_APIENTRY alcGetString(ALCdevice *Device, ALCenum param)
{
    const ALCchar *value = NULL;

    switch(param)
    {
    case ALC_NO_ERROR:
        value = alcNoError;
        break;

    case ALC_INVALID_ENUM:
        value = alcErrInvalidEnum;
        break;

    case ALC_INVALID_VALUE:
        value = alcErrInvalidValue;
        break;

    case ALC_INVALID_DEVICE:
        value = alcErrInvalidDevice;
        break;

    case ALC_INVALID_CONTEXT:
        value = alcErrInvalidContext;
        break;

    case ALC_OUT_OF_MEMORY:
        value = alcErrOutOfMemory;
        break;

    case ALC_DEVICE_SPECIFIER:
        value = alcDefaultName;
        break;

    case ALC_ALL_DEVICES_SPECIFIER:
        if(VerifyDevice(&Device))
        {
            value = al_string_get_cstr(Device->DeviceName);
            ALCdevice_DecRef(Device);
        }
        else
        {
            ProbeAllDevicesList();
            value = al_string_get_cstr(alcAllDevicesList);
        }
        break;

    case ALC_CAPTURE_DEVICE_SPECIFIER:
        if(VerifyDevice(&Device))
        {
            value = al_string_get_cstr(Device->DeviceName);
            ALCdevice_DecRef(Device);
        }
        else
        {
            ProbeCaptureDeviceList();
            value = al_string_get_cstr(alcCaptureDeviceList);
        }
        break;

    /* Default devices are always first in the list */
    case ALC_DEFAULT_DEVICE_SPECIFIER:
        value = alcDefaultName;
        break;

    case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
        if(al_string_empty(alcAllDevicesList))
            ProbeAllDevicesList();

        VerifyDevice(&Device);

        free(alcDefaultAllDevicesSpecifier);
        alcDefaultAllDevicesSpecifier = strdup(al_string_get_cstr(alcAllDevicesList));
        if(!alcDefaultAllDevicesSpecifier)
            alcSetError(Device, ALC_OUT_OF_MEMORY);

        value = alcDefaultAllDevicesSpecifier;
        if(Device) ALCdevice_DecRef(Device);
        break;

    case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
        if(al_string_empty(alcCaptureDeviceList))
            ProbeCaptureDeviceList();

        VerifyDevice(&Device);

        free(alcCaptureDefaultDeviceSpecifier);
        alcCaptureDefaultDeviceSpecifier = strdup(al_string_get_cstr(alcCaptureDeviceList));
        if(!alcCaptureDefaultDeviceSpecifier)
            alcSetError(Device, ALC_OUT_OF_MEMORY);

        value = alcCaptureDefaultDeviceSpecifier;
        if(Device) ALCdevice_DecRef(Device);
        break;

    case ALC_EXTENSIONS:
        if(!VerifyDevice(&Device))
            value = alcNoDeviceExtList;
        else
        {
            value = alcExtensionList;
            ALCdevice_DecRef(Device);
        }
        break;

    case ALC_HRTF_SPECIFIER_SOFT:
        if(!VerifyDevice(&Device))
            alcSetError(NULL, ALC_INVALID_DEVICE);
        else
        {
            LockLists();
            value = (Device->Hrtf ? al_string_get_cstr(Device->Hrtf_Name) : "");
            UnlockLists();
            ALCdevice_DecRef(Device);
        }
        break;

    default:
        VerifyDevice(&Device);
        alcSetError(Device, ALC_INVALID_ENUM);
        if(Device) ALCdevice_DecRef(Device);
        break;
    }

    return value;
}


static ALCsizei GetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
    ALCsizei i;

    if(size <= 0 || values == NULL)
    {
        alcSetError(device, ALC_INVALID_VALUE);
        return 0;
    }

    if(!device)
    {
        switch(param)
        {
            case ALC_MAJOR_VERSION:
                values[0] = alcMajorVersion;
                return 1;
            case ALC_MINOR_VERSION:
                values[0] = alcMinorVersion;
                return 1;

            case ALC_ATTRIBUTES_SIZE:
            case ALC_ALL_ATTRIBUTES:
            case ALC_FREQUENCY:
            case ALC_REFRESH:
            case ALC_SYNC:
            case ALC_MONO_SOURCES:
            case ALC_STEREO_SOURCES:
            case ALC_CAPTURE_SAMPLES:
            case ALC_FORMAT_CHANNELS_SOFT:
            case ALC_FORMAT_TYPE_SOFT:
                alcSetError(NULL, ALC_INVALID_DEVICE);
                return 0;

            default:
                alcSetError(NULL, ALC_INVALID_ENUM);
                return 0;
        }
        return 0;
    }

    if(device->Type == Capture)
    {
        switch(param)
        {
            case ALC_CAPTURE_SAMPLES:
                V0(device->Backend,lock)();
                values[0] = V0(device->Backend,availableSamples)();
                V0(device->Backend,unlock)();
                return 1;

            case ALC_CONNECTED:
                values[0] = device->Connected;
                return 1;

            default:
                alcSetError(device, ALC_INVALID_ENUM);
                return 0;
        }
        return 0;
    }

    /* render device */
    switch(param)
    {
        case ALC_MAJOR_VERSION:
            values[0] = alcMajorVersion;
            return 1;

        case ALC_MINOR_VERSION:
            values[0] = alcMinorVersion;
            return 1;

        case ALC_EFX_MAJOR_VERSION:
            values[0] = alcEFXMajorVersion;
            return 1;

        case ALC_EFX_MINOR_VERSION:
            values[0] = alcEFXMinorVersion;
            return 1;

        case ALC_ATTRIBUTES_SIZE:
            values[0] = 17;
            return 1;

        case ALC_ALL_ATTRIBUTES:
            if(size < 17)
            {
                alcSetError(device, ALC_INVALID_VALUE);
                return 0;
            }

            i = 0;
            values[i++] = ALC_FREQUENCY;
            values[i++] = device->Frequency;

            if(device->Type != Loopback)
            {
                values[i++] = ALC_REFRESH;
                values[i++] = device->Frequency / device->UpdateSize;

                values[i++] = ALC_SYNC;
                values[i++] = ALC_FALSE;
            }
            else
            {
                values[i++] = ALC_FORMAT_CHANNELS_SOFT;
                values[i++] = device->FmtChans;

                values[i++] = ALC_FORMAT_TYPE_SOFT;
                values[i++] = device->FmtType;
            }

            values[i++] = ALC_MONO_SOURCES;
            values[i++] = device->NumMonoSources;

            values[i++] = ALC_STEREO_SOURCES;
            values[i++] = device->NumStereoSources;

            values[i++] = ALC_MAX_AUXILIARY_SENDS;
            values[i++] = device->NumAuxSends;

            values[i++] = ALC_HRTF_SOFT;
            values[i++] = (device->Hrtf ? ALC_TRUE : ALC_FALSE);

            values[i++] = ALC_HRTF_STATUS_SOFT;
            values[i++] = device->Hrtf_Status;

            values[i++] = 0;
            return i;

        case ALC_FREQUENCY:
            values[0] = device->Frequency;
            return 1;

        case ALC_REFRESH:
            if(device->Type == Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->Frequency / device->UpdateSize;
            return 1;

        case ALC_SYNC:
            if(device->Type == Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = ALC_FALSE;
            return 1;

        case ALC_FORMAT_CHANNELS_SOFT:
            if(device->Type != Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->FmtChans;
            return 1;

        case ALC_FORMAT_TYPE_SOFT:
            if(device->Type != Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->FmtType;
            return 1;

        case ALC_MONO_SOURCES:
            values[0] = device->NumMonoSources;
            return 1;

        case ALC_STEREO_SOURCES:
            values[0] = device->NumStereoSources;
            return 1;

        case ALC_MAX_AUXILIARY_SENDS:
            values[0] = device->NumAuxSends;
            return 1;

        case ALC_CONNECTED:
            values[0] = device->Connected;
            return 1;

        case ALC_HRTF_SOFT:
            values[0] = (device->Hrtf ? ALC_TRUE : ALC_FALSE);
            return 1;

        case ALC_HRTF_STATUS_SOFT:
            values[0] = device->Hrtf_Status;
            return 1;

        case ALC_NUM_HRTF_SPECIFIERS_SOFT:
            FreeHrtfList(&device->Hrtf_List);
            device->Hrtf_List = EnumerateHrtf(device->DeviceName);
            values[0] = (ALCint)VECTOR_SIZE(device->Hrtf_List);
            return 1;

        default:
            alcSetError(device, ALC_INVALID_ENUM);
            return 0;
    }
    return 0;
}

/* alcGetIntegerv
 *
 * Returns information about the device and the version of OpenAL
 */
ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
    VerifyDevice(&device);
    if(size <= 0 || values == NULL)
        alcSetError(device, ALC_INVALID_VALUE);
    else
        GetIntegerv(device, param, size, values);
    if(device) ALCdevice_DecRef(device);
}

ALC_API void ALC_APIENTRY alcGetInteger64vSOFT(ALCdevice *device, ALCenum pname, ALCsizei size, ALCint64SOFT *values)
{
    ALCint *ivals;
    ALsizei i;

    VerifyDevice(&device);
    if(size <= 0 || values == NULL)
        alcSetError(device, ALC_INVALID_VALUE);
    else if(!device || device->Type == Capture)
    {
        ivals = malloc(size * sizeof(ALCint));
        size = GetIntegerv(device, pname, size, ivals);
        for(i = 0;i < size;i++)
            values[i] = ivals[i];
        free(ivals);
    }
    else /* render device */
    {
        switch(pname)
        {
            case ALC_ATTRIBUTES_SIZE:
                *values = 19;
                break;

            case ALC_ALL_ATTRIBUTES:
                if(size < 19)
                    alcSetError(device, ALC_INVALID_VALUE);
                else
                {
                    int i = 0;

                    V0(device->Backend,lock)();
                    values[i++] = ALC_FREQUENCY;
                    values[i++] = device->Frequency;

                    if(device->Type != Loopback)
                    {
                        values[i++] = ALC_REFRESH;
                        values[i++] = device->Frequency / device->UpdateSize;

                        values[i++] = ALC_SYNC;
                        values[i++] = ALC_FALSE;
                    }
                    else
                    {
                        values[i++] = ALC_FORMAT_CHANNELS_SOFT;
                        values[i++] = device->FmtChans;

                        values[i++] = ALC_FORMAT_TYPE_SOFT;
                        values[i++] = device->FmtType;
                    }

                    values[i++] = ALC_MONO_SOURCES;
                    values[i++] = device->NumMonoSources;

                    values[i++] = ALC_STEREO_SOURCES;
                    values[i++] = device->NumStereoSources;

                    values[i++] = ALC_MAX_AUXILIARY_SENDS;
                    values[i++] = device->NumAuxSends;

                    values[i++] = ALC_HRTF_SOFT;
                    values[i++] = (device->Hrtf ? ALC_TRUE : ALC_FALSE);

                    values[i++] = ALC_HRTF_STATUS_SOFT;
                    values[i++] = device->Hrtf_Status;

                    values[i++] = ALC_DEVICE_CLOCK_SOFT;
                    values[i++] = device->ClockBase +
                                  (device->SamplesDone * DEVICE_CLOCK_RES / device->Frequency);

                    values[i++] = 0;
                    V0(device->Backend,unlock)();
                }
                break;

            case ALC_DEVICE_CLOCK_SOFT:
                V0(device->Backend,lock)();
                *values = device->ClockBase +
                          (device->SamplesDone * DEVICE_CLOCK_RES / device->Frequency);
                V0(device->Backend,unlock)();
                break;

            default:
                ivals = malloc(size * sizeof(ALCint));
                size = GetIntegerv(device, pname, size, ivals);
                for(i = 0;i < size;i++)
                    values[i] = ivals[i];
                free(ivals);
                break;
        }
    }
    if(device)
        ALCdevice_DecRef(device);
}


/* alcIsExtensionPresent
 *
 * Determines if there is support for a particular extension
 */
ALC_API ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extName)
{
    ALCboolean bResult = ALC_FALSE;

    VerifyDevice(&device);

    if(!extName)
        alcSetError(device, ALC_INVALID_VALUE);
    else
    {
        size_t len = strlen(extName);
        const char *ptr = (device ? alcExtensionList : alcNoDeviceExtList);
        while(ptr && *ptr)
        {
            if(strncasecmp(ptr, extName, len) == 0 &&
               (ptr[len] == '\0' || isspace(ptr[len])))
            {
                bResult = ALC_TRUE;
                break;
            }
            if((ptr=strchr(ptr, ' ')) != NULL)
            {
                do {
                    ++ptr;
                } while(isspace(*ptr));
            }
        }
    }
    if(device)
        ALCdevice_DecRef(device);
    return bResult;
}


/* alcGetProcAddress
 *
 * Retrieves the function address for a particular extension function
 */
ALC_API ALCvoid* ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcName)
{
    ALCvoid *ptr = NULL;

    if(!funcName)
    {
        VerifyDevice(&device);
        alcSetError(device, ALC_INVALID_VALUE);
        if(device) ALCdevice_DecRef(device);
    }
    else
    {
        ALsizei i = 0;
        while(alcFunctions[i].funcName && strcmp(alcFunctions[i].funcName, funcName) != 0)
            i++;
        ptr = alcFunctions[i].address;
    }

    return ptr;
}


/* alcGetEnumValue
 *
 * Get the value for a particular ALC enumeration name
 */
ALC_API ALCenum ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumName)
{
    ALCenum val = 0;

    if(!enumName)
    {
        VerifyDevice(&device);
        alcSetError(device, ALC_INVALID_VALUE);
        if(device) ALCdevice_DecRef(device);
    }
    else
    {
        ALsizei i = 0;
        while(enumeration[i].enumName && strcmp(enumeration[i].enumName, enumName) != 0)
            i++;
        val = enumeration[i].value;
    }

    return val;
}


/* alcCreateContext
 *
 * Create and attach a context to the given device.
 */
ALC_API ALCcontext* ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrList)
{
    ALCcontext *ALContext;
    ALCenum err;

    LockLists();
    if(!VerifyDevice(&device) || device->Type == Capture || !device->Connected)
    {
        UnlockLists();
        alcSetError(device, ALC_INVALID_DEVICE);
        if(device) ALCdevice_DecRef(device);
        return NULL;
    }

    ATOMIC_STORE(&device->LastError, ALC_NO_ERROR);

    if((err=UpdateDeviceParams(device, attrList)) != ALC_NO_ERROR)
    {
        UnlockLists();
        alcSetError(device, err);
        if(err == ALC_INVALID_DEVICE)
        {
            V0(device->Backend,lock)();
            aluHandleDisconnect(device);
            V0(device->Backend,unlock)();
        }
        ALCdevice_DecRef(device);
        return NULL;
    }

    ALContext = al_calloc(16, sizeof(ALCcontext)+sizeof(ALlistener));
    if(ALContext)
    {
        InitRef(&ALContext->ref, 1);
        ALContext->Listener = (ALlistener*)ALContext->_listener_mem;

        VECTOR_INIT(ALContext->ActiveAuxSlots);

        ALContext->VoiceCount = 0;
        ALContext->MaxVoices = 256;
        ALContext->Voices = al_calloc(16, ALContext->MaxVoices * sizeof(ALContext->Voices[0]));
    }
    if(!ALContext || !ALContext->Voices)
    {
        if(!ATOMIC_LOAD(&device->ContextList))
        {
            V0(device->Backend,stop)();
            device->Flags &= ~DEVICE_RUNNING;
        }
        UnlockLists();

        if(ALContext)
        {
            al_free(ALContext->Voices);
            ALContext->Voices = NULL;

            VECTOR_DEINIT(ALContext->ActiveAuxSlots);

            al_free(ALContext);
            ALContext = NULL;
        }

        alcSetError(device, ALC_OUT_OF_MEMORY);
        ALCdevice_DecRef(device);
        return NULL;
    }

    ALContext->Device = device;
    ALCdevice_IncRef(device);
    InitContext(ALContext);

    {
        ALCcontext *head = ATOMIC_LOAD(&device->ContextList);
        do {
            ALContext->next = head;
        } while(!ATOMIC_COMPARE_EXCHANGE_WEAK(ALCcontext*, &device->ContextList, &head, ALContext));
    }
    UnlockLists();

    ALCdevice_DecRef(device);

    TRACE("Created context %p\n", ALContext);
    return ALContext;
}

/* alcDestroyContext
 *
 * Remove a context from its device
 */
ALC_API ALCvoid ALC_APIENTRY alcDestroyContext(ALCcontext *context)
{
    ALCdevice *Device;

    LockLists();
    /* alcGetContextsDevice sets an error for invalid contexts */
    Device = alcGetContextsDevice(context);
    if(Device)
    {
        ReleaseContext(context, Device);
        if(!ATOMIC_LOAD(&Device->ContextList))
        {
            V0(Device->Backend,stop)();
            Device->Flags &= ~DEVICE_RUNNING;
        }
    }
    UnlockLists();
}


/* alcGetCurrentContext
 *
 * Returns the currently active context on the calling thread
 */
ALC_API ALCcontext* ALC_APIENTRY alcGetCurrentContext(void)
{
    ALCcontext *Context = altss_get(LocalContext);
    if(!Context) Context = ATOMIC_LOAD(&GlobalContext);
    return Context;
}

/* alcGetThreadContext
 *
 * Returns the currently active thread-local context
 */
ALC_API ALCcontext* ALC_APIENTRY alcGetThreadContext(void)
{
    return altss_get(LocalContext);
}


/* alcMakeContextCurrent
 *
 * Makes the given context the active process-wide context, and removes the
 * thread-local context for the calling thread.
 */
ALC_API ALCboolean ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context)
{
    /* context must be valid or NULL */
    if(context && !VerifyContext(&context))
    {
        alcSetError(NULL, ALC_INVALID_CONTEXT);
        return ALC_FALSE;
    }
    /* context's reference count is already incremented */
    context = ATOMIC_EXCHANGE(ALCcontext*, &GlobalContext, context);
    if(context) ALCcontext_DecRef(context);

    if((context=altss_get(LocalContext)) != NULL)
    {
        altss_set(LocalContext, NULL);
        ALCcontext_DecRef(context);
    }

    return ALC_TRUE;
}

/* alcSetThreadContext
 *
 * Makes the given context the active context for the current thread
 */
ALC_API ALCboolean ALC_APIENTRY alcSetThreadContext(ALCcontext *context)
{
    ALCcontext *old;

    /* context must be valid or NULL */
    if(context && !VerifyContext(&context))
    {
        alcSetError(NULL, ALC_INVALID_CONTEXT);
        return ALC_FALSE;
    }
    /* context's reference count is already incremented */
    old = altss_get(LocalContext);
    altss_set(LocalContext, context);
    if(old) ALCcontext_DecRef(old);

    return ALC_TRUE;
}


/* alcGetContextsDevice
 *
 * Returns the device that a particular context is attached to
 */
ALC_API ALCdevice* ALC_APIENTRY alcGetContextsDevice(ALCcontext *Context)
{
    ALCdevice *Device;

    if(!VerifyContext(&Context))
    {
        alcSetError(NULL, ALC_INVALID_CONTEXT);
        return NULL;
    }
    Device = Context->Device;
    ALCcontext_DecRef(Context);

    return Device;
}


/* alcOpenDevice
 *
 * Opens the named device.
 */
ALC_API ALCdevice* ALC_APIENTRY alcOpenDevice(const ALCchar *deviceName)
{
    const ALCchar *fmt;
    ALCdevice *device;
    ALCenum err;

    DO_INITCONFIG();

    if(!PlaybackBackend.name)
    {
        alcSetError(NULL, ALC_INVALID_VALUE);
        return NULL;
    }

    if(deviceName && (!deviceName[0] || strcasecmp(deviceName, alcDefaultName) == 0 || strcasecmp(deviceName, "openal-soft") == 0
#ifdef _WIN32
        /* Some old Windows apps hardcode these expecting OpenAL to use a
         * specific audio API, even when they're not enumerated. Creative's
         * router effectively ignores them too.
         */
        || strcasecmp(deviceName, "DirectSound3D") == 0 || strcasecmp(deviceName, "DirectSound") == 0
        || strcasecmp(deviceName, "MMSYSTEM") == 0
#endif
    ))
        deviceName = NULL;

    device = al_calloc(16, sizeof(ALCdevice)+sizeof(ALeffectslot));
    if(!device)
    {
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }

    //Validate device
    InitRef(&device->ref, 1);
    device->Connected = ALC_TRUE;
    device->Type = Playback;
    ATOMIC_INIT(&device->LastError, ALC_NO_ERROR);

    device->Flags = 0;
    device->Bs2b = NULL;
    VECTOR_INIT(device->Hrtf_List);
    AL_STRING_INIT(device->Hrtf_Name);
    device->Hrtf_Mode = DisabledHrtf;
    AL_STRING_INIT(device->DeviceName);
    device->DryBuffer = NULL;

    ATOMIC_INIT(&device->ContextList, NULL);

    device->ClockBase = 0;
    device->SamplesDone = 0;

    device->MaxNoOfSources = 256;
    device->AuxiliaryEffectSlotMax = 4;
    device->NumAuxSends = MAX_SENDS;

    InitUIntMap(&device->BufferMap, ~0);
    InitUIntMap(&device->EffectMap, ~0);
    InitUIntMap(&device->FilterMap, ~0);

    //Set output format
    device->FmtChans = DevFmtChannelsDefault;
    device->FmtType = DevFmtTypeDefault;
    device->Frequency = DEFAULT_OUTPUT_RATE;
    device->IsHeadphones = AL_FALSE;
    device->NumUpdates = 4;
    device->UpdateSize = 1024;

    if(!PlaybackBackend.getFactory)
        device->Backend = create_backend_wrapper(device, &PlaybackBackend.Funcs,
                                                 ALCbackend_Playback);
    else
    {
        ALCbackendFactory *factory = PlaybackBackend.getFactory();
        device->Backend = V(factory,createBackend)(device, ALCbackend_Playback);
    }
    if(!device->Backend)
    {
        al_free(device);
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }


    if(ConfigValueStr(deviceName, NULL, "channels", &fmt))
    {
        static const struct {
            const char name[16];
            enum DevFmtChannels chans;
        } chanlist[] = {
            { "mono",       DevFmtMono   },
            { "stereo",     DevFmtStereo },
            { "quad",       DevFmtQuad   },
            { "surround51", DevFmtX51    },
            { "surround61", DevFmtX61    },
            { "surround71", DevFmtX71    },
            { "surround51rear", DevFmtX51Rear },
        };
        size_t i;

        for(i = 0;i < COUNTOF(chanlist);i++)
        {
            if(strcasecmp(chanlist[i].name, fmt) == 0)
            {
                device->FmtChans = chanlist[i].chans;
                device->Flags |= DEVICE_CHANNELS_REQUEST;
                break;
            }
        }
        if(i == COUNTOF(chanlist))
            ERR("Unsupported channels: %s\n", fmt);
    }
    if(ConfigValueStr(deviceName, NULL, "sample-type", &fmt))
    {
        static const struct {
            const char name[16];
            enum DevFmtType type;
        } typelist[] = {
            { "int8",    DevFmtByte   },
            { "uint8",   DevFmtUByte  },
            { "int16",   DevFmtShort  },
            { "uint16",  DevFmtUShort },
            { "int32",   DevFmtInt    },
            { "uint32",  DevFmtUInt   },
            { "float32", DevFmtFloat  },
        };
        size_t i;

        for(i = 0;i < COUNTOF(typelist);i++)
        {
            if(strcasecmp(typelist[i].name, fmt) == 0)
            {
                device->FmtType = typelist[i].type;
                device->Flags |= DEVICE_SAMPLE_TYPE_REQUEST;
                break;
            }
        }
        if(i == COUNTOF(typelist))
            ERR("Unsupported sample-type: %s\n", fmt);
    }

    if(ConfigValueUInt(deviceName, NULL, "frequency", &device->Frequency))
    {
        device->Flags |= DEVICE_FREQUENCY_REQUEST;
        if(device->Frequency < MIN_OUTPUT_RATE)
            ERR("%uhz request clamped to %uhz minimum\n", device->Frequency, MIN_OUTPUT_RATE);
        device->Frequency = maxu(device->Frequency, MIN_OUTPUT_RATE);
    }

    ConfigValueUInt(deviceName, NULL, "periods", &device->NumUpdates);
    device->NumUpdates = clampu(device->NumUpdates, 2, 16);

    ConfigValueUInt(deviceName, NULL, "period_size", &device->UpdateSize);
    device->UpdateSize = clampu(device->UpdateSize, 64, 8192);
    if((CPUCapFlags&(CPU_CAP_SSE|CPU_CAP_NEON)) != 0)
        device->UpdateSize = (device->UpdateSize+3)&~3;

    ConfigValueUInt(deviceName, NULL, "sources", &device->MaxNoOfSources);
    if(device->MaxNoOfSources == 0) device->MaxNoOfSources = 256;

    ConfigValueUInt(deviceName, NULL, "slots", &device->AuxiliaryEffectSlotMax);
    if(device->AuxiliaryEffectSlotMax == 0) device->AuxiliaryEffectSlotMax = 4;

    ConfigValueUInt(deviceName, NULL, "sends", &device->NumAuxSends);
    if(device->NumAuxSends > MAX_SENDS) device->NumAuxSends = MAX_SENDS;

    device->NumStereoSources = 1;
    device->NumMonoSources = device->MaxNoOfSources - device->NumStereoSources;

    // Find a playback device to open
    if((err=V(device->Backend,open)(deviceName)) != ALC_NO_ERROR)
    {
        DELETE_OBJ(device->Backend);
        al_free(device);
        alcSetError(NULL, err);
        return NULL;
    }

    if(DefaultEffect.type != AL_EFFECT_NULL)
    {
        device->DefaultSlot = (ALeffectslot*)device->_slot_mem;
        if(InitEffectSlot(device->DefaultSlot) != AL_NO_ERROR)
        {
            device->DefaultSlot = NULL;
            ERR("Failed to initialize the default effect slot\n");
        }
        else if(InitializeEffect(device, device->DefaultSlot, &DefaultEffect) != AL_NO_ERROR)
        {
            ALeffectState *state = device->DefaultSlot->EffectState;
            device->DefaultSlot = NULL;
            DELETE_OBJ(state);
            ERR("Failed to initialize the default effect\n");
        }
    }

    {
        ALCdevice *head = ATOMIC_LOAD(&DeviceList);
        do {
            device->next = head;
        } while(!ATOMIC_COMPARE_EXCHANGE_WEAK(ALCdevice*, &DeviceList, &head, device));
    }

    TRACE("Created device %p, \"%s\"\n", device, al_string_get_cstr(device->DeviceName));
    return device;
}

/* alcCloseDevice
 *
 * Closes the given device.
 */
ALC_API ALCboolean ALC_APIENTRY alcCloseDevice(ALCdevice *device)
{
    ALCdevice *list, *origdev, *nextdev;
    ALCcontext *ctx;

    LockLists();
    list = ATOMIC_LOAD(&DeviceList);
    do {
        if(list == device)
            break;
    } while((list=list->next) != NULL);
    if(!list || list->Type == Capture)
    {
        alcSetError(list, ALC_INVALID_DEVICE);
        UnlockLists();
        return ALC_FALSE;
    }

    origdev = device;
    nextdev = device->next;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(ALCdevice*, &DeviceList, &origdev, nextdev))
    {
        do {
            list = origdev;
            origdev = device;
        } while(!COMPARE_EXCHANGE(&list->next, &origdev, nextdev));
    }
    UnlockLists();

    ctx = ATOMIC_LOAD(&device->ContextList);
    while(ctx != NULL)
    {
        ALCcontext *next = ctx->next;
        WARN("Releasing context %p\n", ctx);
        ReleaseContext(ctx, device);
        ctx = next;
    }
    if((device->Flags&DEVICE_RUNNING))
        V0(device->Backend,stop)();
    device->Flags &= ~DEVICE_RUNNING;

    ALCdevice_DecRef(device);

    return ALC_TRUE;
}


/************************************************
 * ALC capture functions
 ************************************************/
ALC_API ALCdevice* ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei samples)
{
    ALCdevice *device = NULL;
    ALCenum err;

    DO_INITCONFIG();

    if(!CaptureBackend.name)
    {
        alcSetError(NULL, ALC_INVALID_VALUE);
        return NULL;
    }

    if(samples <= 0)
    {
        alcSetError(NULL, ALC_INVALID_VALUE);
        return NULL;
    }

    if(deviceName && (!deviceName[0] || strcasecmp(deviceName, alcDefaultName) == 0 || strcasecmp(deviceName, "openal-soft") == 0))
        deviceName = NULL;

    device = al_calloc(16, sizeof(ALCdevice));
    if(!device)
    {
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }

    //Validate device
    InitRef(&device->ref, 1);
    device->Connected = ALC_TRUE;
    device->Type = Capture;

    VECTOR_INIT(device->Hrtf_List);
    AL_STRING_INIT(device->Hrtf_Name);

    AL_STRING_INIT(device->DeviceName);
    device->DryBuffer = NULL;

    InitUIntMap(&device->BufferMap, ~0);
    InitUIntMap(&device->EffectMap, ~0);
    InitUIntMap(&device->FilterMap, ~0);

    if(!CaptureBackend.getFactory)
        device->Backend = create_backend_wrapper(device, &CaptureBackend.Funcs,
                                                 ALCbackend_Capture);
    else
    {
        ALCbackendFactory *factory = CaptureBackend.getFactory();
        device->Backend = V(factory,createBackend)(device, ALCbackend_Capture);
    }
    if(!device->Backend)
    {
        al_free(device);
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }

    device->Flags |= DEVICE_FREQUENCY_REQUEST;
    device->Frequency = frequency;

    device->Flags |= DEVICE_CHANNELS_REQUEST | DEVICE_SAMPLE_TYPE_REQUEST;
    if(DecomposeDevFormat(format, &device->FmtChans, &device->FmtType) == AL_FALSE)
    {
        al_free(device);
        alcSetError(NULL, ALC_INVALID_ENUM);
        return NULL;
    }
    device->IsHeadphones = AL_FALSE;

    device->UpdateSize = samples;
    device->NumUpdates = 1;

    if((err=V(device->Backend,open)(deviceName)) != ALC_NO_ERROR)
    {
        al_free(device);
        alcSetError(NULL, err);
        return NULL;
    }

    {
        ALCdevice *head = ATOMIC_LOAD(&DeviceList);
        do {
            device->next = head;
        } while(!ATOMIC_COMPARE_EXCHANGE_WEAK(ALCdevice*, &DeviceList, &head, device));
    }

    TRACE("Created device %p, \"%s\"\n", device, al_string_get_cstr(device->DeviceName));
    return device;
}

ALC_API ALCboolean ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device)
{
    ALCdevice *list, *next, *nextdev;

    LockLists();
    list = ATOMIC_LOAD(&DeviceList);
    do {
        if(list == device)
            break;
    } while((list=list->next) != NULL);
    if(!list || list->Type != Capture)
    {
        alcSetError(list, ALC_INVALID_DEVICE);
        UnlockLists();
        return ALC_FALSE;
    }

    next = device;
    nextdev = device->next;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(ALCdevice*, &DeviceList, &next, nextdev))
    {
        do {
            list = next;
            next = device;
        } while(!COMPARE_EXCHANGE(&list->next, &next, nextdev));
    }
    UnlockLists();

    ALCdevice_DecRef(device);

    return ALC_TRUE;
}

ALC_API void ALC_APIENTRY alcCaptureStart(ALCdevice *device)
{
    if(!VerifyDevice(&device) || device->Type != Capture)
        alcSetError(device, ALC_INVALID_DEVICE);
    else
    {
        V0(device->Backend,lock)();
        if(!device->Connected)
            alcSetError(device, ALC_INVALID_DEVICE);
        else if(!(device->Flags&DEVICE_RUNNING))
        {
            if(V0(device->Backend,start)())
                device->Flags |= DEVICE_RUNNING;
            else
            {
                aluHandleDisconnect(device);
                alcSetError(device, ALC_INVALID_DEVICE);
            }
        }
        V0(device->Backend,unlock)();
    }

    if(device) ALCdevice_DecRef(device);
}

ALC_API void ALC_APIENTRY alcCaptureStop(ALCdevice *device)
{
    if(!VerifyDevice(&device) || device->Type != Capture)
        alcSetError(device, ALC_INVALID_DEVICE);
    else
    {
        V0(device->Backend,lock)();
        if((device->Flags&DEVICE_RUNNING))
            V0(device->Backend,stop)();
        device->Flags &= ~DEVICE_RUNNING;
        V0(device->Backend,unlock)();
    }

    if(device) ALCdevice_DecRef(device);
}

ALC_API void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
{
    if(!VerifyDevice(&device) || device->Type != Capture)
        alcSetError(device, ALC_INVALID_DEVICE);
    else
    {
        ALCenum err = ALC_INVALID_VALUE;

        V0(device->Backend,lock)();
        if(samples >= 0 && V0(device->Backend,availableSamples)() >= (ALCuint)samples)
            err = V(device->Backend,captureSamples)(buffer, samples);
        V0(device->Backend,unlock)();

        if(err != ALC_NO_ERROR)
            alcSetError(device, err);
    }
    if(device) ALCdevice_DecRef(device);
}


/************************************************
 * ALC loopback functions
 ************************************************/

/* alcLoopbackOpenDeviceSOFT
 *
 * Open a loopback device, for manual rendering.
 */
ALC_API ALCdevice* ALC_APIENTRY alcLoopbackOpenDeviceSOFT(const ALCchar *deviceName)
{
    ALCbackendFactory *factory;
    ALCdevice *device;

    DO_INITCONFIG();

    /* Make sure the device name, if specified, is us. */
    if(deviceName && strcmp(deviceName, alcDefaultName) != 0)
    {
        alcSetError(NULL, ALC_INVALID_VALUE);
        return NULL;
    }

    device = al_calloc(16, sizeof(ALCdevice));
    if(!device)
    {
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }

    //Validate device
    InitRef(&device->ref, 1);
    device->Connected = ALC_TRUE;
    device->Type = Loopback;
    ATOMIC_INIT(&device->LastError, ALC_NO_ERROR);

    device->Flags = 0;
    VECTOR_INIT(device->Hrtf_List);
    AL_STRING_INIT(device->Hrtf_Name);
    device->Bs2b = NULL;
    device->Hrtf_Mode = DisabledHrtf;
    AL_STRING_INIT(device->DeviceName);
    device->DryBuffer = NULL;

    ATOMIC_INIT(&device->ContextList, NULL);

    device->ClockBase = 0;
    device->SamplesDone = 0;

    device->MaxNoOfSources = 256;
    device->AuxiliaryEffectSlotMax = 4;
    device->NumAuxSends = MAX_SENDS;

    InitUIntMap(&device->BufferMap, ~0);
    InitUIntMap(&device->EffectMap, ~0);
    InitUIntMap(&device->FilterMap, ~0);

    factory = ALCloopbackFactory_getFactory();
    device->Backend = V(factory,createBackend)(device, ALCbackend_Loopback);
    if(!device->Backend)
    {
        al_free(device);
        alcSetError(NULL, ALC_OUT_OF_MEMORY);
        return NULL;
    }

    //Set output format
    device->NumUpdates = 0;
    device->UpdateSize = 0;

    device->Frequency = DEFAULT_OUTPUT_RATE;
    device->FmtChans = DevFmtChannelsDefault;
    device->FmtType = DevFmtTypeDefault;
    device->IsHeadphones = AL_FALSE;

    ConfigValueUInt(NULL, NULL, "sources", &device->MaxNoOfSources);
    if(device->MaxNoOfSources == 0) device->MaxNoOfSources = 256;

    ConfigValueUInt(NULL, NULL, "slots", &device->AuxiliaryEffectSlotMax);
    if(device->AuxiliaryEffectSlotMax == 0) device->AuxiliaryEffectSlotMax = 4;

    ConfigValueUInt(NULL, NULL, "sends", &device->NumAuxSends);
    if(device->NumAuxSends > MAX_SENDS) device->NumAuxSends = MAX_SENDS;

    device->NumStereoSources = 1;
    device->NumMonoSources = device->MaxNoOfSources - device->NumStereoSources;

    // Open the "backend"
    V(device->Backend,open)("Loopback");

    {
        ALCdevice *head = ATOMIC_LOAD(&DeviceList);
        do {
            device->next = head;
        } while(!ATOMIC_COMPARE_EXCHANGE_WEAK(ALCdevice*, &DeviceList, &head, device));
    }

    TRACE("Created device %p\n", device);
    return device;
}

/* alcIsRenderFormatSupportedSOFT
 *
 * Determines if the loopback device supports the given format for rendering.
 */
ALC_API ALCboolean ALC_APIENTRY alcIsRenderFormatSupportedSOFT(ALCdevice *device, ALCsizei freq, ALCenum channels, ALCenum type)
{
    ALCboolean ret = ALC_FALSE;

    if(!VerifyDevice(&device) || device->Type != Loopback)
        alcSetError(device, ALC_INVALID_DEVICE);
    else if(freq <= 0)
        alcSetError(device, ALC_INVALID_VALUE);
    else
    {
        if(IsValidALCType(type) && BytesFromDevFmt(type) > 0 &&
           IsValidALCChannels(channels) && ChannelsFromDevFmt(channels) > 0 &&
           freq >= MIN_OUTPUT_RATE)
            ret = ALC_TRUE;
    }
    if(device) ALCdevice_DecRef(device);

    return ret;
}

/* alcRenderSamplesSOFT
 *
 * Renders some samples into a buffer, using the format last set by the
 * attributes given to alcCreateContext.
 */
FORCE_ALIGN ALC_API void ALC_APIENTRY alcRenderSamplesSOFT(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
{
    if(!VerifyDevice(&device) || device->Type != Loopback)
        alcSetError(device, ALC_INVALID_DEVICE);
    else if(samples < 0 || (samples > 0 && buffer == NULL))
        alcSetError(device, ALC_INVALID_VALUE);
    else
        aluMixData(device, buffer, samples);
    if(device) ALCdevice_DecRef(device);
}


/************************************************
 * ALC DSP pause/resume functions
 ************************************************/

/* alcDevicePauseSOFT
 *
 * Pause the DSP to stop audio processing.
 */
ALC_API void ALC_APIENTRY alcDevicePauseSOFT(ALCdevice *device)
{
    if(!VerifyDevice(&device) || device->Type != Playback)
        alcSetError(device, ALC_INVALID_DEVICE);
    else
    {
        LockLists();
        if((device->Flags&DEVICE_RUNNING))
            V0(device->Backend,stop)();
        device->Flags &= ~DEVICE_RUNNING;
        device->Flags |= DEVICE_PAUSED;
        UnlockLists();
    }
    if(device) ALCdevice_DecRef(device);
}

/* alcDeviceResumeSOFT
 *
 * Resume the DSP to restart audio processing.
 */
ALC_API void ALC_APIENTRY alcDeviceResumeSOFT(ALCdevice *device)
{
    if(!VerifyDevice(&device) || device->Type != Playback)
        alcSetError(device, ALC_INVALID_DEVICE);
    else
    {
        LockLists();
        if((device->Flags&DEVICE_PAUSED))
        {
            device->Flags &= ~DEVICE_PAUSED;
            if(ATOMIC_LOAD(&device->ContextList) != NULL)
            {
                if(V0(device->Backend,start)() != ALC_FALSE)
                    device->Flags |= DEVICE_RUNNING;
                else
                {
                    alcSetError(device, ALC_INVALID_DEVICE);
                    V0(device->Backend,lock)();
                    aluHandleDisconnect(device);
                    V0(device->Backend,unlock)();
                }
            }
        }
        UnlockLists();
    }
    if(device) ALCdevice_DecRef(device);
}


/************************************************
 * ALC HRTF functions
 ************************************************/

/* alcGetStringiSOFT
 *
 * Gets a string parameter at the given index.
 */
ALC_API const ALCchar* ALC_APIENTRY alcGetStringiSOFT(ALCdevice *device, ALCenum paramName, ALCsizei index)
{
    const ALCchar *str = NULL;

    if(!VerifyDevice(&device) || device->Type == Capture)
        alcSetError(device, ALC_INVALID_DEVICE);
    else switch(paramName)
    {
        case ALC_HRTF_SPECIFIER_SOFT:
            if(index >= 0 && (size_t)index < VECTOR_SIZE(device->Hrtf_List))
                str = al_string_get_cstr(VECTOR_ELEM(device->Hrtf_List, index).name);
            else
                alcSetError(device, ALC_INVALID_VALUE);
            break;

        default:
            alcSetError(device, ALC_INVALID_ENUM);
            break;
    }
    if(device) ALCdevice_DecRef(device);

    return str;
}

/* alcResetDeviceSOFT
 *
 * Resets the given device output, using the specified attribute list.
 */
ALC_API ALCboolean ALC_APIENTRY alcResetDeviceSOFT(ALCdevice *device, const ALCint *attribs)
{
    ALCenum err;

    LockLists();
    if(!VerifyDevice(&device) || device->Type == Capture || !device->Connected)
    {
        UnlockLists();
        alcSetError(device, ALC_INVALID_DEVICE);
        if(device) ALCdevice_DecRef(device);
        return ALC_FALSE;
    }

    if((err=UpdateDeviceParams(device, attribs)) != ALC_NO_ERROR)
    {
        UnlockLists();
        alcSetError(device, err);
        if(err == ALC_INVALID_DEVICE)
        {
            V0(device->Backend,lock)();
            aluHandleDisconnect(device);
            V0(device->Backend,unlock)();
        }
        ALCdevice_DecRef(device);
        return ALC_FALSE;
    }
    UnlockLists();
    ALCdevice_DecRef(device);

    return ALC_TRUE;
}
