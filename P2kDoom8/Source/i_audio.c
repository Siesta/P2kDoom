/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *  Copyright 2023-2025 by
 *  Frenkel Smeijers
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  System interface for sound.
 *
 *-----------------------------------------------------------------------------
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "z_zone.h"

#include "m_swap.h"
#include "i_sound.h"
#include "w_wad.h"
#include "s_sound.h"

#include "doomdef.h"
#include "d_player.h"
#include "doomtype.h"

#include "d_main.h"

#include "m_fixed.h"

#include "a_pcfx.h"

#include "globdata.h"

#if defined(P2K)
#include <dl.h>
#include <mme.h>
#endif


#define MAX_CHANNELS    3


#if !defined(P2K)
static int16_t firstsfx;
#endif

#if defined(P2K)

static int16_t p2k_sfx_lumps[NUMSFX];
static boolean p2k_audio_volume_ready;
static boolean p2k_audio_route_primed;
#if defined(P2K_AUDIO_BUFFER_PROBE)
static boolean p2k_audio_buffer_probe_done;
#endif
#if defined(P2K_AUDIO_TONE_PROBE)
static boolean p2k_audio_tone_probe_played;
#endif

static const char p2k_sfx_names[NUMSFX][9] =
{
	"",
	"DPPISTOL",
	"DPSHOTGN",
	"DPSGCOCK",
	"DPSAWUP",
	"DPSAWIDL",
	"DPSAWFUL",
	"DPSAWHIT",
	"DPRLAUNC",
	"DPRXPLOD",
	"DPFIRSHT",
	"DPFIRXPL",
	"DPPSTART",
	"DPPSTOP",
	"DPDOROPN",
	"DPDORCLS",
	"DPSTNMOV",
	"DPSWTCHN",
	"DPSWTCHX",
	"DPPLPAIN",
	"DPDMPAIN",
	"DPPOPAIN",
	"DPSLOP",
	"DPITEMUP",
	"DPWPNUP",
	"DPOOF",
	"DPTELEPT",
	"DPPOSIT1",
	"DPPOSIT2",
	"DPPOSIT3",
	"DPBGSIT1",
	"DPBGSIT2",
	"DPSGTSIT",
	"DPBRSSIT",
	"DPSGTATK",
	"DPCLAW",
	"DPPLDETH",
	"DPPDIEHI",
	"DPPODTH1",
	"DPPODTH2",
	"DPPODTH3",
	"DPBGDTH1",
	"DPBGDTH2",
	"DPSGTDTH",
	"DPBRSDTH",
	"DPPOSACT",
	"DPBGACT",
	"DPDMACT",
	"DPNOWAY",
	"DPBAREXP",
	"DPPUNCH",
	"DPTINK",
	"DPGETPOW"
};

#define P2K_AUDIO_RATE             22050u
#define P2K_AUDIO_BYTES_PER_SAMPLE 1u
#define P2K_PC_TICRATE             140u
#define P2K_PC_PIT_CLOCK           1193182u
#define P2K_MAX_PC_TICKS           146u
#define P2K_WAV_HEADER_SIZE        44u
#define P2K_WAV_MAX_SAMPLES        (((P2K_AUDIO_RATE * P2K_MAX_PC_TICKS) + P2K_PC_TICRATE - 1u) / P2K_PC_TICRATE)
#define P2K_WAV_MAX_DATA_SIZE      (P2K_WAV_MAX_SAMPLES * P2K_AUDIO_BYTES_PER_SAMPLE)
#define P2K_WAV_BUFFER_SIZE        (P2K_WAV_HEADER_SIZE + P2K_WAV_MAX_DATA_SIZE)
#define P2K_GC_EXTRA_LIFE_TICS     140u
#define P2K_GC_OPEN_TIMEOUT_TICS   350u

typedef struct
{
	MME_GC_MEDIA_FILE media_handle;
	UINT32 iface_handle;
	uint32_t wav_size;
	uint16_t life_tics;
	uint16_t play_tics;
	uint8_t active;
	uint8_t started;
	uint16_t wav_align_pad;
	uint8_t wav[P2K_WAV_BUFFER_SIZE];
} p2k_audio_channel_t;

extern IFACE_DATA_T g_p2k_iface_data;

static p2k_audio_channel_t p2k_audio_channels[MAX_CHANNELS];

static void P2K_Clear(void *ptr, uint32_t size)
{
	uint8_t *bytes = (uint8_t *)ptr;

	while (size--)
		*bytes++ = 0;
}

static void P2K_WriteLe16(uint8_t *ptr, uint16_t value)
{
	ptr[0] = (uint8_t)(value & 0xffu);
	ptr[1] = (uint8_t)(value >> 8);
}

static void P2K_WriteLe32(uint8_t *ptr, uint32_t value)
{
	ptr[0] = (uint8_t)(value & 0xffu);
	ptr[1] = (uint8_t)((value >> 8) & 0xffu);
	ptr[2] = (uint8_t)((value >> 16) & 0xffu);
	ptr[3] = (uint8_t)(value >> 24);
}

static void P2K_WriteWavHeader(uint8_t *wav, uint32_t data_size)
{
	wav[0] = 'R';
	wav[1] = 'I';
	wav[2] = 'F';
	wav[3] = 'F';
	P2K_WriteLe32(&wav[4], 36u + data_size);
	wav[8] = 'W';
	wav[9] = 'A';
	wav[10] = 'V';
	wav[11] = 'E';
	wav[12] = 'f';
	wav[13] = 'm';
	wav[14] = 't';
	wav[15] = ' ';
	P2K_WriteLe32(&wav[16], 16u);
	P2K_WriteLe16(&wav[20], 1u);
	P2K_WriteLe16(&wav[22], 1u);
	P2K_WriteLe32(&wav[24], P2K_AUDIO_RATE);
	P2K_WriteLe32(&wav[28], P2K_AUDIO_RATE * P2K_AUDIO_BYTES_PER_SAMPLE);
	P2K_WriteLe16(&wav[32], P2K_AUDIO_BYTES_PER_SAMPLE);
	P2K_WriteLe16(&wav[34], P2K_AUDIO_BYTES_PER_SAMPLE * 8u);
	wav[36] = 'd';
	wav[37] = 'a';
	wav[38] = 't';
	wav[39] = 'a';
	P2K_WriteLe32(&wav[40], data_size);
}

static uint32_t P2K_SynthPcSpeakerWav(uint8_t *wav, const uint16_t *pc_data, uint16_t pc_length, int16_t volume)
{
	uint32_t data_size = 0;
	uint32_t tick_remainder = 0;
	uint32_t phase = 0;
	uint16_t tick;
	uint8_t high = 0;
	uint16_t amp;

	if (pc_length > P2K_MAX_PC_TICKS)
		pc_length = P2K_MAX_PC_TICKS;

	if (volume < 0)
		volume = 0;
	if (volume > 127)
		volume = 127;

#if P2K_AUDIO_BYTES_PER_SAMPLE == 1u
	amp = (uint16_t)((volume * 30000) / 127);
#else
	amp = (uint16_t)((volume * 12000) / 127);
#endif
	P2K_WriteWavHeader(wav, 0);

	for (tick = 0; tick < pc_length; tick++)
	{
		const uint16_t divisor = SHORT(pc_data[tick]);
		uint32_t samples = P2K_AUDIO_RATE / P2K_PC_TICRATE;
		uint32_t phase_step = 0;
		uint32_t i;

		tick_remainder += P2K_AUDIO_RATE % P2K_PC_TICRATE;
		if (tick_remainder >= P2K_PC_TICRATE)
		{
			samples++;
			tick_remainder -= P2K_PC_TICRATE;
		}

		if (divisor)
		{
			phase_step = (P2K_PC_PIT_CLOCK * 2u) / divisor;
			if (!phase_step)
				phase_step = 1;
		}
		else
		{
			phase = 0;
			high = 0;
		}

		for (i = 0; i < samples && data_size + P2K_AUDIO_BYTES_PER_SAMPLE <= P2K_WAV_MAX_DATA_SIZE; i++)
		{
			int16_t sample = 0;

			if (divisor)
			{
				phase += phase_step;
				while (phase >= P2K_AUDIO_RATE)
				{
					phase -= P2K_AUDIO_RATE;
					high = !high;
				}

				sample = high ? (int16_t)amp : -(int16_t)amp;
			}

#if P2K_AUDIO_BYTES_PER_SAMPLE == 1u
			wav[P2K_WAV_HEADER_SIZE + data_size] = (uint8_t)(128 + (sample / 256));
			data_size += P2K_AUDIO_BYTES_PER_SAMPLE;
#else
			P2K_WriteLe16(&wav[P2K_WAV_HEADER_SIZE + data_size], (uint16_t)sample);
			data_size += P2K_AUDIO_BYTES_PER_SAMPLE;
#endif
		}
	}

	P2K_WriteWavHeader(wav, data_size);

	return P2K_WAV_HEADER_SIZE + data_size;
}

static void P2K_EnsureAudioVolume(void)
{
	UINT8 volume = 0;

	if (p2k_audio_volume_ready)
		return;

	DL_AudGetVolumeSetting(MULTIMEDIA, &volume);
	LOG("P2K audio: multimedia volume before=%d\n", volume);

	if (volume < 7)
	{
		DL_AudSetVolumeSetting(MULTIMEDIA, 7);
		DL_AudGetVolumeSetting(MULTIMEDIA, &volume);
		LOG("P2K audio: multimedia volume after=%d\n", volume);
	}

	p2k_audio_volume_ready = true;
}

static void P2K_PrimeAudioRoute(void)
{
	UINT32 seq;

	if (p2k_audio_route_primed)
		return;

	p2k_audio_route_primed = true;
	seq = DL_AudPlayTone(3, 0);
	LOG("P2K audio: route prime tone=3 volume=0 seq=%d\n", seq);
}

static int16_t P2K_FindChannelByHandle(MME_GC_MEDIA_FILE media_handle, UINT32 iface_handle)
{
	int16_t i;

	if (!media_handle)
		return -1;

	for (i = 0; i < MAX_CHANNELS; i++)
	{
		if (p2k_audio_channels[i].media_handle == media_handle &&
				(!iface_handle || p2k_audio_channels[i].iface_handle == iface_handle))
			return i;
	}

	return -1;
}

static UINT32 P2K_GetMediaHandleSeq(MME_GC_MEDIA_FILE media_handle)
{
	const UINT32 *words = (const UINT32 *)media_handle;

	if (!media_handle)
		return 0;

	return words[2];
}

static void P2K_CloseChannel(int16_t channel, boolean stop_first)
{
	p2k_audio_channel_t *audio = &p2k_audio_channels[channel];
	UINT32 status;

	if (!audio->active && !audio->media_handle)
		return;

	if (audio->media_handle)
	{
		LOG("P2K audio: gc close channel=%d handle=0x%08X iface_handle=0x%08X stop=%d started=%d\n",
				channel, (UINT32)audio->media_handle, audio->iface_handle, stop_first, audio->started);

		if (stop_first && audio->started)
		{
			status = MME_FW_gc_handle_stop(audio->media_handle);
			LOG("P2K audio: gc stop done channel=%d handle=0x%08X status=%d\n",
					channel, (UINT32)audio->media_handle, status);
		}

		status = MME_FW_gc_handle_close(audio->media_handle);
		LOG("P2K audio: gc close done channel=%d handle=0x%08X status=%d\n",
				channel, (UINT32)audio->media_handle, status);
	}

	audio->media_handle = NULL;
	audio->iface_handle = 0;
	audio->wav_size = 0;
	audio->life_tics = 0;
	audio->play_tics = 0;
	audio->active = false;
	audio->started = false;
}

static void P2K_StopChannel(int16_t channel)
{
	P2K_CloseChannel(channel, true);
}

#if defined(P2K_AUDIO_TONE_PROBE)
static void P2K_PlayToneProbe(void)
{
	UINT32 seq;

	if (p2k_audio_tone_probe_played)
		return;

	p2k_audio_tone_probe_played = true;
	seq = DL_AudPlayTone(3, 7);
	LOG("P2K audio: DL_AudPlayTone probe tone=3 volume=7 seq=%d\n", seq);
}
#endif

#if defined(P2K_AUDIO_BUFFER_PROBE)
static void P2K_ProbeAudioBuffer(p2k_audio_channel_t *audio, const char *stage)
{
	UINT16 buffer[256];
	UINT32 checksum = 0;
	UINT16 min = 0xffffu;
	UINT16 max = 0;
	UINT16 i;

	if (p2k_audio_buffer_probe_done || !audio)
		return;

	p2k_audio_buffer_probe_done = true;
	P2K_Clear(buffer, sizeof(buffer));
	LOG("P2K audio: buffer probe begin stage=%s frames=%d\n", stage, 128);

	if (!MME_BAE_get_audio_buffer(MME_BAE_get_system_id(), buffer, 128))
	{
		LOG("P2K audio: buffer probe failed stage=%s\n", stage);
		return;
	}

	for (i = 0; i < (sizeof(buffer) / sizeof(buffer[0])); i++)
	{
		if (buffer[i] < min)
			min = buffer[i];
		if (buffer[i] > max)
			max = buffer[i];
		checksum += buffer[i];
	}

	LOG("P2K audio: buffer probe done stage=%s min=0x%04X max=0x%04X checksum=0x%08X first=0x%04X\n",
			stage, min, max, checksum, buffer[0]);
}
#endif

static int16_t P2K_PlayWav(int16_t channel)
{
	p2k_audio_channel_t *audio = &p2k_audio_channels[channel];
	MMSS_MEDIA_DETAILS_T media_details;

	P2K_EnsureAudioVolume();
	P2K_PrimeAudioRoute();
#if defined(P2K_AUDIO_TONE_PROBE)
	P2K_PlayToneProbe();
#endif

	P2K_Clear(&media_details, sizeof(media_details));
	media_details.offset = 0;
	media_details.size = audio->wav_size;
	media_details.media_path = NULL;
	media_details.format = MMSS_WAV;
	media_details.mime_type = MMSS_MIME_TYPE_AUDIO_WAV;
	media_details.object = NULL;
	media_details.mem_ptr = audio->wav;
	media_details.shutter_tone = false;
	media_details.st_handle = NULL;
	media_details.cataloging_mode = false;
	media_details.fh = 0;

	LOG("P2K audio: mbae create begin iface_port=0x%08X iface_handle=0x%08X wav=0x%08X wav_align=%d wav_size=%d format=%d mime=%d details_size=%d\n",
			g_p2k_iface_data.port, g_p2k_iface_data.handle, (UINT32)audio->wav,
			((UINT32)audio->wav) & 3u, audio->wav_size, media_details.format, media_details.mime_type,
			(UINT32)sizeof(media_details));

	audio->media_handle = MME_GC_mBAE_playback_create(&g_p2k_iface_data, NULL, &media_details);
	audio->iface_handle = P2K_GetMediaHandleSeq(audio->media_handle);
	if (!audio->media_handle)
	{
		LOG("P2K audio: mbae create failed, wav_size=%d\n", audio->wav_size);
		return -1;
	}

	audio->active = true;
	audio->started = false;

	LOG("P2K audio: mbae create done handle=0x%08X iface_handle=0x%08X active=%d\n",
			(UINT32)audio->media_handle, audio->iface_handle, audio->active);

#if defined(P2K_AUDIO_BUFFER_PROBE)
	P2K_ProbeAudioBuffer(audio, "after-create");
#endif

	return channel;
}

UINT32 I_HandleSoundEvent(EVENT_STACK_T *ev_st, APPLICATION_T *app)
{
	EVENT_T *event = AFW_GetEv(ev_st);
	void *data = event ? event->attachment : NULL;
	EVENT_CODE_T code = event ? event->code : AFW_GetEvCode(ev_st);
	MME_GC_MEDIA_FILE media_handle = NULL;
	UINT32 iface_handle = 0;
	int16_t channel;

	switch (code)
	{
		case DL_MMSP_MEDIA_OPEN_SUCCESS:
			if (data)
			{
				DL_MMSP_MEDIA_OPEN_SUCCESS_DATA_T *msg = (DL_MMSP_MEDIA_OPEN_SUCCESS_DATA_T *)data;
				media_handle = msg->media_handle;
				iface_handle = msg->iface_data.handle;
			}
			channel = P2K_FindChannelByHandle(media_handle, iface_handle);
			LOG("P2K audio: event open success handle=0x%08X iface_handle=0x%08X channel=%d\n",
					(UINT32)media_handle, iface_handle, channel);
			if (channel >= 0 && !p2k_audio_channels[channel].started)
			{
				UINT8 gc_volume = 100;
				UINT32 status = MME_FW_gc_handle_set_attribute(media_handle, PLAYBACK_AUDIO_VOLUME, &gc_volume);
				LOG("P2K audio: gc volume set channel=%d handle=0x%08X volume=%d status=%d\n",
						channel, (UINT32)media_handle, gc_volume, status);

				status = MME_FW_gc_handle_start(media_handle, NULL);
				p2k_audio_channels[channel].started = (status == MMSS_NO_ERROR);
				if (p2k_audio_channels[channel].started)
					p2k_audio_channels[channel].life_tics = p2k_audio_channels[channel].play_tics;
				LOG("P2K audio: gc start done channel=%d handle=0x%08X status=%d started=%d\n",
						channel, (UINT32)media_handle, status, p2k_audio_channels[channel].started);
				if (status != MMSS_NO_ERROR)
					P2K_CloseChannel(channel, false);
			}
			break;

		case DL_MMSP_MEDIA_OPEN_FAILED:
			if (data)
			{
				DL_MMSP_MEDIA_OPEN_FAILED_DATA_T *msg = (DL_MMSP_MEDIA_OPEN_FAILED_DATA_T *)data;
				media_handle = msg->media_handle;
				iface_handle = msg->iface_data.handle;
			}
			channel = P2K_FindChannelByHandle(media_handle, iface_handle);
			LOG("P2K audio: event open failed handle=0x%08X iface_handle=0x%08X channel=%d\n",
					(UINT32)media_handle, iface_handle, channel);
			if (channel >= 0)
				P2K_CloseChannel(channel, false);
			break;

		case DL_MMSP_MEDIA_COMPLETE:
			if (data)
			{
				DL_MMSP_MEDIA_COMPLETE_DATA_T *msg = (DL_MMSP_MEDIA_COMPLETE_DATA_T *)data;
				media_handle = msg->media_handle;
				iface_handle = msg->iface_data.handle;
			}
			channel = P2K_FindChannelByHandle(media_handle, iface_handle);
			LOG("P2K audio: event complete handle=0x%08X iface_handle=0x%08X channel=%d\n",
					(UINT32)media_handle, iface_handle, channel);
			if (channel >= 0)
				P2K_CloseChannel(channel, false);
			break;

		case DL_MMSP_MEDIA_ERROR:
			if (data)
			{
				DL_MMSP_MEDIA_ERROR_DATA_T *msg = (DL_MMSP_MEDIA_ERROR_DATA_T *)data;
				media_handle = msg->media_handle;
				iface_handle = msg->iface_data.handle;
			}
			channel = P2K_FindChannelByHandle(media_handle, iface_handle);
			LOG("P2K audio: event error handle=0x%08X iface_handle=0x%08X channel=%d\n",
					(UINT32)media_handle, iface_handle, channel);
			if (channel >= 0)
				P2K_CloseChannel(channel, false);
			break;

		case DL_MMSP_MEDIA_CLOSE_COMPLETE:
		case DL_MMSP_MEDIA_STOP_COMPLETE:
			if (data)
			{
				DL_MMSP_MEDIA_OPEN_SUCCESS_DATA_T *msg = (DL_MMSP_MEDIA_OPEN_SUCCESS_DATA_T *)data;
				media_handle = msg->media_handle;
				iface_handle = msg->iface_data.handle;
			}
			LOG("P2K audio: event cleanup code=0x%08X handle=0x%08X iface_handle=0x%08X channel=%d\n",
					code, (UINT32)media_handle, iface_handle,
					P2K_FindChannelByHandle(media_handle, iface_handle));
			break;

		default:
			LOG("P2K audio: event ignored code=0x%08X\n", code);
			break;
	}

	APP_ConsumeEv(ev_st, app);
	return RESULT_OK;
}

#endif


int16_t I_StartSound(sfxenum_t id, int16_t channel, int16_t vol, int16_t sep)
{
#if defined(SDL) || !defined(P2K)
	UNUSED(vol);
#endif
	UNUSED(sep);
#if defined(SDL)
	UNUSED(id);
#endif

	if (!(0 <= channel && channel < MAX_CHANNELS))
		return -1;

//	// hacks out certain PC sounds
//	if (id == sfx_posact
//	 || id == sfx_bgact
//	 || id == sfx_dmact
//	 || id == sfx_dmpain
//	 || id == sfx_popain
//	 || id == sfx_sawidl)
//		return -1;

#if !defined(SDL) && !defined(P2K)
	int16_t lumpnum = firstsfx + id;
	PCFX_Play(lumpnum);
#elif defined(P2K)
	if (!(sfx_None < id && id < NUMSFX))
	{
		LOG("P2K audio: bad sfx id=%d\n", id);
		return -1;
	}

	{
		const int16_t lumpnum = p2k_sfx_lumps[id];
		uint16_t length;
		uint16_t pc_length;
		uint16_t max_ticks;
		const uint16_t __far *pc_speaker;

		if (lumpnum < 0)
		{
			LOG("P2K audio: missing sfx id=%d name=%s\n", id, p2k_sfx_names[id]);
			return -1;
		}

		length = W_LumpLength(lumpnum);
		if (length < 4)
		{
			LOG("P2K audio: bad lump length, id=%d name=%s lump=%d length=%d\n",
					id, p2k_sfx_names[id], lumpnum, length);
			return -1;
		}

		max_ticks = (length - sizeof(uint16_t)) / sizeof(uint16_t);
		if (!max_ticks)
		{
			LOG("P2K audio: empty lump, id=%d name=%s lump=%d length=%d\n",
					id, p2k_sfx_names[id], lumpnum, length);
			return -1;
		}

		P2K_StopChannel(channel);

		pc_speaker = (const uint16_t __far *)W_GetLumpByNum(lumpnum);
		pc_length = SHORT(pc_speaker[0]);
		if (pc_length > max_ticks)
			pc_length = max_ticks;

		p2k_audio_channels[channel].wav_size =
				P2K_SynthPcSpeakerWav(p2k_audio_channels[channel].wav, &pc_speaker[1], pc_length, vol);
		p2k_audio_channels[channel].life_tics = P2K_GC_OPEN_TIMEOUT_TICS;
		p2k_audio_channels[channel].play_tics = (uint16_t)((pc_length + 3u) / 4u + P2K_GC_EXTRA_LIFE_TICS);
		Z_ChangeTagToCache(pc_speaker);

		LOG("P2K audio: start id=%d name=%s lump=%d lump_size=%d pc_len=%d wav_size=%d rate=%d bits=%d vol=%d\n",
				id, p2k_sfx_names[id], lumpnum, length, pc_length, p2k_audio_channels[channel].wav_size,
				P2K_AUDIO_RATE, P2K_AUDIO_BYTES_PER_SAMPLE * 8u, vol);

		return P2K_PlayWav(channel);
	}
#endif

	return channel;
}


void I_StopSound(int16_t channel)
{
	if (!(0 <= channel && channel < MAX_CHANNELS))
		return;

#if defined(P2K)
	P2K_StopChannel(channel);
#endif
}


boolean I_SoundIsPlaying(int16_t channel)
{
	if (!(0 <= channel && channel < MAX_CHANNELS))
		return false;

#if defined(P2K)
	return p2k_audio_channels[channel].active;
#else
	return false;
#endif
}


void I_UpdateSound(void)
{
#if defined(P2K)
	for (int16_t i = 0; i < MAX_CHANNELS; i++)
	{
		if (p2k_audio_channels[i].active)
		{
			if (p2k_audio_channels[i].life_tics)
				p2k_audio_channels[i].life_tics--;

			if (!p2k_audio_channels[i].life_tics)
			{
				LOG("P2K audio: gc timeout channel=%d handle=0x%08X started=%d\n",
						i, (UINT32)p2k_audio_channels[i].media_handle, p2k_audio_channels[i].started);
				P2K_CloseChannel(i, true);
			}
		}
	}
#endif
}


void I_InitSound(void)
{
	if (M_CheckParm("-nosound") || M_CheckParm("-nosfx"))
		nosfxparm = true;

	if (nomusicparm && nosfxparm)
		return;

#if !defined(SDL) && !defined(P2K)
	PCFX_Init();
#elif defined(SDL)
	nosfxparm = true;
#elif defined(P2K)
	LOG("P2K audio: init gc backend iface_port=0x%08X iface_handle=0x%08X\n",
			g_p2k_iface_data.port, g_p2k_iface_data.handle);
#endif

	// Finished initialization.
	printf("I_InitSound: sound ready\n");
}


void I_InitSound2(void)
{
#if defined(P2K)
	int16_t i;
	int16_t found = 0;

	p2k_sfx_lumps[0] = -1;
	for (i = 1; i < NUMSFX; i++)
	{
		p2k_sfx_lumps[i] = W_CheckNumForName(p2k_sfx_names[i]);
		if (p2k_sfx_lumps[i] >= 0)
			found++;
	}

	if (!found)
	{
		nosfxparm = true;
		LOG("%s\n", "I_InitSound: no PC speaker lumps found");
		return;
	}

	LOG("I_InitSound: %d PC speaker lumps found\n", found);
#else
	firstsfx = W_GetNumForName("DPPISTOL") - 1;
#endif
}


void I_ShutdownSound(void)
{
	if (nosfxparm)
		return;

#if !defined(SDL) && !defined(P2K)
	PCFX_Shutdown();
#elif defined(P2K)
	for (int16_t i = 0; i < MAX_CHANNELS; i++)
		P2K_StopChannel(i);
	S_Shutdown();
#else
	S_Shutdown();
#endif
}


void I_PlaySong(musicenum_t handle, boolean looping)
{
	UNUSED(handle);
	UNUSED(looping);
}


void I_StopSong(musicenum_t handle)
{
	UNUSED(handle);
}

void I_SetMusicVolume(int16_t volume)
{
	UNUSED(volume);
}
