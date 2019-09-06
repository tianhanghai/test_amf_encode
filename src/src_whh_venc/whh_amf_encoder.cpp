#include "mw_amd_encoder.h"
#include "vector"
#include "time.h"
using namespace std;

CPollingThread::CPollingThread(
	amf::AMFContext* context,
	amf::AMFComponent* encoder, 
	mw_venc_code_type_t t_code_type)
{
	m_p_context = context;
	m_p_encoder = encoder;

	m_frame_callback = NULL;
	m_p_user_ptr = NULL;

	m_b_started = false;

	m_code_type = t_code_type;
}

CPollingThread::~CPollingThread()
{

}

void CPollingThread::Run()
{
	RequestStop();

	HANDLE t_h_thread = GetCurrentThread();
	DWORD t_dw_priority = GetPriorityClass(t_h_thread);
	DWORD t_dw_set_priority = REALTIME_PRIORITY_CLASS;
	BOOL t_b_ret = SetPriorityClass(t_h_thread,t_dw_set_priority);
	if (t_b_ret == TRUE) {
		printf("set thread to real time succeeded\n");
	}
	else {
		printf("set thread to real time failed\n");
	}

	amf_pts t_pts_latency_time = 0;
	amf_pts t_pts_encode_duration = 0;
	amf_pts t_pts_last_poll_time = 0;

	AMF_RESULT t_res = AMF_OK;

	m_b_started = true;

	while (true){
		amf::AMFDataPtr t_p_data;
		t_res = m_p_encoder->QueryOutput(&t_p_data);
		if(t_res==AMF_EOF)
			break;// drain complete

		if (t_p_data != NULL) {
			amf_pts t_pts_poll_time = amf_high_precision_clock();
			amf_pts t_pts_start_time = 0;
			t_p_data->GetProperty(START_TIME_PROPERTY, &t_pts_start_time);
			if (t_pts_start_time < t_pts_last_poll_time)// remove wait time if submission was faster than encode
				t_pts_start_time = t_pts_last_poll_time;

			t_pts_last_poll_time = t_pts_poll_time;
			t_pts_encode_duration += t_pts_poll_time - t_pts_start_time;
			if (t_pts_latency_time == 0)
				t_pts_latency_time = t_pts_poll_time - t_pts_start_time;

			mw_venc_frame_info_t t_frame_info;
			amf_pts t_pts=t_p_data->GetPts();
			t_frame_info.pts = t_pts;
			t_frame_info.dts = t_pts;

			amf::AMFBufferPtr t_p_buffer(t_p_data);//query for buffer interface

			if (m_code_type == MW_VENC_CODE_TYPE_H264) {
				amf_int64 t_frame_type = -1;
				t_res = t_p_buffer->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &t_frame_type);
				if (t_res != AMF_OK)
					t_frame_info.frame_type = MW_VENC_FRAME_TYPE_UNKNOWN;
				else {
					switch (t_frame_type)
					{
					case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_IDR;
						break;
					case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_I;
						break;
					case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_P;
						break;
					case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_B:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_B;
						break;
					default:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_UNKNOWN;
						break;
					}
				}
			}
			else {
				amf_int64 t_frame_type = -1;
				t_res = t_p_buffer->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &t_frame_type);
				if (t_res != AMF_OK)
					t_frame_info.frame_type = MW_VENC_FRAME_TYPE_UNKNOWN;
				else{
					switch (t_frame_type)
					{
					case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_IDR;
						break;
					case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_I:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_I;
						break;
					case AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_P:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_P;
						break;
					default:
						t_frame_info.frame_type = MW_VENC_FRAME_TYPE_UNKNOWN;
						break;
					}
				}
			}

			if (m_frame_callback != NULL) {
				m_frame_callback(
					m_p_user_ptr,
					(const uint8_t*)(t_p_buffer->GetNative()),
					t_p_buffer->GetSize(),
					&t_frame_info);
			}
		}
		else
			amf_sleep(1);
	}
}

void CPollingThread::set_callback_output(MW_ENCODER_CALLBACK frame_callback, void *user_ptr)
{
	m_frame_callback = frame_callback;
	m_p_user_ptr = user_ptr;
}

CMWAMDEncoder::CMWAMDEncoder()
{
	m_b_init = false;
	m_b_create_encoder = false;

	m_b_force_idr = false;

	m_p_thread = NULL;
	m_p_context = NULL;
	m_p_encoder = NULL;

	m_frame_callback = NULL;
	m_p_user_ptr = NULL;

	m_mem_type_in = amf::AMF_MEMORY_DX11;

	m_pu8_extra_data = NULL;

	m_p_d3d11_device = NULL;
}

CMWAMDEncoder::~CMWAMDEncoder()
{
	destory_encoder();
}

mw_venc_status_t CMWAMDEncoder::init()
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	t_res = g_amf_factory.init();
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;
		m_b_init = false;
	}
	else
		m_b_init = true;

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::deinit()
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (m_b_init) {
		g_amf_factory.terminate();
		m_b_init = false;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder(
	mw_venc_param_t *p_param, 
	MW_ENCODER_CALLBACK frame_callback,
	void *user_ptr,
	int32_t index)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_init) {
		t_stat = MW_VENC_STATUS_FAIL;
		m_b_create_encoder = false;
		return t_stat;
	}

	m_venc_param = *p_param;
	m_encode_param = m_venc_param;
	m_u32_width = m_encode_param.width;
	m_u32_height = m_encode_param.height;

	if (m_venc_param.code_type == MW_VENC_CODE_TYPE_H264)
		t_stat = create_encoder_h264(p_param, frame_callback, user_ptr,index);
	else if (m_venc_param.code_type == MW_VENC_CODE_TYPE_H265)
		t_stat = create_encoder_h265(p_param, frame_callback, user_ptr,index);
	else
		t_stat = MW_VENC_STATUS_INVALID_PARAM;

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder_ex(mw_venc_param_t *p_param, mw_venc_amd_opengl_param_prv_t* p_opengl_param, MW_ENCODER_CALLBACK frame_callback, void *user_ptr)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_init) {
		t_stat = MW_VENC_STATUS_FAIL;
		m_b_create_encoder = false;
		return t_stat;
	}

	m_venc_param = *p_param;
	m_encode_param = m_venc_param;
	m_venc_opengl_param = *p_opengl_param;
	m_encode_opengl_param = m_venc_opengl_param;
	m_u32_width = m_encode_param.width;
	m_u32_height = m_encode_param.height;

	if (m_venc_param.code_type == MW_VENC_CODE_TYPE_H264)
		t_stat = create_encoder_h264_ex(p_param, p_opengl_param,frame_callback, user_ptr);
	else if (m_venc_param.code_type == MW_VENC_CODE_TYPE_H265)
		t_stat = create_encoder_h265_ex(p_param, p_opengl_param,frame_callback, user_ptr);
	else
		t_stat = MW_VENC_STATUS_INVALID_PARAM;

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::destory_encoder()
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (m_b_create_encoder) {
		AMF_RESULT t_res = AMF_OK;
		while (true) {
			t_res = m_p_encoder->Drain();
			if (t_res != AMF_INPUT_FULL)
				break;

			amf_sleep(1);
		}

		while (!m_p_thread->m_b_started) {
			amf_sleep(100);
		}
	}

	if (m_p_thread != NULL) {
		m_p_thread->WaitForStop();

		delete m_p_thread;
		m_p_thread = NULL;
	}

	if (m_p_encoder != NULL) {
		m_p_encoder->Terminate();
		m_p_encoder = NULL;
	}

	if (m_p_context != NULL) {
		m_p_context->Terminate();
		m_p_context = NULL;
	}

	if (m_pu8_extra_data != NULL) {
		free(m_pu8_extra_data);
		m_pu8_extra_data = NULL;
	}

	if (m_p_d3d11_device != NULL) {
#ifdef _WIN32
		ID3D11Device* t_p_d3d11_device = (ID3D11Device*)m_p_d3d11_device;
		t_p_d3d11_device->Release();
#endif
		m_p_d3d11_device = NULL;
	}

	m_b_create_encoder = false;

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::put_frame(uint8_t *p_frame)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_create_encoder) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	AMF_RESULT t_res = AMF_OK;
	uint32_t t_u32_stride = 0;
	amf::AMF_SURFACE_FORMAT t_amf_fmt;
	uint32_t t_u32_width;
	uint32_t t_u32_height;

	t_amf_fmt = m_surface_fmt;
	if (m_surface_fmt == amf::AMF_SURFACE_YV12)
		t_amf_fmt = amf::AMF_SURFACE_YUV420P;

	t_u32_width = m_encode_param.width;
	t_u32_height = m_encode_param.height;

#ifdef USE_TIME
	clock_t t_alloc_begin = 0;
	clock_t t_alloc_end = 0;
	clock_t t_fill_begin = 0;
	clock_t t_fill_end = 0;
	clock_t t_submit_begin = 0;
	clock_t t_submit_end = 0;
#else
	amf_pts t_alloc_begin = 0;
	amf_pts t_alloc_end = 0;
	amf_pts t_fill_begin = 0;
	amf_pts t_fill_end = 0;
	amf_pts t_submit_begin = 0;
	amf_pts t_submit_end = 0;
#endif

	if (m_surface_in == NULL&&p_frame != NULL) {
		m_surface_in = NULL;
		amf::AMF_MEMORY_TYPE t_mem_type = m_mem_type_in == amf::AMF_MEMORY_VULKAN?amf::AMF_MEMORY_HOST:m_mem_type_in;
#ifdef USE_TIME
		//t_alloc_begin = clock();
#else
		t_alloc_begin = amf_high_precision_clock();
#endif
		t_res = m_p_context->AllocSurface(
			t_mem_type,
			t_amf_fmt,
			t_u32_width,
			t_u32_height,
			&m_surface_in);
#ifdef USE_TIME
		//t_alloc_end = clock();
#else
		t_alloc_end = amf_high_precision_clock();
#endif
		if (t_res != AMF_OK||m_surface_in==NULL) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
	}

	if (m_surface_in != NULL) {
		if (m_mem_type_in == amf::AMF_MEMORY_HOST)
			filldata_surface_host(m_p_context, m_surface_in, p_frame);
#ifdef _WIN32
		else if (m_mem_type_in == amf::AMF_MEMORY_DX11) {
#ifdef USE_TIME
			//t_fill_begin = clock();
#else
			t_fill_begin = amf_high_precision_clock();
#endif
			filldata_surface_dx11(m_p_context, m_surface_in, p_frame);
#ifdef USE_TIME
			//t_fill_end = clock();
#else
			t_fill_end = amf_high_precision_clock();
#endif	
		}
#endif
		else if(m_mem_type_in == amf::AMF_MEMORY_VULKAN)
			filldata_surface_host(m_p_context, m_surface_in, p_frame);

		amf_pts t_pts_start_time = amf_high_precision_clock();
		m_surface_in->SetProperty(START_TIME_PROPERTY, t_pts_start_time);

		bool t_b_insert = false;
		if (m_encode_param.code_type == MW_VENC_CODE_TYPE_H264) {
			m_surface_in->SetProperty(AMF_VIDEO_ENCODER_INSERT_SPS, &t_b_insert);
			m_surface_in->SetProperty(AMF_VIDEO_ENCODER_INSERT_PPS, &t_b_insert);

			if (m_b_force_idr) {
				amf_int64 t_i64_force_idr = AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR;
				t_res = m_surface_in->SetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, t_i64_force_idr);
				m_b_force_idr = false;
			}
		}
		else {
			//t_res = m_surface_in->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER, &t_b_insert);
			if (m_b_force_idr) {
				amf_int64 t_i64_force_idr = AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR;
				t_res = m_surface_in->SetProperty(AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, t_i64_force_idr);
				m_b_force_idr = false;
			}
		}

		t_res = AMF_INPUT_FULL;
		uint8_t t_u8_sub_try = 0;
#ifdef USE_TIME
		//t_submit_begin = clock();
#else
		t_submit_begin = amf_high_precision_clock();
#endif
		while (t_res != AMF_OK) {
			t_res = m_p_encoder->SubmitInput(m_surface_in);
			t_u8_sub_try++;
			if (t_res == AMF_INPUT_FULL)
				amf_sleep(1);
			else {
				if (t_res != AMF_OK) {
					t_stat = MW_VENC_STATUS_FAIL;
					return t_stat;
				}

				m_surface_in = NULL;
			}

			if (t_u8_sub_try > 1000) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}		
		}
#ifdef USE_TIME
		t_submit_end = clock();
		//printf("alloc:%ld fill:%ld submit:%ld try:%d\r", t_alloc_end - t_alloc_begin, t_fill_end - t_fill_begin, t_submit_end - t_submit_begin, t_u8_sub_try);
#else
		t_submit_end = amf_high_precision_clock();
		printf("alloc:%lld fill:%lld submit:%lld try:%d\n", t_alloc_end - t_alloc_begin, t_fill_end - t_fill_begin, t_submit_end - t_submit_begin, t_u8_sub_try);
#endif
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::put_opengl_texture(uint32_t *p_u32_textureid)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_create_encoder) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	AMF_RESULT t_res = AMF_OK;
	uint32_t t_u32_stride = 0;
	amf::AMF_SURFACE_FORMAT t_amf_fmt;
	uint32_t t_u32_width;
	uint32_t t_u32_height;

	t_amf_fmt = m_surface_fmt;
	if (m_surface_fmt == amf::AMF_SURFACE_YV12)
		t_amf_fmt = amf::AMF_SURFACE_YUV420P;

	t_u32_width = m_encode_param.width;
	t_u32_height = m_encode_param.height;

	if (m_surface_in != NULL || p_u32_textureid == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	if (m_mem_type_in != amf::AMF_MEMORY_OPENGL) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	amf::AMFContext::AMFOpenCLLocker locker(m_p_context);
	/**/
	t_res = m_p_context->CreateSurfaceFromOpenGLNative(m_surface_fmt, (amf_handle)p_u32_textureid[0], &m_surface_in, NULL);
	if (t_res != AMF_OK) {
		// wrap opengl texture failed
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	amf_pts t_pts_start_time = amf_high_precision_clock();
	m_surface_in->SetProperty(START_TIME_PROPERTY, t_pts_start_time);

	t_res = AMF_INPUT_FULL;
	uint8_t t_u8_sub_try = 0;
	while (t_res != AMF_OK) {
		t_res = m_p_encoder->SubmitInput(m_surface_in);
		t_u8_sub_try++;
		if (t_res == AMF_INPUT_FULL)
			amf_sleep(1);
		else {
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			m_surface_in = NULL;
		}

		if (t_u8_sub_try > 1000) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::get_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_create_encoder) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	if (m_encode_param.code_type == MW_VENC_CODE_TYPE_H264) {
		t_stat = get_h264_property(param, args);
	}
	else if(m_encode_param.code_type == MW_VENC_CODE_TYPE_H265){
		t_stat = get_h265_property(param, args);
	}
	else {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::set_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	if (!m_b_create_encoder) {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	if (m_encode_param.code_type == MW_VENC_CODE_TYPE_H264) {
		t_stat = set_h264_property(param, args);
	}
	else if (m_encode_param.code_type == MW_VENC_CODE_TYPE_H265) {
		t_stat = set_h265_property(param, args);
	}
	else {
		t_stat = MW_VENC_STATUS_FAIL;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::filldata_surface_host(amf::AMFContext *context, amf::AMFSurface *surface, unsigned char* t_p_data)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	amf::AMFPlane* t_p_plane=NULL;
	int t_n_plane = 0;
	int t_n_i = 0;

	t_n_plane = surface->GetPlanesCount();

	unsigned char* t_array_plannes[4];
	uint32_t t_u32_plannes;
	unsigned char* t_array_plannes_src[4];
	uint32_t t_array_plannes_hpitch[4];
	uint32_t t_array_plannes_width[4];
	uint32_t t_array_plannes_height[4];
	uint32_t t_array_plannes_stride[4];

	switch (m_surface_fmt)
	{
	case amf::AMF_SURFACE_NV12:
		t_u32_plannes = 2;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_src[1] = t_p_data + m_u32_width*m_u32_height;
		t_array_plannes_stride[0] = m_u32_width;
		t_array_plannes_stride[1] = m_u32_width;
		break;
	case amf::AMF_SURFACE_YV12:
		t_u32_plannes = 3;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_src[1] = t_p_data + m_u32_width*m_u32_height + m_u32_width / 2 * m_u32_height / 2;
		t_array_plannes_src[2] = t_p_data + m_u32_width*m_u32_height;
		t_array_plannes_stride[0] = m_u32_width;
		t_array_plannes_stride[1] = m_u32_width / 2;
		t_array_plannes_stride[2] = m_u32_width / 2;
		break;
	case amf::AMF_SURFACE_BGRA:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 4;
		break;
	case amf::AMF_SURFACE_ARGB:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 4;
		break;
	case amf::AMF_SURFACE_RGBA:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 4;
		break;
	case amf::AMF_SURFACE_GRAY8:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width;
		break;
	case amf::AMF_SURFACE_YUV420P:
		t_u32_plannes = 3;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_src[1] = t_p_data + m_u32_width*m_u32_height;
		t_array_plannes_src[2] = t_p_data + m_u32_width*m_u32_height + m_u32_width / 2 * m_u32_height / 2;
		t_array_plannes_stride[0] = m_u32_width;
		t_array_plannes_stride[1] = m_u32_width / 2;
		t_array_plannes_stride[2] = m_u32_width / 2;
		break;
	case amf::AMF_SURFACE_U8V8:
		break;
	case amf::AMF_SURFACE_YUY2:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 2;
		break;
	case amf::AMF_SURFACE_P010:
		t_u32_plannes = 2;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_src[1] = t_p_data + m_u32_width*m_u32_height * 2;
		t_array_plannes_stride[0] = m_u32_width * 2;
		t_array_plannes_stride[1] = m_u32_width * 2;
		break;
	case amf::AMF_SURFACE_RGBA_F16:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 8;
		break;
	case amf::AMF_SURFACE_UYVY:
		t_u32_plannes = 1;
		t_array_plannes_src[0] = t_p_data;
		t_array_plannes_stride[0] = m_u32_width * 2;
		break;
	default:
		break;
	}
	uint32_t t_u32_real_planes = surface->GetPlanesCount();
	if (t_u32_real_planes != t_u32_plannes) {
		return t_stat;
	}

	for (uint8_t i = 0; i < t_u32_plannes; i++) {
		amf::AMFPlanePtr t_p_plane = surface->GetPlaneAt(i);
		t_array_plannes[i] = (unsigned char*)t_p_plane->GetNative();
		t_array_plannes_width[i] = t_p_plane->GetWidth();
		t_array_plannes_height[i] = t_p_plane->GetHeight();
		t_array_plannes_hpitch[i] = t_p_plane->GetHPitch();
	}

	for (uint8_t i = 0; i < t_u32_plannes; i++) {
		for (uint32_t j = 0; j < t_array_plannes_height[i]; j++) {
			uint32_t t_u32_plane_off = j * t_array_plannes_hpitch[i];
			uint32_t t_u32_frame_off = j * t_array_plannes_stride[i];
#ifdef _WIN32
			std::memcpy(static_cast<void*>(static_cast<uint8_t*>(t_array_plannes[i] + t_u32_plane_off)),
				static_cast<void*>(t_array_plannes_src[i] + t_u32_frame_off), t_array_plannes_stride[i]);
#endif
#ifdef __linux
			memcpy(static_cast<void*>(static_cast<uint8_t*>(t_array_plannes[i] + t_u32_plane_off)),
				static_cast<void*>(t_array_plannes_src[i] + t_u32_frame_off), t_array_plannes_stride[i]);
#endif
		}
	}

	return t_stat;
}

#ifdef _WIN32
mw_venc_status_t CMWAMDEncoder::filldata_surface_dx11(amf::AMFContext *context, amf::AMFSurface *surface, unsigned char* t_p_data)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;
	HRESULT hr = S_OK;
	// fill surface with something something useful. We fill with color and color rect
	// get native DX objects
	ID3D11Device *deviceDX11 = (ID3D11Device *)context->GetDX11Device(); // no reference counting - do not Release()

	int t_n_plane_count = 0;
	t_n_plane_count=surface->GetPlanesCount();

	amf::AMF_MEMORY_TYPE t_type=surface->GetMemoryType();
	amf::AMF_SURFACE_FORMAT t_fmt = surface->GetFormat();

	if (t_type == amf::AMF_MEMORY_DX11) {
		ID3D11DeviceContext *deviceContextDX11 = NULL;
		deviceDX11->GetImmediateContext(&deviceContextDX11);

		uint32_t t_u32_stride = 0;
		if (m_venc_param.code_type == MW_VENC_CODE_TYPE_H264)
			t_u32_stride = get_stride(m_surface_fmt, m_venc_param.width);
		else
			t_u32_stride = get_stride(m_surface_fmt, m_venc_param.width);

		if (t_fmt == amf::AMF_SURFACE_NV12) {
			ID3D11Texture2D* surfaceDX11_1 = NULL;
			surfaceDX11_1 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
			deviceContextDX11->UpdateSubresource(surfaceDX11_1, 0, NULL, t_p_data, t_u32_stride, 0);
		}
		else if (t_fmt == amf::AMF_SURFACE_YV12) {
			if (t_n_plane_count == 3) {
				ID3D11Texture2D* surfaceDX11_1 = NULL;
				ID3D11Texture2D* surfaceDX11_2 = NULL;
				ID3D11Texture2D* surfaceDX11_3 = NULL;

				int t_n_u_offset=m_venc_param.width*m_venc_param.height + m_venc_param.width / 2 * m_venc_param.height / 2;
				int t_n_v_offset = m_venc_param.width*m_venc_param.height;

				surfaceDX11_1 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_1, 0, NULL, t_p_data, t_u32_stride, 0);
				surfaceDX11_2 = (ID3D11Texture2D*)surface->GetPlaneAt(1)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_2, 0, NULL, t_p_data + t_n_v_offset, t_u32_stride / 2, 0);
				surfaceDX11_3 = (ID3D11Texture2D*)surface->GetPlaneAt(2)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_3, 0, NULL, t_p_data + t_n_u_offset, t_u32_stride / 2, 0);
			}

		}
		else if(t_fmt==amf::AMF_SURFACE_YUY2){
			ID3D11Texture2D* surfaceDX11 = NULL;
			surfaceDX11 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
			deviceContextDX11->UpdateSubresource(surfaceDX11, 0, NULL, t_p_data, t_u32_stride, 0);
		}
		else if (t_fmt == amf::AMF_SURFACE_YUV420P) {
			if (t_n_plane_count == 3) {
				ID3D11Texture2D* surfaceDX11_1 = NULL;
				ID3D11Texture2D* surfaceDX11_2 = NULL;
				ID3D11Texture2D* surfaceDX11_3 = NULL;

				int t_n_u_offset = m_venc_param.width*m_venc_param.height;
				int t_n_v_offset = t_n_u_offset + m_venc_param.width*m_venc_param.height /4;

				surfaceDX11_1 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_1, 0, NULL, t_p_data, t_u32_stride, 0);
				if (m_surface_fmt == amf::AMF_SURFACE_YV12) {
					surfaceDX11_2 = (ID3D11Texture2D*)surface->GetPlaneAt(1)->GetNative(); // no reference counting - do not Release()
					deviceContextDX11->UpdateSubresource(surfaceDX11_2, 0, NULL, t_p_data + t_n_v_offset, t_u32_stride / 2, 0);
					surfaceDX11_3 = (ID3D11Texture2D*)surface->GetPlaneAt(2)->GetNative(); // no reference counting - do not Release()
					deviceContextDX11->UpdateSubresource(surfaceDX11_3, 0, NULL, t_p_data + t_n_u_offset, t_u32_stride / 2, 0);
				}
				else {
					surfaceDX11_2 = (ID3D11Texture2D*)surface->GetPlaneAt(1)->GetNative(); // no reference counting - do not Release()
					deviceContextDX11->UpdateSubresource(surfaceDX11_2, 0, NULL, t_p_data + t_n_u_offset, t_u32_stride / 2, 0);
					surfaceDX11_3 = (ID3D11Texture2D*)surface->GetPlaneAt(2)->GetNative(); // no reference counting - do not Release()
					deviceContextDX11->UpdateSubresource(surfaceDX11_3, 0, NULL, t_p_data + t_n_v_offset, t_u32_stride / 2, 0);
				}

			}
		}
		else if (t_fmt == amf::AMF_SURFACE_BGRA) {
			if (t_n_plane_count == 1) {
				ID3D11Texture2D* surfaceDX11_1 = NULL;

				int t_n_uv_offfset = m_venc_param.width*m_venc_param.height;

				surfaceDX11_1 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_1, 0, NULL, t_p_data, t_u32_stride, 0);
			}
		}
		else if (t_fmt == amf::AMF_SURFACE_RGBA) {
			if (t_n_plane_count == 1) {
				ID3D11Texture2D* surfaceDX11_1 = NULL;

				int t_n_uv_offfset = m_venc_param.width*m_venc_param.height;

				surfaceDX11_1 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
				deviceContextDX11->UpdateSubresource(surfaceDX11_1, 0, NULL, t_p_data, t_u32_stride, 0);
			}
		}

		deviceContextDX11->Flush();

		deviceContextDX11->Release();
	}

	return t_stat;
}
#endif

mw_venc_status_t CMWAMDEncoder::create_encoder_h264(
	mw_venc_param_t *p_param, 
	MW_ENCODER_CALLBACK frame_callback, 
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory *t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK||m_p_context==NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_init_mem(p_param,t_u32_adapter_id);
	if (t_stat != MW_VENC_STATUS_SUCCESS)
		return t_stat;

	t_stat = create_encoder_h264_init_props(t_p_factory, p_param, frame_callback, user_ptr);

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder_h264_ex(
	mw_venc_param_t *p_param, 
	mw_venc_amd_opengl_param_prv_t* p_opengl_param, 
	MW_ENCODER_CALLBACK frame_callback, 
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory *t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK || m_p_context == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_ex_init_mem(p_param, p_opengl_param);
	if (t_stat != MW_VENC_STATUS_SUCCESS)
		return t_stat;

	t_stat = create_encoder_h264_init_props(t_p_factory, p_param, frame_callback, user_ptr);

	return t_stat;
}

mw_venc_status_t 
	CMWAMDEncoder::create_encoder_init_mem(
	mw_venc_param_t *p_param,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;

	if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_DX)
		m_mem_type_in = amf::AMF_MEMORY_DX11;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_CPU)
		m_mem_type_in = amf::AMF_MEMORY_HOST;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_OPENGL)
		m_mem_type_in = amf::AMF_MEMORY_OPENGL;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_VULKAN)
		m_mem_type_in = amf::AMF_MEMORY_VULKAN;
	else
		m_mem_type_in = amf::AMF_MEMORY_DX11;

	if (m_mem_type_in == amf::AMF_MEMORY_DX11||
		m_mem_type_in == amf::AMF_MEMORY_HOST) {
			void* t_p_dx11_device = NULL;
			if (t_u32_adapter_id >= 0) {
				// create dx11 device from given adapter id
				t_p_dx11_device = get_dx11_device(t_u32_adapter_id);
			}
			m_p_d3d11_device = t_p_dx11_device;
			t_res = m_p_context->InitDX11(t_p_dx11_device);
			if (t_res != AMF_OK&&t_res != AMF_ALREADY_INITIALIZED)
				m_mem_type_in = amf::AMF_MEMORY_HOST;
	}

	if (m_mem_type_in == amf::AMF_MEMORY_OPENGL) {
		t_stat = MW_VENC_STATUS_FAIL;
		m_b_create_encoder = false;
		return t_stat;
	}

	if (m_mem_type_in == amf::AMF_MEMORY_VULKAN) {
		void* t_p_vulkan_device = NULL;
		if (t_u32_adapter_id != 0) {
			// create vulkan device from given id
		}
		t_res = amf::AMFContext1Ptr(m_p_context)->InitVulkan(NULL);
		if (t_res != AMF_OK&&t_res != AMF_ALREADY_INITIALIZED) {
			t_stat = MW_VENC_STATUS_FAIL;	// init vulkan failed
			m_b_create_encoder = false;
			return t_stat;
		}
	}

	return t_stat;
}

mw_venc_status_t 
	CMWAMDEncoder::create_encoder_ex_init_mem(
	mw_venc_param_t *p_param, 
	mw_venc_amd_opengl_param_prv_t* p_opengl_param,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;

	if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_DX)
		m_mem_type_in = amf::AMF_MEMORY_DX11;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_CPU)
		m_mem_type_in = amf::AMF_MEMORY_HOST;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_OPENGL)
		m_mem_type_in = amf::AMF_MEMORY_OPENGL;
	else if (p_param->amd_mem_reserved == MW_VENC_AMD_MEM_VULKAN)
		m_mem_type_in = amf::AMF_MEMORY_VULKAN;
	else
		m_mem_type_in = amf::AMF_MEMORY_DX11;

	if (m_mem_type_in == amf::AMF_MEMORY_DX11||
		m_mem_type_in == amf::AMF_MEMORY_HOST) {
			void* t_p_dx11_device = NULL;
			if (t_u32_adapter_id >= 0) {
				// create dx11 device from given adapter id
				t_p_dx11_device = get_dx11_device(t_u32_adapter_id);
			}
			m_p_d3d11_device = t_p_dx11_device;
			t_res = m_p_context->InitDX11(t_p_dx11_device);
			if (t_res != AMF_OK&&t_res != AMF_ALREADY_INITIALIZED)
				m_mem_type_in = amf::AMF_MEMORY_HOST;
	}

	if (m_mem_type_in == amf::AMF_MEMORY_OPENGL) {
		t_res = m_p_context->InitOpenGL(p_opengl_param->m_p_opengl_context,
			p_opengl_param->m_p_window,
			p_opengl_param->m_p_dc);
		if (t_res != AMF_OK&&t_res != AMF_ALREADY_INITIALIZED) {
			t_stat = MW_VENC_STATUS_FAIL;
			m_b_create_encoder = false;
			return t_stat;
		}
	}

	if (m_mem_type_in == amf::AMF_MEMORY_VULKAN) {
		t_res = amf::AMFContext1Ptr(m_p_context)->InitVulkan(NULL);
		if (t_res != AMF_OK&&t_res != AMF_ALREADY_INITIALIZED) {
			t_stat = MW_VENC_STATUS_FAIL;	// init vulkan failed
			m_b_create_encoder = false;
			return t_stat;
		}
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder_h264_init_props(
	amf::AMFFactory *t_p_factory,
	mw_venc_param_t *p_param,
	MW_ENCODER_CALLBACK frame_callback,
	void *user_ptr)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;

	t_res = t_p_factory->CreateComponent(m_p_context, AMFVideoEncoderVCE_AVC, &m_p_encoder);
	if (t_res != AMF_OK || m_p_encoder == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create component failed
		m_b_create_encoder = false;
		return t_stat;
	}

	amf::AMFCaps *t_caps = NULL;
	t_res = m_p_encoder->GetCaps(&t_caps);
	if (t_res == AMF_OK) {
		amf::AMFIOCaps * t_iocaps = NULL;
		t_res = t_caps->GetInputCaps(&t_iocaps);
		if (t_res == AMF_OK) {
			uint32_t t_u32_mem_types = t_iocaps->GetNumOfMemoryTypes();
			amf::AMF_MEMORY_TYPE t_mem_type;
			bool t_b_native = false;
			for (int i = 0; i < t_u32_mem_types; i++) {
				t_res = t_iocaps->GetMemoryTypeAt(i, &t_mem_type, &t_b_native);
				if (t_res == AMF_OK) {

				}
			}

			uint32_t t_u32_formats = t_iocaps->GetNumOfFormats();
			amf::AMF_SURFACE_FORMAT t_fmt;
			for (int i = 0; i < t_u32_formats; i++) {
				t_res = t_iocaps->GetFormatAt(i, &t_fmt, &t_b_native);
				if (t_res == AMF_OK) {
				}
			}
		}
	}

	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set transcode failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_targetusage_t targetusage; //default MW_VENC_TARGETUSAGE_BALANCED
	AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
	switch (p_param->targetusage)
	{
	case MW_VENC_TARGETUSAGE_BEST_QUALITY:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;
		break;
	case MW_VENC_TARGETUSAGE_BALANCED://default
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
		break;
	case MW_VENC_TARGETUSAGE_BEST_SPEED:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;
		break;
	default:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, t_quality);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder quality failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_rate_control_t rate_control;//default MW_VENC_RATECONTROL_CBR 4096k 
	AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
	switch (p_param->rate_control.mode)
	{
	case MW_VENC_RATECONTROL_CBR:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		break;
	case MW_VENC_RATECONTROL_VBR:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
		break;
	case MW_VENC_RATECONTROL_CQP:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
		break;
	default:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rete_control);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set rate control failed
		m_b_create_encoder = false;
		return t_stat;
	}

	// CQP ????
	if (t_rete_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, p_param->rate_control.qpi);
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, p_param->rate_control.qpp);
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, p_param->rate_control.qpb);
	}
	else if (t_rete_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) {
		uint32_t t_u32_bitrate = p_param->rate_control.target_bitrate;
		if (t_u32_bitrate == 0) {
			uint32_t t_u32_resolution = p_param->width*p_param->height;
			if (t_u32_resolution <= 720 * 480)
				t_u32_bitrate = 2 * 1024 * 1024;
			else if (t_u32_resolution <= 1920 * 1080)
				t_u32_bitrate = 4 * 1024 * 1024;
			else if (t_u32_resolution <= 3840 * 2160)
				t_u32_bitrate = 16 * 1024 * 1024;
			else
				t_u32_bitrate = 32 * 1024 * 1024;
		}
		else
			t_u32_bitrate = p_param->rate_control.target_bitrate * 1024;

		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
	}
	else if (t_rete_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
		uint32_t t_u32_max_bitrate = p_param->rate_control.max_bitrate * 1024;
		uint32_t t_u32_bitrate = p_param->rate_control.target_bitrate * 1024;

		if (t_u32_bitrate == 0) {
			uint32_t t_u32_resolution = p_param->width*p_param->height;
			if (t_u32_resolution <= 720 * 480)
				t_u32_bitrate = 2 * 1024 * 1024;
			else if (t_u32_resolution <= 1920 * 1080)
				t_u32_bitrate = 4 * 1024 * 1024;
			else if (t_u32_resolution <= 3840 * 2160)
				t_u32_bitrate = 16 * 1024 * 1024;
			else
				t_u32_bitrate = 32 * 1024 * 1024;

			t_u32_max_bitrate = t_u32_bitrate;
		}

		if (t_u32_bitrate > t_u32_max_bitrate)
			t_u32_max_bitrate = t_u32_bitrate;

		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, t_u32_max_bitrate);
	}
	else {

	}

	//mw_venc_fourcc_t fourcc;
	//int32_t width;
	//int32_t height;
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(p_param->width, p_param->height));
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder frame size failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fps_t fps;//default 60/1
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(p_param->fps.num, p_param->fps.den));
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder frame rate failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t slice_num;//default 0
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, p_param->slice_num);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder slice failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t gop_pic_size;//default 60
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, p_param->gop_pic_size);

	//int32_t gop_ref_size;//default 0, if = 1 -> no b-frame
	//t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, p_param->gop_ref_size);

	//mw_venc_profile_t profile;//defalut MW_VENC_PROFILE_H265_MAIN OR MW_VENC_PROFILE_H264_MAIN
	AMF_VIDEO_ENCODER_PROFILE_ENUM t_profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
	switch (p_param->profile)
	{
	case MW_VENC_PROFILE_H264_BASELINE:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
		break;
	case MW_VENC_PROFILE_H264_MAIN:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
		break;
	case MW_VENC_PROFILE_H264_HIGH:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
		break;
	default:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, t_profile);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder profile failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_level_t level;//MW_VENC_LEVEL_5_1
	amf_int64 t_i64_profile_lvl = 42;
	switch (p_param->level)
	{
	case MW_VENC_LEVEL_2_1:
		t_i64_profile_lvl = 21;
		break;
	case MW_VENC_LEVEL_3_1:
		t_i64_profile_lvl = 31;
		break;
	case MW_VENC_LEVEL_4_1:
		t_i64_profile_lvl = 41;
		break;
	case MW_VENC_LEVEL_5_1:
		t_i64_profile_lvl = 51;
		break;
	case MW_VENC_LEVEL_5_2:
		t_i64_profile_lvl = 52;
		break;
	case MW_VENC_LEVEL_6_1:
		t_i64_profile_lvl = 61;
		break;
	case MW_VENC_LEVEL_6_2:
		t_i64_profile_lvl = 62;
		break;
	default:
		if (p_param->level <= 0)
			t_i64_profile_lvl = 42;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, t_i64_profile_lvl);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder profile lvl failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	bool t_b_fmt_valid = false;
	amf::AMF_SURFACE_FORMAT t_surface_fmt = amf::AMF_SURFACE_NV12;
	switch (p_param->fourcc)
	{
	case MW_VENC_FOURCC_NV12:
		t_surface_fmt = amf::AMF_SURFACE_NV12;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_NV21:
		t_b_fmt_valid = false;
		break;
	case MW_VENC_FOURCC_YV12:
		t_surface_fmt = amf::AMF_SURFACE_YUV420P;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_I420:
		t_surface_fmt = amf::AMF_SURFACE_YUV420P;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_YUY2:
		t_surface_fmt = amf::AMF_SURFACE_YUY2;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_P010:
		t_b_fmt_valid = false;
		break;
	case MW_VENC_FOURCC_BGRA:
		t_surface_fmt = amf::AMF_SURFACE_BGRA;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_RGBA:
		t_surface_fmt = amf::AMF_SURFACE_RGBA;
		t_b_fmt_valid = true;
		break;
	default:
		t_b_fmt_valid = false;
		break;
	}

	if (t_b_fmt_valid == false) {
		t_stat = MW_VENC_STATUS_FAIL;	// fmt unsupport 
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = m_p_encoder->Init(t_surface_fmt, p_param->width, p_param->height);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// init failed
		m_b_create_encoder = false;
		return t_stat;
	}

	m_surface_fmt = t_surface_fmt;
	if (p_param->fourcc == MW_VENC_FOURCC_YV12)
		m_surface_fmt = amf::AMF_SURFACE_YV12;

	m_p_thread = new CPollingThread(m_p_context, m_p_encoder,MW_VENC_CODE_TYPE_H264);
	m_p_thread->set_callback_output(frame_callback, user_ptr);
	m_p_thread->Start();

	m_b_create_encoder = true;

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder_h265(
	mw_venc_param_t *p_param, 
	MW_ENCODER_CALLBACK frame_callback,
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory* t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK || m_p_context == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_init_mem(p_param, t_u32_adapter_id);
	if (t_stat != MW_VENC_STATUS_SUCCESS)
		return t_stat;

	t_res = t_p_factory->CreateComponent(m_p_context, AMFVideoEncoder_HEVC, &m_p_encoder);
	if (t_res != AMF_OK || m_p_encoder == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create component failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_h265_init_props(t_p_factory, p_param, frame_callback, user_ptr);


	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::create_encoder_h265_ex(
	mw_venc_param_t *p_param, 
	mw_venc_amd_opengl_param_prv_t* p_opengl_param, 
	MW_ENCODER_CALLBACK frame_callback, 
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory* t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK || m_p_context == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateComponent(m_p_context, AMFVideoEncoder_HEVC, &m_p_encoder);
	if (t_res != AMF_OK || m_p_encoder == NULL) {
		t_stat = MW_VENC_STATUS_FAIL;	// create component failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_ex_init_mem(p_param, p_opengl_param,t_u32_adapter_id);
	if (t_stat != MW_VENC_STATUS_SUCCESS)
		return t_stat;

	t_stat = create_encoder_h265_init_props(t_p_factory, p_param, frame_callback, user_ptr);

	return t_stat;
}

mw_venc_status_t 
	CMWAMDEncoder::create_encoder_h265_init_props(
	amf::AMFFactory *t_p_factory, 
	mw_venc_param_t *p_param, 
	MW_ENCODER_CALLBACK frame_callback, 
	void *user_ptr)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;

	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set transcode failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_targetusage_t targetusage; //default MW_VENC_TARGETUSAGE_BALANCED
	AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_ENUM t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
	switch (p_param->targetusage)
	{
	case MW_VENC_TARGETUSAGE_BEST_QUALITY:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY;
		break;
	case MW_VENC_TARGETUSAGE_BALANCED://default
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
		break;
	case MW_VENC_TARGETUSAGE_BEST_SPEED:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED;
		break;
	default:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, t_quality);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set quality failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_rate_control_t rate_control;//default MW_VENC_RATECONTROL_CBR 4096k 
	AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
	switch (p_param->rate_control.mode)
	{
	case MW_VENC_RATECONTROL_CBR:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
		break;
	case MW_VENC_RATECONTROL_VBR:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
		break;
	case MW_VENC_RATECONTROL_CQP:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP;
		break;
	default:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, t_rete_control);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set rate control failed
		m_b_create_encoder = false;
		return t_stat;
	}

	// CQP ????
	if (t_rete_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP) {
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_I, p_param->rate_control.qpi);
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_P, p_param->rate_control.qpp);
	}
	else if (t_rete_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR) {
		uint32_t t_u32_bitrate = p_param->rate_control.target_bitrate;
		if (t_u32_bitrate == 0) {
			uint32_t t_u32_resolution = p_param->width*p_param->height;
			if (t_u32_resolution <= 720 * 480)
				t_u32_bitrate = 2 * 1024 * 1024;
			else if (t_u32_resolution <= 1920 * 1080)
				t_u32_bitrate = 4 * 1024 * 1024;
			else if (t_u32_resolution <= 3840 * 2160)
				t_u32_bitrate = 16 * 1024 * 1024;
			else
				t_u32_bitrate = 32 * 1024 * 1024;
		}
		else
			t_u32_bitrate = p_param->rate_control.target_bitrate * 1024;

		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, t_u32_bitrate);
	}
	else if (t_rete_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
		uint32_t t_u32_max_bitrate = p_param->rate_control.max_bitrate * 1024;
		uint32_t t_u32_bitrate = p_param->rate_control.target_bitrate * 1024;

		if (t_u32_bitrate == 0) {
			uint32_t t_u32_resolution = p_param->width*p_param->height;
			if (t_u32_resolution <= 720 * 480)
				t_u32_bitrate = 2 * 1024 * 1024;
			else if (t_u32_resolution <= 1920 * 1080)
				t_u32_bitrate = 4 * 1024 * 1024;
			else if (t_u32_resolution <= 3840 * 2160)
				t_u32_bitrate = 16 * 1024 * 1024;
			else
				t_u32_bitrate = 32 * 1024 * 1024;

			t_u32_max_bitrate = t_u32_bitrate;
		}

		if (t_u32_bitrate > t_u32_max_bitrate)
			t_u32_max_bitrate = t_u32_bitrate;

		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, t_u32_bitrate);
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, t_u32_max_bitrate);
	}
	else {

	}

	//int32_t width;
	//int32_t height;
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(p_param->width, p_param->height));
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder frame size failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fps_t fps;//default 60/1
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(p_param->fps.num, p_param->fps.den));
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder frame rate failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t slice_num;//default 0
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, p_param->slice_num);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder slice failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t gop_pic_size;//default 60
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, p_param->gop_pic_size);

	//int32_t gop_ref_size;//default 0, if = 1 -> no b-frame
	// no interface to set

	//mw_venc_profile_t profile;//defalut MW_VENC_PROFILE_H265_MAIN OR MW_VENC_PROFILE_H264_MAIN
	AMF_VIDEO_ENCODER_HEVC_PROFILE_ENUM t_profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
	switch (p_param->profile)
	{
	case MW_VENC_PROFILE_H265_MAIN:
		t_profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
		break;
	default:
		t_profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE, t_profile);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder profile failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_level_t level;//MW_VENC_LEVEL_5_1
	AMF_VIDEO_ENCODER_LEVEL_ENUM t_profile_lvl = AMF_LEVEL_5_1;
	switch (p_param->level)
	{
	case MW_VENC_LEVEL_2_1:
		t_profile_lvl = AMF_LEVEL_2_1;
		break;
	case MW_VENC_LEVEL_3_1:
		t_profile_lvl = AMF_LEVEL_3_1;
		break;
	case MW_VENC_LEVEL_4_1:
		t_profile_lvl = AMF_LEVEL_4_1;
		break;
	case MW_VENC_LEVEL_5_1:
		t_profile_lvl = AMF_LEVEL_5_1;
		break;
	case MW_VENC_LEVEL_5_2:
		t_profile_lvl = AMF_LEVEL_5_2;
		break;
	case MW_VENC_LEVEL_6_1:
		t_profile_lvl = AMF_LEVEL_6_1;
		break;
	case MW_VENC_LEVEL_6_2:
		t_profile_lvl = AMF_LEVEL_6_2;
		break;
	default:
		if (p_param->level <= 0)
			t_profile_lvl = AMF_LEVEL_5_1;
		else
			t_profile_lvl = (AMF_VIDEO_ENCODER_LEVEL_ENUM)(p_param->level * 3);
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, t_profile_lvl);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// set encoder profile lvl failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fourcc_t fourcc;
	bool t_b_fmt_valid = false;
	amf::AMF_SURFACE_FORMAT t_surface_fmt = amf::AMF_SURFACE_NV12;
	switch (p_param->fourcc)
	{
	case MW_VENC_FOURCC_NV12:
		t_surface_fmt = amf::AMF_SURFACE_NV12;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_NV21:
		t_b_fmt_valid = false;
		break;
	case MW_VENC_FOURCC_YV12:
		t_surface_fmt = amf::AMF_SURFACE_YUV420P;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_I420:
		t_surface_fmt = amf::AMF_SURFACE_YUV420P;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_YUY2:
		t_surface_fmt = amf::AMF_SURFACE_YUY2;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_P010:
		t_b_fmt_valid = false;
		break;
	case MW_VENC_FOURCC_BGRA:
		t_surface_fmt = amf::AMF_SURFACE_BGRA;
		t_b_fmt_valid = true;
		break;
	case MW_VENC_FOURCC_RGBA:
		t_surface_fmt = amf::AMF_SURFACE_RGBA;
		t_b_fmt_valid = true;
		break;
	default:
		t_b_fmt_valid = false;
		break;
	}

	if (t_b_fmt_valid == false) {
		t_stat = MW_VENC_STATUS_FAIL;	// fmt unsupport 
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = m_p_encoder->Init(t_surface_fmt, p_param->width, p_param->height);
	if (t_res != AMF_OK) {
		t_stat = MW_VENC_STATUS_FAIL;	// init failed
		m_b_create_encoder = false;
		return t_stat;
	}

	m_surface_fmt = t_surface_fmt;
	if (p_param->fourcc == MW_VENC_FOURCC_YV12)
		m_surface_fmt = amf::AMF_SURFACE_YV12;

	m_p_thread = new CPollingThread(m_p_context, m_p_encoder,MW_VENC_CODE_TYPE_H265);
	m_p_thread->set_callback_output(frame_callback, user_ptr);
	m_p_thread->Start();

	m_b_create_encoder = true;

	return t_stat;
}

uint32_t CMWAMDEncoder::get_stride(amf::AMF_SURFACE_FORMAT t_fmt, uint32_t t_u32_width)
{
	uint32_t t_u32_stride = 0;

	switch (t_fmt)
	{
	case amf::AMF_SURFACE_NV12:
		t_u32_stride = t_u32_width;
		break;
	case amf::AMF_SURFACE_YV12:
		t_u32_stride = t_u32_width;
		break;
	case amf::AMF_SURFACE_BGRA:
		t_u32_stride = t_u32_width * 4;
		break;
	case amf::AMF_SURFACE_ARGB:
		t_u32_stride = t_u32_width * 4;
		break;
	case amf::AMF_SURFACE_RGBA:
		t_u32_stride = t_u32_width * 4;
		break;
	case amf::AMF_SURFACE_GRAY8:
		t_u32_stride = t_u32_width;
		break;
	case amf::AMF_SURFACE_YUV420P:
		t_u32_stride = t_u32_width;
		break;
	case amf::AMF_SURFACE_U8V8:
		break;
	case amf::AMF_SURFACE_YUY2:
		t_u32_stride = t_u32_width * 2;
		break;
	case amf::AMF_SURFACE_P010:
		t_u32_stride = t_u32_width * 2;
		break;
	case amf::AMF_SURFACE_RGBA_F16:
		t_u32_stride = t_u32_width * 8;
		break;
	case amf::AMF_SURFACE_UYVY:
		t_u32_stride = t_u32_width * 2;
		break;
	default:
		break;
	}

	return t_u32_stride;
}

mw_venc_status_t CMWAMDEncoder::get_h264_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	if (param == MW_VENC_PROPERTY_RATE_CONTROL) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(mw_venc_rate_control_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		mw_venc_rate_control_t* t_p_rate_control = (mw_venc_rate_control_t*)args;
		AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM t_rate_control;
		amf::AMFVariant t_var_rate_control;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, &t_var_rate_control);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}

		t_rate_control = (AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM)t_var_rate_control.ToInt64();
		if (t_rate_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_UNKNOWN;
			uint32_t t_u32_bitrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_CQP;
			amf::AMFVariant t_var_bpi;
			amf::AMFVariant t_var_bpb;
			amf::AMFVariant t_var_bpp;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_QP_I, &t_var_bpi);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_QP_P, &t_var_bpb);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_QP_B, &t_var_bpp);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->qpi = t_var_bpi.ToInt64();
			t_p_rate_control->qpp = t_var_bpp.ToInt64();
			t_p_rate_control->qpb = t_var_bpb.ToInt64();
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_CBR;
			uint32_t t_u32_bitrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_VBR;
			uint32_t t_u32_bitrate;
			uint32_t t_u32_peak_itrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, &t_u32_peak_itrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
			t_p_rate_control->max_bitrate = t_u32_peak_itrate/1024;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_VBR;
			uint32_t t_u32_bitrate;
			uint32_t t_u32_peak_itrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, &t_u32_peak_itrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
			t_p_rate_control->max_bitrate = t_u32_peak_itrate/1024;
		}
		else {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
	}
	else if (param == MW_VENC_PROPERTY_FPS) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(mw_venc_fps_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		AMFRate t_rate;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &t_rate);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		mw_venc_fps_t* t_p_fps = (mw_venc_fps_t*)args;
		t_p_fps->den = t_rate.den;
		t_p_fps->num = t_rate.num;
	}
	else if (param == MW_VENC_PROPERTY_GOP_SIZE) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t t_i32_gop_size;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, &t_i32_gop_size);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		int32_t* t_p_gop_size = (int32_t*)args;
		*t_p_gop_size = t_i32_gop_size;

	}
	else if (param == MW_VENC_PROPERTY_SLICE_NUM) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t t_i32_slice_num;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, &t_i32_slice_num);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		int32_t* t_p_slice_num = (int32_t*)args;
		*t_p_slice_num = t_i32_slice_num;
	}
	else if (param == MW_VENC_PROPERTY_GOP_REF_SIZE) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t t_i32_gop_ref_size;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, &t_i32_gop_ref_size);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		int32_t* t_p_gop_ref_size = (int32_t*)args;
		*t_p_gop_ref_size = t_i32_gop_ref_size;
	}
	else if (param == MW_VENC_PROPERTY_EXTDATA) {
		amf::AMFInterface *pExtraData;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
		if (t_res != AMF_OK || pExtraData == NULL) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		amf::AMFBuffer*	t_p_buffer = (amf::AMFBuffer*)pExtraData;
		amf_size t_size = t_p_buffer->GetSize();
		void* t_p_data = t_p_buffer->GetNative();

		if (m_pu8_extra_data != NULL) {
			free(m_pu8_extra_data);
			m_pu8_extra_data = NULL;
		}
		m_pu8_extra_data = (uint8_t*)malloc(t_size);
		if (m_pu8_extra_data == NULL) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		else {
			mw_venc_extdata_t* t_p_extra_data = (mw_venc_extdata_t*)args;
			t_p_extra_data->p_extdata = m_pu8_extra_data;
			//t_p_extra_data->p_extdata = (uint8_t*)t_p_data;
			//t_p_extra_data->extdata_len = t_size;
			parse_extra_data_h264(t_p_extra_data, (uint8_t*)t_p_data, t_size);
		}
	}
	else if (param == MW_VENC_PROPERTY_FORCE_IDR) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else {
		t_stat = MW_VENC_STATUS_INVALID_PARAM;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::set_h264_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	if (param == MW_VENC_PROPERTY_RATE_CONTROL) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(mw_venc_rate_control_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		mw_venc_rate_control_t* t_p_rate_control = (mw_venc_rate_control_t*)args;
		AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM t_rate_control;
		if (t_p_rate_control->mode == MW_VENC_RATECONTROL_UNKNOWN) {
			t_rate_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rate_control);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate;
			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;
			}
			else
				t_u32_bitrate = t_p_rate_control->target_bitrate * 1024;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_CQP) {
			t_rate_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rate_control);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, t_p_rate_control->qpi);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, t_p_rate_control->qpp);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, t_p_rate_control->qpb);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_CBR) {
			t_rate_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rate_control);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate;
			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;
			}
			else
				t_u32_bitrate = t_p_rate_control->target_bitrate * 1024;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_VBR) {
			t_rate_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rate_control);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			uint32_t t_u32_max_bitrate = t_p_rate_control->max_bitrate*1024;
			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate*1024;

			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;

				t_u32_max_bitrate = t_u32_bitrate;
			}

			if (t_u32_bitrate > t_u32_max_bitrate)
				t_u32_max_bitrate = t_u32_bitrate;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, t_u32_bitrate);
		}
		else {
			t_stat = MW_VENC_STATUS_UNSUPPORT;
			return t_stat;
		}
	}
	else if (param == MW_VENC_PROPERTY_FPS) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if (param == MW_VENC_PROPERTY_GOP_SIZE) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t* t_p_gop_size = (int32_t*)args;
		int32_t t_i32_gop_size = *t_p_gop_size;
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, t_i32_gop_size);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
	}
	else if (param == MW_VENC_PROPERTY_SLICE_NUM) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t* t_p_slice_num = (int32_t*)args;
		int32_t t_i32_slice_num= *t_p_slice_num;
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, t_i32_slice_num);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}

	}
	else if (param == MW_VENC_PROPERTY_GOP_REF_SIZE) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t* t_p_gop_ref_size = (int32_t*)args;
		int32_t t_i32_gop_ref_size = *t_p_gop_ref_size;
		t_res = AMF_FAIL;//m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, t_i32_gop_ref_size);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
	}
	else if (param == MW_VENC_PROPERTY_EXTDATA) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if(param == MW_VENC_PROPERTY_FORCE_IDR)
		m_b_force_idr = (*(bool*)args);
	else {
		t_stat = MW_VENC_STATUS_INVALID_PARAM;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::get_h265_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	if (param == MW_VENC_PROPERTY_RATE_CONTROL) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(mw_venc_rate_control_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		mw_venc_rate_control_t* t_p_rate_control = (mw_venc_rate_control_t*)args;
		AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM t_rate_control;
		amf::AMFVariant t_var_rate_control;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, &t_var_rate_control);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}

		t_rate_control = (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM)t_var_rate_control.ToInt64();
		if (t_rate_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_UNKNOWN;
			uint32_t t_u32_bitrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_CQP;
			amf::AMFVariant t_var_bpi;
			amf::AMFVariant t_var_bpp;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_QP_I, &t_var_bpi);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_QP_P, &t_var_bpp);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			t_p_rate_control->qpi = t_var_bpi.ToInt64();
			t_p_rate_control->qpp = t_var_bpp.ToInt64();
			t_p_rate_control->qpb = 0;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_CBR;
			uint32_t t_u32_bitrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_VBR;
			uint32_t t_u32_bitrate;
			uint32_t t_u32_peak_itrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, &t_u32_peak_itrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
			t_p_rate_control->max_bitrate = t_u32_peak_itrate/1024;
		}
		else if (t_rate_control == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR) {
			t_p_rate_control->mode = MW_VENC_RATECONTROL_VBR;
			uint32_t t_u32_bitrate;
			uint32_t t_u32_peak_itrate;
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &t_u32_bitrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, &t_u32_peak_itrate);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			t_p_rate_control->target_bitrate = t_u32_bitrate/1024;
			t_p_rate_control->max_bitrate = t_u32_peak_itrate/1024;
		}
		else {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}

	}
	else if (param == MW_VENC_PROPERTY_FPS) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(mw_venc_fps_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		AMFRate t_rate;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &t_rate);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		mw_venc_fps_t* t_p_fps = (mw_venc_fps_t*)args;
		t_p_fps->den = t_rate.den;
		t_p_fps->num = t_rate.num;
	}
	else if (param == MW_VENC_PROPERTY_GOP_SIZE) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t t_i32_gop_size;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, &t_i32_gop_size);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		int32_t* t_p_gop_size = (int32_t*)args;
		*t_p_gop_size = t_i32_gop_size;

	}
	else if (param == MW_VENC_PROPERTY_SLICE_NUM) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadWritePtr(args, sizeof(int32_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		int32_t t_i32_slice_num;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, &t_i32_slice_num);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		int32_t* t_p_slice_num = (int32_t*)args;
		*t_p_slice_num = t_i32_slice_num;
	}
	else if (param == MW_VENC_PROPERTY_GOP_REF_SIZE) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if (param == MW_VENC_PROPERTY_EXTDATA) {
		amf::AMFInterface* t_p_interface = NULL;
		t_res = m_p_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &t_p_interface);
		if (t_res != AMF_OK||t_p_interface == NULL) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}
		else {
			amf::AMFBuffer* t_p_buffer = (amf::AMFBuffer*)t_p_interface;
			amf_size t_size = t_p_buffer->GetSize();
			void* t_p_data = t_p_buffer->GetNative();
			if (m_pu8_extra_data != NULL) {
				free(m_pu8_extra_data);
				m_pu8_extra_data = NULL;
			}
			m_pu8_extra_data = (uint8_t*)malloc(t_size);
			if (m_pu8_extra_data == NULL) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}
			else {
				mw_venc_extdata_t* t_p_extra_data = (mw_venc_extdata_t*)args;
				t_p_extra_data->p_extdata = m_pu8_extra_data;
				parse_extra_data_h265(t_p_extra_data, (uint8_t*)t_p_data, t_size);
			}
		}
	}
	else if (param == MW_VENC_PROPERTY_FORCE_IDR) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else {
		t_stat = MW_VENC_STATUS_INVALID_PARAM;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t CMWAMDEncoder::set_h265_property(mw_venc_property_t param, void *args)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;

	AMF_RESULT t_res = AMF_OK;
	if (param == MW_VENC_PROPERTY_RATE_CONTROL) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(mw_venc_rate_control_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		mw_venc_rate_control_t* t_p_rate_control = (mw_venc_rate_control_t*)args;
		AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM t_rate_control;
		if (t_p_rate_control->mode != m_encode_param.rate_control.mode) {
			t_stat = MW_VENC_STATUS_UNSUPPORT;
			return t_stat;
		}

		if (t_p_rate_control->mode == MW_VENC_RATECONTROL_UNKNOWN) {
			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate;
			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;
			}
			else
				t_u32_bitrate = t_p_rate_control->target_bitrate * 1024;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, t_u32_bitrate);
		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_CQP) {
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_I, t_p_rate_control->qpi);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_P, t_p_rate_control->qpp);
			if (t_res != AMF_OK) {
				t_stat = MW_VENC_STATUS_FAIL;
				return t_stat;
			}

		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_CBR) {
			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate;
			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;
			}
			else
				t_u32_bitrate = t_p_rate_control->target_bitrate * 1024;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, t_u32_bitrate);
		}
		else if (t_p_rate_control->mode == MW_VENC_RATECONTROL_VBR) {
			uint32_t t_u32_max_bitrate = t_p_rate_control->max_bitrate*1024;
			uint32_t t_u32_bitrate = t_p_rate_control->target_bitrate*1024;

			if (t_u32_bitrate == 0) {
				uint32_t t_u32_resolution = m_encode_param.width*m_encode_param.height;
				if (t_u32_resolution <= 720 * 480)
					t_u32_bitrate = 2 * 1024 * 1024;
				else if (t_u32_resolution <= 1920 * 1080)
					t_u32_bitrate = 4 * 1024 * 1024;
				else if (t_u32_resolution <= 3840 * 2160)
					t_u32_bitrate = 16 * 1024 * 1024;
				else
					t_u32_bitrate = 32 * 1024 * 1024;

				t_u32_max_bitrate = t_u32_bitrate;
			}

			if (t_u32_bitrate > t_u32_max_bitrate)
				t_u32_max_bitrate = t_u32_bitrate;

			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, t_u32_bitrate);
			t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, t_u32_max_bitrate);
		}
		else {
			t_stat = MW_VENC_STATUS_UNSUPPORT;
			return t_stat;
		}
	}
	else if (param == MW_VENC_PROPERTY_FPS) {
		if (args == NULL) {
			t_stat = MW_VENC_STATUS_INVALID_PARAM;
			return t_stat;
		}
		else {
#ifdef _WIN32
			if (IsBadReadPtr(args, sizeof(mw_venc_fps_t))) {
				t_stat = MW_VENC_STATUS_INVALID_PARAM;
				return t_stat;
			}
#endif
		}

		AMFRate t_rate;
		mw_venc_fps_t* t_p_fps = (mw_venc_fps_t*)args;
		t_rate.den = t_p_fps->den;
		t_rate.num = t_p_fps->num;
		t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, t_rate);
		if (t_res != AMF_OK) {
			t_stat = MW_VENC_STATUS_FAIL;
			return t_stat;
		}

	}
	else if (param == MW_VENC_PROPERTY_GOP_SIZE) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if (param == MW_VENC_PROPERTY_SLICE_NUM) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if (param == MW_VENC_PROPERTY_GOP_REF_SIZE) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if (param == MW_VENC_PROPERTY_EXTDATA) {
		t_stat = MW_VENC_STATUS_UNSUPPORT;
		return t_stat;
	}
	else if(param == MW_VENC_PROPERTY_FORCE_IDR)
		m_b_force_idr = (*(bool*)args);
	else {
		t_stat = MW_VENC_STATUS_INVALID_PARAM;
		return t_stat;
	}

	return t_stat;
}

mw_venc_status_t 
	CMWAMDEncoder::parse_extra_data_h264(
	mw_venc_extdata_t* t_p_extra_data, 
	uint8_t* t_pu8_data, 
	int t_n_size)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;
	vector<int>	t_vec_start;
	uint8_t* t_pu8_nal_data = t_pu8_data;
	int t_n_nal_start = 0;
	int t_n_nal_size = t_n_size;
	while (t_n_nal_start != -1) {
		t_n_nal_start = find_next_nal_start(t_pu8_nal_data, t_n_nal_start, t_n_nal_size);
		if (t_n_nal_start != -1) {
			t_vec_start.push_back(t_n_nal_start);
			if (t_pu8_nal_data[t_n_nal_start + 2] == 0x00)
				t_n_nal_start += 4;
			else
				t_n_nal_start = 3;
		}
	}

	t_p_extra_data->len[0] = 0;
	t_p_extra_data->len[1] = 0;
	t_p_extra_data->len[2] = 0;
	uint32_t t_array_start[3] = {0};

	for (int i = 0; i < t_vec_start.size(); i++) {
		int t_n_end = 0;
		int t_n_start = t_vec_start.at(i);
		if (i == t_vec_start.size() - 1)
			t_n_end = t_n_size;
		else
			t_n_end = t_vec_start.at(i + 1);

		int t_n_len = t_n_end - t_n_start;

		int t_n_nal_type = -1;
		if (t_n_len >= 4) {
			if (t_pu8_data[t_n_start + 2] == 0x00) {
				if (t_n_len > 4)
					t_n_nal_type = t_pu8_data[t_n_start + 4] & 0x1f;
			}
			else if (t_pu8_data[t_n_start + 2] == 0x01)
				t_n_nal_type = t_pu8_data[t_n_start + 3] & 0x1f;

			switch (t_n_nal_type)
			{
			case MW_VENC_AMD_H264_NAL_TYPE_SPS:
				t_p_extra_data->len[1] = t_n_len;
				t_array_start[1] = t_n_start;
				break;
			case MW_VENC_AMD_H264_NAL_TYPE_PPS:
				t_p_extra_data->len[2] = t_n_len;
				t_array_start[2] = t_n_start;
				break;
			default:
				break;
			}
		}
	}

	t_p_extra_data->extdata_len = t_p_extra_data->len[0] + t_p_extra_data->len[1] + t_p_extra_data->len[2];
	if (t_p_extra_data->len[0] != 0) 
		memcpy(t_p_extra_data->p_extdata, t_pu8_data + t_array_start[0], t_p_extra_data->len[0]);
	if (t_p_extra_data->len[1] != 0)
		memcpy(t_p_extra_data->p_extdata + t_p_extra_data->len[0], t_pu8_data + t_array_start[1], t_p_extra_data->len[1]);
	if (t_p_extra_data->len[2] != 0)
		memcpy(t_p_extra_data->p_extdata + t_p_extra_data->len[0] + t_p_extra_data->len[1],
		t_pu8_data + t_array_start[2], t_p_extra_data->len[2]);

	return t_stat;
}

int CMWAMDEncoder::find_next_nal_start(
	uint8_t* t_pu8_data,
	int t_n_start,
	int t_n_size)
{
	int t_n_ret = -1;
	if (t_n_size-t_n_start < 4)
		return t_n_ret;

	for (int i = t_n_start; i < t_n_size - 4; i++) {
		if (t_pu8_data[i] == 0x00 && t_pu8_data[i + 1] == 0x00 && t_pu8_data[i + 2] == 0x01 ||
			t_pu8_data[i] == 0x00 && t_pu8_data[i + 1] == 0x00 && t_pu8_data[i + 2] == 0x00 && t_pu8_data[i + 3] == 0x01) {
				t_n_ret = i;
				break;
		}
	}

	return t_n_ret;
}

mw_venc_status_t 
	CMWAMDEncoder::parse_extra_data_h265(
	mw_venc_extdata_t* t_p_extra_data, 
	uint8_t* t_pu8_data, 
	int t_n_size)
{
	mw_venc_status_t t_stat = MW_VENC_STATUS_SUCCESS;
	vector<int>	t_vec_start;
	uint8_t* t_pu8_nal_data = t_pu8_data;
	int t_n_nal_start = 0;
	int t_n_nal_size = t_n_size;
	while (t_n_nal_start != -1) {
		t_n_nal_start = find_next_nal_start(t_pu8_nal_data, t_n_nal_start, t_n_nal_size);
		if (t_n_nal_start != -1) {
			t_vec_start.push_back(t_n_nal_start);
			if (t_pu8_nal_data[t_n_nal_start + 2] == 0x00)
				t_n_nal_start += 4;
			else
				t_n_nal_start = 3;
		}
	}

	t_p_extra_data->len[0] = 0;
	t_p_extra_data->len[1] = 0;
	t_p_extra_data->len[2] = 0;
	uint32_t t_array_start[3] = { 0 };

	for (int i = 0; i < t_vec_start.size(); i++) {
		int t_n_end = 0;
		int t_n_start = t_vec_start.at(i);
		if (i == t_vec_start.size() - 1)
			t_n_end = t_n_size;
		else
			t_n_end = t_vec_start.at(i + 1);

		int t_n_len = t_n_end - t_n_start;

		int t_n_nal_type = -1;
		if (t_n_len >= 4) {
			if (t_pu8_data[t_n_start + 2] == 0x00) {
				if (t_n_len > 4)
					t_n_nal_type = (t_pu8_data[t_n_start + 4] & 0x7e)>>1;
			}
			else if (t_pu8_data[t_n_start + 2] == 0x01) 
				t_n_nal_type = (t_pu8_data[t_n_start + 3] & 0x1f)>>1;

			switch (t_n_nal_type)
			{
			case MW_VENC_AMD_H265_NAL_UNIT_VPS:
				t_p_extra_data->len[0] = t_n_len;
				t_array_start[0] = t_n_start;
				break;
			case MW_VENC_AMD_H265_NAL_UNIT_SPS:
				t_p_extra_data->len[1] = t_n_len;
				t_array_start[1] = t_n_start;
				break;
			case MW_VENC_AMD_H265_NAL_UNIT_PPS:
				t_p_extra_data->len[2] = t_n_len;
				t_array_start[2] = t_n_start;
				break;
			default:
				break;
			}
		}
	}

	t_p_extra_data->extdata_len = t_p_extra_data->len[0] + t_p_extra_data->len[1] + t_p_extra_data->len[2];
	if (t_p_extra_data->len[0] != 0)
		memcpy(t_p_extra_data->p_extdata, t_pu8_data + t_array_start[0], t_p_extra_data->len[0]);
	if (t_p_extra_data->len[1] != 0)
		memcpy(t_p_extra_data->p_extdata + t_p_extra_data->len[0], t_pu8_data + t_array_start[1], t_p_extra_data->len[1]);
	if (t_p_extra_data->len[2] != 0)
		memcpy(t_p_extra_data->p_extdata + t_p_extra_data->len[0] + t_p_extra_data->len[1],
		t_pu8_data + t_array_start[2], t_p_extra_data->len[2]);

	return t_stat;
}

void* CMWAMDEncoder::get_dx11_device(int32_t t_i32_adapter_id)
{
	void* t_p_dx11_device = NULL;

#ifdef _WIN32
	do
	{
		IDXGIFactory* t_p_dxgi_factory = NULL;
		HRESULT t_hr = S_OK;
		t_hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&t_p_dxgi_factory);
		if (FAILED(t_hr) || t_p_dxgi_factory == NULL)
			break;

		IDXGIAdapter* t_p_dxgi_adapter = NULL;
		t_hr = t_p_dxgi_factory->EnumAdapters(t_i32_adapter_id, &t_p_dxgi_adapter);
		if (FAILED(t_hr) || t_p_dxgi_adapter == NULL) {
			t_p_dxgi_factory->Release();
			t_p_dxgi_factory = NULL;
			break;
		}

		ID3D11Device* t_p_d3d11_device = NULL;
		ID3D11DeviceContext* t_p_d3d11_device_context = NULL;
		UINT t_ui_create_device_flags = 0;
		HMONITOR t_h_monitor = NULL;
		DWORD t_dw_vp = 0;
		D3D_FEATURE_LEVEL t_array_d3d_feature_levels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};
		D3D_FEATURE_LEVEL t_d3d_feature_level = D3D_FEATURE_LEVEL_11_0;
		D3D_DRIVER_TYPE t_d3d_driver_type = D3D_DRIVER_TYPE_UNKNOWN;

		t_hr = D3D11CreateDevice(
			t_p_dxgi_adapter,
			t_d3d_driver_type,
			NULL,
			t_ui_create_device_flags,
			t_array_d3d_feature_levels,
			_countof(t_array_d3d_feature_levels),
			D3D11_SDK_VERSION,
			&t_p_d3d11_device,
			&t_d3d_feature_level,
			&t_p_d3d11_device_context);
		// init dx11 failed to create hw dx11.1 device

		if (FAILED(t_hr)) {
			t_hr = D3D11CreateDevice(
				t_p_dxgi_adapter,
				t_d3d_driver_type,
				NULL,
				t_ui_create_device_flags,
				t_array_d3d_feature_levels + 1,
				_countof(t_array_d3d_feature_levels) - 1,
				D3D11_SDK_VERSION,
				&t_p_d3d11_device,
				&t_d3d_feature_level,
				&t_p_d3d11_device_context);
			// init dx11 failed to create hw dx11 device
		}
		if (FAILED(t_hr)) {
			t_hr = D3D11CreateDevice(
				NULL,
				D3D_DRIVER_TYPE_SOFTWARE,
				NULL,
				t_ui_create_device_flags,
				t_array_d3d_feature_levels,
				_countof(t_array_d3d_feature_levels),
				D3D11_SDK_VERSION,
				&t_p_d3d11_device,
				&t_d3d_feature_level,
				&t_p_d3d11_device_context);
			// init dx11 failed to create sw dx11.1 device
		}
		if (FAILED(t_hr)) {
			t_hr = D3D11CreateDevice(
				NULL,
				D3D_DRIVER_TYPE_SOFTWARE,
				NULL,
				t_ui_create_device_flags,
				t_array_d3d_feature_levels + 1,
				_countof(t_array_d3d_feature_levels) - 1,
				D3D11_SDK_VERSION,
				&t_p_d3d11_device,
				&t_d3d_feature_level,
				&t_p_d3d11_device_context);

			// init dx11 failed to create sw dx11 device
		}

		ID3D10Multithread* t_p_d3d10_multi_thread = NULL;
		if (t_p_d3d11_device != NULL) {
			t_hr = t_p_d3d11_device->QueryInterface(
				__uuidof(ID3D10Multithread),
				reinterpret_cast<void**>(&t_p_d3d10_multi_thread));
			if (t_p_d3d10_multi_thread)
				t_p_d3d10_multi_thread->SetMultithreadProtected(true);
		}
		t_p_dx11_device = (void*)t_p_d3d11_device;
		if (t_p_dxgi_factory != NULL) {
			t_p_dxgi_factory->Release();
			t_p_dxgi_factory = NULL;
		}
	} while (false);

#endif
	return t_p_dx11_device;
}
