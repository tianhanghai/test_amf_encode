#ifndef WHH_AMF_VENC_H
#define WHH_AMF_VENC_H

#include "stdint.h"
#include "whh_amf_factory_helper.h"
#include "whh_amf_thread.h"
#include "components/VideoEncoderVCE.h"
#include "components/VideoEncoderHEVC.h"
#ifdef _WIN32
#include <d3d9.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#endif

#define START_TIME_PROPERTY L"StartTimeProperty"

struct whh_venc_gpu_info_s {
	char gpu_name[128];
	uint32_t index;
	uint32_t device_id;
};

enum whh_venc_code_type_e
{
	WHH_VENC_CODE_TYPE_UNKNOWN,														
	WHH_VENC_CODE_TYPE_H264,		
	WHH_VENC_CODE_TYPE_H265,	
	WHH_VENC_CODE_TYPE_COUNT								
};

enum whh_venc_fourcc_e
{
	WHH_VENC_FOURCC_UNKNOWN,								
	WHH_VENC_FOURCC_NV12,
	WHH_VENC_FOURCC_COUNT
};

enum whh_venc_targetusage_e
{
	WHH_VENC_TARGETUSAGE_UNKNOWN,						
	WHH_VENC_TARGETUSAGE_BEST_QUALITY,					
	WHH_VENC_TARGETUSAGE_BALANCED,						
	WHH_VENC_TARGETUSAGE_BEST_SPEED,						
	WHH_VENC_TARGETUSAGE_COUNT							
};

enum whh_venc_rate_control_mode_e
{
	WHH_VENC_RATECONTROL_UNKNOWN,						
	WHH_VENC_RATECONTROL_CBR,													
	WHH_VENC_RATECONTROL_COUNT							
};

struct whh_venc_rate_control_s
{
	whh_venc_rate_control_mode_e mode;					
	union{
		struct {
			uint32_t target_bitrate;					
			uint32_t max_bitrate;						
		};
		struct {
			uint8_t qpi;								
			uint8_t qpb;								
			uint8_t qpp;								
			uint8_t reserved;							  
		};
	};
};

struct whh_venc_fps_s{
	int32_t num;									
	int32_t den;									
};

enum whh_venc_profile_e
{
	WHH_VENC_PROFILE_UNKNOWN,							
	WHH_VENC_PROFILE_H264_BASELINE,						
	WHH_VENC_PROFILE_H264_MAIN,							
	WHH_VENC_PROFILE_H264_HIGH,							
	WHH_VENC_PROFILE_H265_MAIN,							
	WHH_VENC_PROFILE_COUNT								
};

enum whh_venc_level_e
{
	WHH_VENC_LEVEL_UNKNOWN,								
	WHH_VENC_LEVEL_2_1,									
	WHH_VENC_LEVEL_3_1,									
	WHH_VENC_LEVEL_4_1,									
	WHH_VENC_LEVEL_5_1,									
	WHH_VENC_LEVEL_5_2,									
	WHH_VENC_LEVEL_6_1,									
	WHH_VENC_LEVEL_6_2,									
	WHH_VENC_LEVEL_COUNT									
};

enum whh_venc_amd_mem_type_e {
	WHH_VENC_AMD_MEM_CPU,								
	WHH_VENC_AMD_MEM_DX,									
	WHH_VENC_AMD_MEM_OPENGL,								
	WHH_VENC_AMD_MEM_VULKAN,								
	WHH_VENC_AMD_MEM_COUNT								
};

struct whh_venc_param_s {
	whh_venc_code_type_e			code_type;						
	whh_venc_fourcc_e				fourcc;							
	whh_venc_targetusage_e			targetusage;					
	whh_venc_rate_control_s			rate_control;				
	int32_t							width;										
	int32_t							height;										
	whh_venc_fps_s					fps;									
	int32_t							slice_num;									
	int32_t							gop_pic_size;								
	int32_t							gop_ref_size;								
	whh_venc_profile_e				profile;							
	whh_venc_level_e				level;								
	whh_venc_amd_mem_type_e			amd_mem_reserved;			
};


extern int32_t					g_amd_gpu_num ;
extern whh_venc_gpu_info_s*		g_p_amd_gpu_list;


typedef void(*WHH_ENCODER_CALLBACK)(void * user_ptr, const uint8_t * p_frame, uint32_t frame_len);


void whh_venc_init();

void whh_venc_deinit();

int whh_venc_get_amd_gpu_num();

bool whh_venc_get_gpu_info_by_index(int t_n_index, whh_venc_gpu_info_s* t_p_info);


class CPollingThread:public amf::AMFThread
{
public:
	CPollingThread(amf::AMFContext* context,amf::AMFComponent* encoder, whh_venc_code_type_e t_code_type);
	~CPollingThread();

public:
	virtual void Run();
	void set_callback_output(
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr);

public:
	bool							m_b_started;

protected:
	amf::AMFContextPtr				m_p_context;
	amf::AMFComponentPtr			m_p_encoder;

	WHH_ENCODER_CALLBACK			m_frame_callback;
	void*							m_p_user_ptr;

	whh_venc_code_type_e			m_code_type;
};

class CWHHAMDEncoder
{
public:
	CWHHAMDEncoder();
	~CWHHAMDEncoder();

public:
	bool init();
	bool deinit();

	bool create_encoder(
		whh_venc_param_s *p_param,
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr,
		int32_t index = -1
		);

	bool destory_encoder();

	bool put_frame(uint8_t *p_frame);

protected:
	bool filldata_surface_host(
		amf::AMFContext *context,
		amf::AMFSurface *surface,
		unsigned char* t_p_data);

#ifdef _WIN32
	bool filldata_surface_dx11(
		amf::AMFContext *context,
		amf::AMFSurface *surface,
		unsigned char* t_p_data);
#endif

	bool create_encoder_h264(
		whh_venc_param_s *p_param,
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr,
		int32_t t_u32_adapter_id = -1
		);

	bool create_encoder_init_mem(
		whh_venc_param_s *p_param,
		int32_t t_u32_adapter_id = -1);


	bool create_encoder_h264_init_props(
		amf::AMFFactory *t_p_factory,
		whh_venc_param_s *p_param,
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr);

	bool create_encoder_h265(
		whh_venc_param_s *p_param,
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr,
		int32_t t_u32_adapter_id = -1
		);

	bool create_encoder_h265_init_props(
		amf::AMFFactory *t_p_factory,
		whh_venc_param_s *p_param,
		WHH_ENCODER_CALLBACK frame_callback,
		void *user_ptr
		);

	uint32_t get_stride(amf::AMF_SURFACE_FORMAT t_fmt, uint32_t t_u32_width);

	void* get_dx11_device(int32_t t_i32_adapter_id);

protected:
	amf::AMFContextPtr					m_p_context;
	amf::AMFComponentPtr				m_p_encoder;
	amf::AMF_MEMORY_TYPE				m_mem_type_in;
	amf::AMFSurfacePtr					m_surface_in;

	WHH_ENCODER_CALLBACK				m_frame_callback;
	void*								m_p_user_ptr;

	bool								m_b_init;
	bool								m_b_create_encoder;

	CPollingThread*						m_p_thread;
	whh_venc_param_s					m_encode_param;

	amf::AMF_SURFACE_FORMAT				m_surface_fmt;
	uint32_t							m_u32_width;
	uint32_t							m_u32_height;

	whh_venc_param_s					m_venc_param;

	uint8_t*							m_pu8_extra_data;

	bool								m_b_force_idr;

	//d3d11 device
	void*								m_p_d3d11_device;
};

#endif