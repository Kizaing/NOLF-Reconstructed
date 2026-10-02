// Miles Sound System (MSS32.DLL) declarations used by the Talon sound code.
// Only what lithtech.exe imports; signatures follow the Miles 6 mss.h.
#ifndef __MSS_H__
#define __MSS_H__

#include <windows.h>
#include <mmsystem.h>

typedef signed char		S8;
typedef unsigned char	U8;
typedef short			S16;
typedef unsigned short	U16;
typedef long			S32;
typedef unsigned long	U32;
typedef float			F32;

#define AILCALL		__stdcall
#define DXDEC		extern "C" __declspec(dllimport)

typedef void *	HSAMPLE;
typedef void *	H3DSAMPLE;
typedef void *	H3DPOBJECT;
typedef void *	HDIGDRIVER;
typedef void *	HSTREAM;
typedef U32		HPROVIDER;
typedef U32		HPROENUM;
typedef U32		HINTENUM;

#define HPROENUM_FIRST		0
#define HINTENUM_FIRST		0

// AIL_set_preference
#define DIG_MIXER_CHANNELS			1
#define DIG_USE_WAVEOUT				15
#define AIL_LOCK_PROTECTION			18
#define AIL_ENABLE_MMX_SUPPORT		27
#define DIG_REVERB_BUFFER_SIZE		40

// Sample status
#define SMP_FREE			0x0001
#define SMP_DONE			0x0002
#define SMP_PLAYING			0x0004
#define SMP_STOPPED			0x0008

// Pipeline stages
#define DP_FILTER			1

// Sample formats
#define DIG_F_MONO_8		0
#define DIG_F_MONO_16		1
#define DIG_F_STEREO_8		2
#define DIG_F_STEREO_16		3

#define M3D_NOERR			0

typedef S32 (AILCALL *AILLENGTHYCB)(U32 state, U32 user);

typedef struct
{
	S32		format;
	void const *data_ptr;
	U32		data_len;
	U32		rate;
	S32		bits;
	S32		channels;
	U32		samples;
	U32		block_size;
	void const *initial_ptr;
} AILSOUNDINFO;

// RIB_INTERFACE_ENTRY
typedef struct
{
	S32		type;
	char	*entry_name;
	U32		token;
	S32		subtype;
} RIB_INTERFACE_ENTRY;

// System
DXDEC S32		AILCALL AIL_startup(void);
DXDEC void		AILCALL AIL_shutdown(void);
DXDEC void		AILCALL AIL_lock(void);
DXDEC void		AILCALL AIL_unlock(void);
DXDEC S32		AILCALL AIL_set_preference(U32 number, S32 value);
DXDEC S32		AILCALL AIL_get_preference(U32 number);
DXDEC U32		AILCALL AIL_ms_count(void);
DXDEC void *	AILCALL AIL_mem_alloc_lock(U32 size);
DXDEC void		AILCALL AIL_mem_free_lock(void *ptr);

// mss32.lib: registers AIL_shutdown with atexit (AIL_startup macro in mss.h).
extern "C" S32 MSS_auto_cleanup(void);

// Digital driver
DXDEC S32		AILCALL AIL_waveOutOpen(HDIGDRIVER *drvr, LPHWAVEOUT *lphWaveOut, S32 wDeviceID, LPWAVEFORMAT lpFormat);
DXDEC void		AILCALL AIL_waveOutClose(HDIGDRIVER drvr);
DXDEC S32		AILCALL AIL_digital_handle_release(HDIGDRIVER drvr);
DXDEC S32		AILCALL AIL_digital_handle_reacquire(HDIGDRIVER drvr);
DXDEC void		AILCALL AIL_set_digital_master_volume(HDIGDRIVER dig, S32 master_volume);
DXDEC S32		AILCALL AIL_digital_master_volume(HDIGDRIVER dig);

// 2D samples
DXDEC HSAMPLE	AILCALL AIL_allocate_sample_handle(HDIGDRIVER dig);
DXDEC void		AILCALL AIL_release_sample_handle(HSAMPLE S);
DXDEC void		AILCALL AIL_init_sample(HSAMPLE S);
DXDEC S32		AILCALL AIL_set_sample_file(HSAMPLE S, void const *file_image, S32 block);
DXDEC void		AILCALL AIL_set_sample_address(HSAMPLE S, void const *start, U32 len);
DXDEC void		AILCALL AIL_set_sample_type(HSAMPLE S, S32 format, U32 flags);
DXDEC void		AILCALL AIL_set_sample_playback_rate(HSAMPLE S, S32 playback_rate);
DXDEC void		AILCALL AIL_set_sample_volume(HSAMPLE S, S32 volume);
DXDEC void		AILCALL AIL_set_sample_pan(HSAMPLE S, S32 pan);
DXDEC S32		AILCALL AIL_sample_volume(HSAMPLE S);
DXDEC S32		AILCALL AIL_sample_pan(HSAMPLE S);
DXDEC void		AILCALL AIL_set_sample_user_data(HSAMPLE S, U32 index, S32 value);
DXDEC S32		AILCALL AIL_sample_user_data(HSAMPLE S, U32 index);
DXDEC void		AILCALL AIL_stop_sample(HSAMPLE S);
DXDEC void		AILCALL AIL_resume_sample(HSAMPLE S);
DXDEC void		AILCALL AIL_set_sample_loop_count(HSAMPLE S, S32 loop_count);
DXDEC void		AILCALL AIL_set_sample_loop_block(HSAMPLE S, S32 loop_start_offset, S32 loop_end_offset);
DXDEC void		AILCALL AIL_set_sample_ms_position(HSAMPLE S, S32 milliseconds);
DXDEC void		AILCALL AIL_set_sample_reverb(HSAMPLE S, F32 reverb_level, F32 reverb_reflect_time, F32 reverb_decay_time);
DXDEC HPROVIDER	AILCALL AIL_set_sample_processor(HSAMPLE S, U32 pipeline_stage, HPROVIDER provider);

// Filters
DXDEC S32		AILCALL AIL_enumerate_filters(HPROENUM *next, HPROVIDER *dest, char **name);
DXDEC S32		AILCALL AIL_enumerate_filter_sample_attributes(HPROVIDER lib, HINTENUM *next, RIB_INTERFACE_ENTRY *dest);
DXDEC void		AILCALL AIL_filter_sample_attribute(HSAMPLE S, char const *name, void *val);
DXDEC void		AILCALL AIL_set_filter_sample_preference(HSAMPLE S, char const *name, void const *val);

// 3D providers
DXDEC S32		AILCALL AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name);
DXDEC S32		AILCALL AIL_open_3D_provider(HPROVIDER lib);
DXDEC void		AILCALL AIL_close_3D_provider(HPROVIDER lib);
DXDEC void		AILCALL AIL_3D_provider_attribute(HPROVIDER lib, char const *name, void *val);
DXDEC void		AILCALL AIL_set_3D_room_type(HPROVIDER lib, S32 room_type);
DXDEC S32		AILCALL AIL_3D_room_type(HPROVIDER lib);
DXDEC H3DPOBJECT AILCALL AIL_open_3D_listener(HPROVIDER lib);
DXDEC void		AILCALL AIL_close_3D_listener(H3DPOBJECT listener);

// 3D objects
DXDEC void		AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z);
DXDEC void		AILCALL AIL_set_3D_velocity_vector(H3DPOBJECT obj, F32 dX_per_ms, F32 dY_per_ms, F32 dZ_per_ms);
DXDEC void		AILCALL AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up);
DXDEC void		AILCALL AIL_3D_position(H3DPOBJECT obj, F32 *X, F32 *Y, F32 *Z);
DXDEC void		AILCALL AIL_3D_velocity(H3DPOBJECT obj, F32 *dX_per_ms, F32 *dY_per_ms, F32 *dZ_per_ms);
DXDEC void		AILCALL AIL_3D_orientation(H3DPOBJECT obj, F32 *X_face, F32 *Y_face, F32 *Z_face, F32 *X_up, F32 *Y_up, F32 *Z_up);
DXDEC void		AILCALL AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value);
DXDEC S32		AILCALL AIL_3D_user_data(H3DPOBJECT obj, U32 index);

// 3D samples
DXDEC H3DSAMPLE	AILCALL AIL_allocate_3D_sample_handle(HPROVIDER lib);
DXDEC void		AILCALL AIL_release_3D_sample_handle(H3DSAMPLE S);
DXDEC void		AILCALL AIL_start_3D_sample(H3DSAMPLE S);
DXDEC void		AILCALL AIL_stop_3D_sample(H3DSAMPLE S);
DXDEC void		AILCALL AIL_resume_3D_sample(H3DSAMPLE S);
DXDEC void		AILCALL AIL_end_3D_sample(H3DSAMPLE S);
DXDEC S32		AILCALL AIL_set_3D_sample_info(H3DSAMPLE S, AILSOUNDINFO const *info);
DXDEC void		AILCALL AIL_set_3D_sample_volume(H3DSAMPLE S, S32 volume);
DXDEC S32		AILCALL AIL_3D_sample_volume(H3DSAMPLE S);
DXDEC void		AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE S, S32 playback_rate);
DXDEC void		AILCALL AIL_set_3D_sample_offset(H3DSAMPLE S, U32 offset);
DXDEC void		AILCALL AIL_set_3D_sample_loop_count(H3DSAMPLE S, U32 loops);
DXDEC void		AILCALL AIL_set_3D_sample_loop_block(H3DSAMPLE S, S32 loop_start_offset, S32 loop_end_offset);
DXDEC U32		AILCALL AIL_3D_sample_status(H3DSAMPLE S);
DXDEC void		AILCALL AIL_set_3D_sample_distances(H3DSAMPLE S, F32 max_dist, F32 min_dist);
DXDEC void		AILCALL AIL_set_3D_sample_preference(H3DSAMPLE S, char const *name, void const *val);
DXDEC void		AILCALL AIL_set_3D_sample_effects_level(H3DSAMPLE S, F32 effects_level);
DXDEC void		AILCALL AIL_set_3D_sample_obstruction(H3DSAMPLE S, F32 obstruction);
DXDEC F32		AILCALL AIL_3D_sample_obstruction(H3DSAMPLE S);
DXDEC void		AILCALL AIL_set_3D_sample_occlusion(H3DSAMPLE S, F32 occlusion);

// Streams
DXDEC HSTREAM	AILCALL AIL_open_stream(HDIGDRIVER dig, char const *filename, S32 stream_mem);
DXDEC void		AILCALL AIL_close_stream(HSTREAM stream);
DXDEC void		AILCALL AIL_start_stream(HSTREAM stream);
DXDEC void		AILCALL AIL_pause_stream(HSTREAM stream, S32 onoff);
DXDEC void		AILCALL AIL_set_stream_loop_count(HSTREAM stream, S32 count);
DXDEC void		AILCALL AIL_set_stream_ms_position(HSTREAM S, S32 milliseconds);
DXDEC void		AILCALL AIL_set_stream_playback_rate(HSTREAM stream, S32 rate);
DXDEC void		AILCALL AIL_set_stream_volume(HSTREAM stream, S32 volume);
DXDEC void		AILCALL AIL_set_stream_pan(HSTREAM stream, S32 pan);
DXDEC S32		AILCALL AIL_stream_volume(HSTREAM stream);
DXDEC S32		AILCALL AIL_stream_pan(HSTREAM stream);
DXDEC void		AILCALL AIL_set_stream_user_data(HSTREAM S, U32 index, S32 value);

// Decompression
DXDEC S32		AILCALL AIL_decompress_ADPCM(AILSOUNDINFO const *info, void **outdata, U32 *outsize);
DXDEC S32		AILCALL AIL_decompress_ASI(void const *indata, U32 insize, char const *filename_ext, void **outdata, U32 *outsize, AILLENGTHYCB callback);

#endif  // __MSS_H__
