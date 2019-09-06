#include "whh_amf_venc.h"

#ifdef _WIN32
#include "dxgi.h"
#pragma comment(lib,"DXGI.lib")
#include "vector"
using namespace std;
#endif

int32_t					g_amd_gpu_num = 0;
whh_venc_gpu_info_s*		g_p_amd_gpu_list = NULL;

void convert_wchar_to_char(WCHAR* t_p_wcs, char* t_p_cs);

void whh_venc_init()
{
	IDXGIFactory*	t_p_dxgi_factory = NULL;
	HRESULT t_hr = S_OK;
	t_hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&t_p_dxgi_factory);
	if (FAILED(t_hr)) 
		return;

	UINT t_ui_count = 0;
	vector<whh_venc_gpu_info_s> t_vec_amd_gpu;
	while (true){
		IDXGIAdapter*	t_p_adapter = NULL;
		if(t_p_dxgi_factory->EnumAdapters(t_ui_count,&t_p_adapter) == DXGI_ERROR_NOT_FOUND)
			break;

		DXGI_ADAPTER_DESC t_adapter_desc;
		t_p_adapter->GetDesc(&t_adapter_desc);

		// AMD Graphics
		if (t_adapter_desc.VendorId == 0x1002) {
			// not only with outputs
			whh_venc_gpu_info_s t_amd_gpu_info;
			convert_wchar_to_char(t_adapter_desc.Description,t_amd_gpu_info.gpu_name);
			t_amd_gpu_info.device_id = t_ui_count;
			t_amd_gpu_info.index = t_vec_amd_gpu.size();
			t_vec_amd_gpu.push_back(t_amd_gpu_info);
			g_amd_gpu_num++;
		}

		t_ui_count++;
	}

	int t_n_amd_gpu_num = t_vec_amd_gpu.size();
	if (t_n_amd_gpu_num > 0) {
		if (g_p_amd_gpu_list != NULL) {
			free(g_p_amd_gpu_list);
			g_p_amd_gpu_list = NULL;
		}
		g_p_amd_gpu_list = (whh_venc_gpu_info_s *)malloc(sizeof(whh_venc_gpu_info_s) * t_n_amd_gpu_num);
		if (g_p_amd_gpu_list != NULL) {
			for (int i = 0; i < t_n_amd_gpu_num; i++) {
				g_p_amd_gpu_list[i] = t_vec_amd_gpu.at(i);
			}
		}
	}

	if (t_p_dxgi_factory != NULL) {
		t_p_dxgi_factory->Release();
		t_p_dxgi_factory = NULL;
	}
}


void convert_wchar_to_char(WCHAR* t_p_wcs, char* t_p_cs)
{
	int t_n_len = WideCharToMultiByte(CP_ACP, 0, t_p_wcs, -1, NULL, 0, NULL, NULL);
	// may check the length

	WideCharToMultiByte(CP_ACP, 0, t_p_wcs, -1, t_p_cs, t_n_len, NULL, NULL);
}

int whh_venc_get_amd_gpu_num()
{
	return g_amd_gpu_num;
}

bool whh_venc_get_gpu_info_by_index(
	int t_n_index, 
	whh_venc_gpu_info_s* t_p_info)
{
	if(t_n_index<0||t_n_index>g_amd_gpu_num)
		return false;

	if(t_p_info){

		memcpy(t_p_info->gpu_name,g_p_amd_gpu_list[t_n_index].gpu_name,128);
		t_p_info->index = g_p_amd_gpu_list[t_n_index].index;

		return true;
	}

	return false;
}

void whh_venc_deinit()
{
	g_amd_gpu_num = 0;
	if (g_p_amd_gpu_list)
		free(g_p_amd_gpu_list);
}


CPollingThread::CPollingThread(
	amf::AMFContext* context,
	amf::AMFComponent* encoder, 
	whh_venc_code_type_e t_code_type)
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

			amf_pts t_pts=t_p_data->GetPts();

			amf::AMFBufferPtr t_p_buffer(t_p_data);//query for buffer interface

			if (m_frame_callback != NULL) {
				m_frame_callback(
					m_p_user_ptr,
					(const uint8_t*)(t_p_buffer->GetNative()),
					t_p_buffer->GetSize());
			}
		}
		else
			amf_sleep(1);
	}
}

void CPollingThread::set_callback_output(WHH_ENCODER_CALLBACK frame_callback, void *user_ptr)
{
	m_frame_callback = frame_callback;
	m_p_user_ptr = user_ptr;
}

CWHHAMDEncoder::CWHHAMDEncoder()
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

CWHHAMDEncoder::~CWHHAMDEncoder()
{
	destory_encoder();
}

bool CWHHAMDEncoder::init()
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;
	t_res = g_amf_factory.init();
	if (t_res != AMF_OK) {
		t_stat = false;
		m_b_init = false;
	}
	else
		m_b_init = true;

	return t_stat;
}

bool CWHHAMDEncoder::deinit()
{
	bool t_stat = true;

	if (m_b_init) {
		g_amf_factory.terminate();
		m_b_init = false;
	}

	return t_stat;
}

bool CWHHAMDEncoder::create_encoder(
	whh_venc_param_s *p_param, 
	WHH_ENCODER_CALLBACK frame_callback,
	void *user_ptr,
	int32_t index)
{
	bool t_stat = true;

	if (!m_b_init) {
		t_stat = false;
		m_b_create_encoder = false;
		return t_stat;
	}

	m_venc_param = *p_param;
	m_encode_param = m_venc_param;
	m_u32_width = m_encode_param.width;
	m_u32_height = m_encode_param.height;

	if (m_venc_param.code_type == WHH_VENC_CODE_TYPE_H264)
		t_stat = create_encoder_h264(p_param, frame_callback, user_ptr,index);
	else if (m_venc_param.code_type == WHH_VENC_CODE_TYPE_H265)
		t_stat = create_encoder_h265(p_param, frame_callback, user_ptr,index);
	else
		t_stat = false;

	return t_stat;
}

bool CWHHAMDEncoder::destory_encoder()
{
	bool t_stat = true;

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

bool CWHHAMDEncoder::put_frame(uint8_t *p_frame)
{
	bool t_stat = true;

	if (!m_b_create_encoder) {
		t_stat = false;
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
			t_stat = false;
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
		if (m_encode_param.code_type == WHH_VENC_CODE_TYPE_H264) {
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
					t_stat = false;
					return t_stat;
				}

				m_surface_in = NULL;
			}

			if (t_u8_sub_try > 1000) {
				t_stat = false;
				return t_stat;
			}		
		}
#ifdef USE_TIME
		t_submit_end = clock();
		//printf("alloc:%ld fill:%ld submit:%ld try:%d\r", t_alloc_end - t_alloc_begin, t_fill_end - t_fill_begin, t_submit_end - t_submit_begin, t_u8_sub_try);
#else
		t_submit_end = amf_high_precision_clock();
		//printf("alloc:%lld fill:%lld submit:%lld try:%d\n", t_alloc_end - t_alloc_begin, t_fill_end - t_fill_begin, t_submit_end - t_submit_begin, t_u8_sub_try);
#endif
	}

	return t_stat;
}

bool CWHHAMDEncoder::filldata_surface_host(amf::AMFContext *context, amf::AMFSurface *surface, unsigned char* t_p_data)
{
	bool t_stat = true;

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
bool CWHHAMDEncoder::filldata_surface_dx11(amf::AMFContext *context, amf::AMFSurface *surface, unsigned char* t_p_data)
{
	bool t_stat = true;
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
		if (m_venc_param.code_type == WHH_VENC_CODE_TYPE_H264)
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

bool CWHHAMDEncoder::create_encoder_h264(
	whh_venc_param_s *p_param, 
	WHH_ENCODER_CALLBACK frame_callback, 
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory *t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = false;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK||m_p_context==NULL) {
		t_stat = false;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_init_mem(p_param,t_u32_adapter_id);
	if (t_stat != true)
		return t_stat;

	t_stat = create_encoder_h264_init_props(t_p_factory, p_param, frame_callback, user_ptr);

	return t_stat;
}

bool 
	CWHHAMDEncoder::create_encoder_init_mem(
	whh_venc_param_s *p_param,
	int32_t t_u32_adapter_id)
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;

	if (p_param->amd_mem_reserved == WHH_VENC_AMD_MEM_DX)
		m_mem_type_in = amf::AMF_MEMORY_DX11;
	else if (p_param->amd_mem_reserved == WHH_VENC_AMD_MEM_CPU)
		m_mem_type_in = amf::AMF_MEMORY_HOST;
	else if (p_param->amd_mem_reserved == WHH_VENC_AMD_MEM_OPENGL)
		m_mem_type_in = amf::AMF_MEMORY_OPENGL;
	else if (p_param->amd_mem_reserved == WHH_VENC_AMD_MEM_VULKAN)
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
		t_stat = false;
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
			t_stat = false;	// init vulkan failed
			m_b_create_encoder = false;
			return t_stat;
		}
	}

	return t_stat;
}

bool CWHHAMDEncoder::create_encoder_h264_init_props(
	amf::AMFFactory *t_p_factory,
	whh_venc_param_s *p_param,
	WHH_ENCODER_CALLBACK frame_callback,
	void *user_ptr)
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;

	t_res = t_p_factory->CreateComponent(m_p_context, AMFVideoEncoderVCE_AVC, &m_p_encoder);
	if (t_res != AMF_OK || m_p_encoder == NULL) {
		t_stat = false;	// create component failed
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
		t_stat = false;	// set transcode failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_targetusage_t targetusage; //default MW_VENC_TARGETUSAGE_BALANCED
	AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
	switch (p_param->targetusage)
	{
	case WHH_VENC_TARGETUSAGE_BEST_QUALITY:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;
		break;
	case WHH_VENC_TARGETUSAGE_BALANCED://default
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
		break;
	case WHH_VENC_TARGETUSAGE_BEST_SPEED:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;
		break;
	default:
		t_quality = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, t_quality);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder quality failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_rate_control_t rate_control;//default MW_VENC_RATECONTROL_CBR 4096k 
	AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
	switch (p_param->rate_control.mode)
	{
	case WHH_VENC_RATECONTROL_CBR:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		break;
	default:
		t_rete_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, t_rete_control);
	if (t_res != AMF_OK) {
		t_stat = false;	// set rate control failed
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
		t_stat = false;	// set encoder frame size failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fps_t fps;//default 60/1
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(p_param->fps.num, p_param->fps.den));
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder frame rate failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t slice_num;//default 0
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, p_param->slice_num);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder slice failed 
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
	case WHH_VENC_PROFILE_H264_BASELINE:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
		break;
	case WHH_VENC_PROFILE_H264_MAIN:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
		break;
	case WHH_VENC_PROFILE_H264_HIGH:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
		break;
	default:
		t_profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, t_profile);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder profile failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_level_t level;//MW_VENC_LEVEL_5_1
	amf_int64 t_i64_profile_lvl = 42;
	switch (p_param->level)
	{
	case WHH_VENC_LEVEL_2_1:
		t_i64_profile_lvl = 21;
		break;
	case WHH_VENC_LEVEL_3_1:
		t_i64_profile_lvl = 31;
		break;
	case WHH_VENC_LEVEL_4_1:
		t_i64_profile_lvl = 41;
		break;
	case WHH_VENC_LEVEL_5_1:
		t_i64_profile_lvl = 51;
		break;
	case WHH_VENC_LEVEL_5_2:
		t_i64_profile_lvl = 52;
		break;
	case WHH_VENC_LEVEL_6_1:
		t_i64_profile_lvl = 61;
		break;
	case WHH_VENC_LEVEL_6_2:
		t_i64_profile_lvl = 62;
		break;
	default:
		if (p_param->level <= 0)
			t_i64_profile_lvl = 42;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, t_i64_profile_lvl);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder profile lvl failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	bool t_b_fmt_valid = false;
	amf::AMF_SURFACE_FORMAT t_surface_fmt = amf::AMF_SURFACE_NV12;
	switch (p_param->fourcc)
	{
	case WHH_VENC_FOURCC_NV12:
		t_surface_fmt = amf::AMF_SURFACE_NV12;
		t_b_fmt_valid = true;
		break;
	default:
		t_b_fmt_valid = false;
		break;
	}

	if (t_b_fmt_valid == false) {
		t_stat = false;	// fmt unsupport 
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = m_p_encoder->Init(t_surface_fmt, p_param->width, p_param->height);
	if (t_res != AMF_OK) {
		t_stat = false;	// init failed
		m_b_create_encoder = false;
		return t_stat;
	}

	m_surface_fmt = t_surface_fmt;

	m_p_thread = new CPollingThread(m_p_context, m_p_encoder,WHH_VENC_CODE_TYPE_H264);
	m_p_thread->set_callback_output(frame_callback, user_ptr);
	m_p_thread->Start();

	m_b_create_encoder = true;

	return t_stat;
}

bool CWHHAMDEncoder::create_encoder_h265(
	whh_venc_param_s *p_param, 
	WHH_ENCODER_CALLBACK frame_callback,
	void *user_ptr,
	int32_t t_u32_adapter_id)
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;
	amf::AMFFactory* t_p_factory = NULL;
	t_p_factory = g_amf_factory.get_factory();
	if (t_p_factory == NULL) {
		t_stat = false;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = t_p_factory->CreateContext(&m_p_context);
	if (t_res != AMF_OK || m_p_context == NULL) {
		t_stat = false;	// create context failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_init_mem(p_param, t_u32_adapter_id);
	if (t_stat != true)
		return t_stat;

	t_res = t_p_factory->CreateComponent(m_p_context, AMFVideoEncoder_HEVC, &m_p_encoder);
	if (t_res != AMF_OK || m_p_encoder == NULL) {
		t_stat = false;	// create component failed
		m_b_create_encoder = false;
		return t_stat;
	}

	t_stat = create_encoder_h265_init_props(t_p_factory, p_param, frame_callback, user_ptr);


	return t_stat;
}

bool 
	CWHHAMDEncoder::create_encoder_h265_init_props(
	amf::AMFFactory *t_p_factory, 
	whh_venc_param_s *p_param, 
	WHH_ENCODER_CALLBACK frame_callback, 
	void *user_ptr)
{
	bool t_stat = true;

	AMF_RESULT t_res = AMF_OK;

	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
	if (t_res != AMF_OK) {
		t_stat = false;	// set transcode failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_targetusage_t targetusage; //default MW_VENC_TARGETUSAGE_BALANCED
	AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_ENUM t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
	switch (p_param->targetusage)
	{
	case WHH_VENC_TARGETUSAGE_BEST_QUALITY:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY;
		break;
	case WHH_VENC_TARGETUSAGE_BALANCED://default
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
		break;
	case WHH_VENC_TARGETUSAGE_BEST_SPEED:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED;
		break;
	default:
		t_quality = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, t_quality);
	if (t_res != AMF_OK) {
		t_stat = false;	// set quality failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_rate_control_t rate_control;//default MW_VENC_RATECONTROL_CBR 4096k 
	AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
	switch (p_param->rate_control.mode)
	{
	case WHH_VENC_RATECONTROL_CBR:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
		break;
	default:
		t_rete_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, t_rete_control);
	if (t_res != AMF_OK) {
		t_stat = false;	// set rate control failed
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
		t_stat = false;	// set encoder frame size failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fps_t fps;//default 60/1
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(p_param->fps.num, p_param->fps.den));
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder frame rate failed
		m_b_create_encoder = false;
		return t_stat;
	}

	//int32_t slice_num;//default 0
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, p_param->slice_num);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder slice failed 
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
	case WHH_VENC_PROFILE_H265_MAIN:
		t_profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
		break;
	default:
		t_profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
		break;
	}
	t_res = m_p_encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE, t_profile);
	if (t_res != AMF_OK) {
		t_stat = false;	// set encoder profile failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_level_t level;//MW_VENC_LEVEL_5_1
	AMF_VIDEO_ENCODER_LEVEL_ENUM t_profile_lvl = AMF_LEVEL_5_1;
	switch (p_param->level)
	{
	case WHH_VENC_LEVEL_2_1:
		t_profile_lvl = AMF_LEVEL_2_1;
		break;
	case WHH_VENC_LEVEL_3_1:
		t_profile_lvl = AMF_LEVEL_3_1;
		break;
	case WHH_VENC_LEVEL_4_1:
		t_profile_lvl = AMF_LEVEL_4_1;
		break;
	case WHH_VENC_LEVEL_5_1:
		t_profile_lvl = AMF_LEVEL_5_1;
		break;
	case WHH_VENC_LEVEL_5_2:
		t_profile_lvl = AMF_LEVEL_5_2;
		break;
	case WHH_VENC_LEVEL_6_1:
		t_profile_lvl = AMF_LEVEL_6_1;
		break;
	case WHH_VENC_LEVEL_6_2:
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
		t_stat = false;	// set encoder profile lvl failed 
		m_b_create_encoder = false;
		return t_stat;
	}

	//mw_venc_fourcc_t fourcc;
	bool t_b_fmt_valid = false;
	amf::AMF_SURFACE_FORMAT t_surface_fmt = amf::AMF_SURFACE_NV12;
	switch (p_param->fourcc)
	{
	case WHH_VENC_FOURCC_NV12:
		t_surface_fmt = amf::AMF_SURFACE_NV12;
		t_b_fmt_valid = true;
		break;
	default:
		t_b_fmt_valid = false;
		break;
	}

	if (t_b_fmt_valid == false) {
		t_stat = false;	// fmt unsupport 
		m_b_create_encoder = false;
		return t_stat;
	}

	t_res = m_p_encoder->Init(t_surface_fmt, p_param->width, p_param->height);
	if (t_res != AMF_OK) {
		t_stat = false;	// init failed
		m_b_create_encoder = false;
		return t_stat;
	}

	m_surface_fmt = t_surface_fmt;

	m_p_thread = new CPollingThread(m_p_context, m_p_encoder,WHH_VENC_CODE_TYPE_H265);
	m_p_thread->set_callback_output(frame_callback, user_ptr);
	m_p_thread->Start();

	m_b_create_encoder = true;

	return t_stat;
}

uint32_t CWHHAMDEncoder::get_stride(amf::AMF_SURFACE_FORMAT t_fmt, uint32_t t_u32_width)
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

void* CWHHAMDEncoder::get_dx11_device(int32_t t_i32_adapter_id)
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
#if(_MS_VER>=1900)
			D3D_FEATURE_LEVEL_11_1,
#endif
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

