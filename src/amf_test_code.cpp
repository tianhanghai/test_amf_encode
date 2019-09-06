#include <stdio.h>
#ifdef _WIN32
#include "conio.h"
#include "windows.h"
#include "time.h"
#include "vector"
using namespace std;
#endif
#include "src_whh_venc/whh_amf_venc.h"

// resolution
// 0 1280x720
// 1 1920x1080
// 2 3840x2160

uint8_t**			g_pp_frames = NULL;
int					g_n_frames = 30;
bool				g_b_running = false;
whh_venc_gpu_info_s	g_gpu;
whh_venc_param_s	g_param;



struct whh_thread_fps_s {
	clock_t			m_time;
	int				m_n_encode;
	float			m_f_fps;
};

whh_thread_fps_s*	g_array_fps = NULL;
int					g_n_thread = 0;

void printf_usage();
whh_venc_gpu_info_s get_specified_card();
whh_venc_amd_mem_type_e get_amd_mem_type();
whh_venc_code_type_e get_code_type();
whh_venc_fourcc_e get_input_fourcc();
int get_resolution();
int get_thread_nums();

bool load_file(whh_venc_fourcc_e t_fourcc, int t_n_resolution);
void unload_file();
#ifdef _WIN32
DWORD WINAPI thread_test_encode(LPVOID lpThreadParameter);
DWORD WINAPI thread_print_fps(LPVOID lpThreadParameter);
#endif
void test_encode(
	whh_venc_gpu_info_s t_gpu,
	whh_venc_param_s t_param,
	whh_thread_fps_s*	t_p_fps);
void data_callback(
	void* t_p_param,
	const uint8_t* t_p_frame,
	uint32_t t_u32_frame_len
);

int main()
{
	int t_n_ret = 0;
	whh_venc_param_s t_venc_param;

	printf_usage();

	// init me_venc and get graphic cards info
	whh_venc_init();

	// select graphic adapter
	whh_venc_gpu_info_s t_selected_adapter;
	t_selected_adapter.index = -1;
	t_selected_adapter = get_specified_card();
	if (t_selected_adapter.index == -1) {
		printf("There is no suitable graphic adapter.\n");
		goto end;
	}

	// select amd mem type
	whh_venc_amd_mem_type_e t_amd_mem_type = WHH_VENC_AMD_MEM_DX;
	t_amd_mem_type = get_amd_mem_type();

	// select code type
	whh_venc_code_type_e t_code_type = WHH_VENC_CODE_TYPE_H264;
	t_code_type = get_code_type();

	// select fourcc
	whh_venc_fourcc_e t_fourcc = WHH_VENC_FOURCC_NV12;
	t_fourcc = get_input_fourcc();

	// select resolution
	int t_n_resolution = -1;
	t_n_resolution = get_resolution();

	int t_n_thread = get_thread_nums();

	// load file
	bool t_b_load_file = false;
	t_b_load_file = load_file(t_fourcc, t_n_resolution);

	// test encode
	if (t_b_load_file) {
		t_venc_param.amd_mem_reserved = t_amd_mem_type;
		t_venc_param.code_type = t_code_type;
		if (t_venc_param.code_type == WHH_VENC_CODE_TYPE_H265)
			t_venc_param.profile = WHH_VENC_PROFILE_H265_MAIN;
		else
			t_venc_param.profile = WHH_VENC_PROFILE_H264_BASELINE;
		t_venc_param.fourcc = t_fourcc;
		t_venc_param.fps.num = 60;
		t_venc_param.fps.den = 1;
		t_venc_param.gop_pic_size = 30;
		t_venc_param.gop_ref_size = 1;
		t_venc_param.level = WHH_VENC_LEVEL_5_2;
		t_venc_param.rate_control.mode = WHH_VENC_RATECONTROL_CBR;
		if (t_n_resolution == 0) {
			t_venc_param.height = 720;
			t_venc_param.width = 1280;
			t_venc_param.rate_control.target_bitrate = 2048;
		}
		else if (t_n_resolution == 1) {
			t_venc_param.height = 1080;
			t_venc_param.width = 1920;
			t_venc_param.rate_control.target_bitrate = 4096;
		}
		else if (t_n_resolution == 2) {
			t_venc_param.height = 2160;
			t_venc_param.width = 3840;
			t_venc_param.rate_control.target_bitrate = 16384;
		}
		t_venc_param.slice_num = 1;
		t_venc_param.targetusage = WHH_VENC_TARGETUSAGE_BEST_SPEED;
	}



	g_gpu = t_selected_adapter;
	g_param = t_venc_param;
	
	HANDLE* t_array_handle = new HANDLE[t_n_thread + 1];
	whh_thread_fps_s* t_array_fps = new whh_thread_fps_s[t_n_thread];
	int t_n_ch = 0;
	do 
	{
		g_array_fps = t_array_fps;
		g_n_thread = t_n_thread;
		HANDLE t_h_handle = CreateThread(NULL, 0, thread_print_fps, NULL, 0, NULL);
		t_array_handle[t_n_thread] = t_h_handle;

		for (int i = 0; i < t_n_thread; i++) {
			t_array_handle[i] = NULL;
			t_array_fps[i].m_time = 0;
			t_array_fps[i].m_n_encode = 0;
			t_array_fps[i].m_f_fps = 0;
			HANDLE t_h_thread = CreateThread(NULL, 0, thread_test_encode, &t_array_fps[i], 0, NULL);
			if (t_h_thread != NULL)
				t_array_handle[i] = t_h_thread;

			Sleep(1000);
		}
		
		printf("Press any key to exit encode thread ...\n");
		t_n_ch = getchar();
		g_b_running = false;
		WaitForMultipleObjects(t_n_thread + 1, t_array_handle, TRUE, INFINITE);

	} while (t_n_ch != 'z');

	if (t_array_fps != NULL) {
		delete t_array_fps;
		t_array_fps = NULL;
	}

	if (t_array_handle != NULL) {
		delete t_array_handle;
		t_array_handle = NULL;
	}

end:
	printf("Press any key to exit ...\n");
	getchar();

	whh_venc_deinit();
	unload_file();

	return t_n_ret;
}

void printf_usage()
{
	printf("This program is used to test encoding by specified graphic adapters.\n");
}

whh_venc_gpu_info_s get_specified_card()
{
	whh_venc_gpu_info_s t_selected;
	t_selected.index = -1;

	int t_n_num = 0;
	t_n_num = whh_venc_get_amd_gpu_num();
	if (t_n_num <= 0) {
		printf("Find 0 graphic adapter.\n");
		return t_selected;
	}

	printf("Find %d graphic cards:\n", t_n_num);
	vector<whh_venc_gpu_info_s> t_vec_valid;
	for (int i = 0; i < t_n_num; i++) {
		whh_venc_gpu_info_s t_gpu_info;
		bool t_status = false;
		t_status = whh_venc_get_gpu_info_by_index(i, &t_gpu_info);
		if (t_status) {
			t_vec_valid.push_back(t_gpu_info);
			printf("%d. %s\n", i, t_gpu_info.gpu_name);
		}
		else
			printf("%d. invalid\n", i);
	}

	if (t_vec_valid.size() > 0) {
		bool t_b_selected = false;
		int t_n_sel = -1;
		while (!t_b_selected) {
			printf("Please select graphic card by index from \n");
			for (int i = 0; i < t_vec_valid.size(); i++)
				printf("%d ", t_vec_valid.at(i).index);
			printf("\n");

			scanf("%d", &t_n_sel);
#if(_MSC_VER<1900)
			fflush(stdin);
#else
			rewind(stdin);
#endif
			bool t_b_find = false;
			for (int i = 0; i < t_vec_valid.size(); i++) {
				if (t_vec_valid.at(i).index == t_n_sel) {
					t_b_find = true;
					break;
				}
			}

			if (t_b_find) {
				t_selected = t_vec_valid.at(t_n_sel);
				t_b_selected = true;
			}
			else
				printf("Please select a valid adapter.\n");
		}
	}
	else {
		printf("No adapter is valid.\n");
		return t_selected;
	}

	return t_selected;
}

whh_venc_amd_mem_type_e get_amd_mem_type()
{
	whh_venc_amd_mem_type_e t_amd_mem_type = WHH_VENC_AMD_MEM_DX;

	bool t_b_selected = false;
	int t_n_selected = -1;
	while (!t_b_selected) {
		printf("Select AMD memory type from bellow:\n 0. DX\n 1.CPU\nPlease input 0 or 1\n");
		scanf("%d", &t_n_selected);
#if(_MSC_VER<1900)
		fflush(stdin);
#else
		rewind(stdin);
#endif
		if (t_n_selected == 0) {
			t_amd_mem_type = WHH_VENC_AMD_MEM_DX;
			t_b_selected = true;
		}
		else if (t_n_selected == 1) {
			t_amd_mem_type = WHH_VENC_AMD_MEM_CPU;
			t_b_selected = true;
		}
	}

	return t_amd_mem_type;
}

whh_venc_code_type_e get_code_type()
{
	whh_venc_code_type_e t_code_type = WHH_VENC_CODE_TYPE_H264;

	bool t_b_selected = false;
	int t_n_selected = -1;
	while (!t_b_selected) {
		printf("Select code type from bellow:\n 0. H264\n 1. H265\nPlease input 0 or 1\n");
		scanf("%d", &t_n_selected);
#if(_MSC_VER<1900)
		fflush(stdin);
#else
		rewind(stdin);
#endif
		if (t_n_selected == 0) {
			t_code_type = WHH_VENC_CODE_TYPE_H264;
			t_b_selected = true;
		}
		else if (t_n_selected == 1) {
			t_code_type = WHH_VENC_CODE_TYPE_H265;
			t_b_selected = true;
		}
	}

	return t_code_type;
}

whh_venc_fourcc_e get_input_fourcc()
{
	whh_venc_fourcc_e t_fourcc = WHH_VENC_FOURCC_NV12;

	bool t_b_selected = false;
	int t_n_selected = -1;
	while (!t_b_selected) {
		printf("Select fourcc from bellow:\n 0. NV12\nPlease input 0\n");
		scanf("%d", &t_n_selected);
#if(_MSC_VER<1900)
		fflush(stdin);
#else
		rewind(stdin);
#endif
		if (t_n_selected == 0) {
			t_fourcc = WHH_VENC_FOURCC_NV12;
			t_b_selected = true;
		}
	}

	return t_fourcc;
}

int get_resolution()
{
	int t_n_resolution = -1;

	bool t_b_selected = false;
	int t_n_selected = -1;
	while (!t_b_selected) {
		printf("Select resolution from bellow:\n 0. 1280x720\n 1. 1920x1080\n 2. 3840x2160\nPlease input 0 , 1 or 2\n");
		scanf("%d", &t_n_selected);
#if(_MSC_VER<1900)
		fflush(stdin);
#else
		rewind(stdin);
#endif
		if (t_n_selected == 0) {
			t_n_resolution = 0;
			t_b_selected = true;
		}
		else if (t_n_selected == 1) {
			t_n_resolution = 1;
			t_b_selected = true;
		}
		else if (t_n_selected == 2) {
			t_n_resolution = 2;
			t_b_selected = true;
		}
	}

	return t_n_resolution;
}

bool load_file(
	whh_venc_fourcc_e t_fourcc,
	int t_n_resolution)
{
	bool t_b_loaded = false;

	FILE* t_p_file_in = NULL;
	int t_n_width = 0;
	int t_n_height = 0;
	int t_n_frame_size = 0;

	if (t_fourcc == WHH_VENC_FOURCC_NV12) {
		if (t_n_resolution == 0) {// 1280x720
			t_p_file_in = fopen("../../../test_data/nv12_720.yuv", "rb");
			t_n_width = 1280;
			t_n_height = 720;
		}
		else if (t_n_resolution == 1) {// 1920x1080
			t_p_file_in = fopen("../../../test_data/nv12_1080.yuv", "rb");
			t_n_width = 1920;
			t_n_height = 1080;
		}
		else if (t_n_resolution == 2) {// 3840x2160
			t_p_file_in = fopen("../../../test_data/nv12_2160.yuv", "rb");
			t_n_width = 3840;
			t_n_height = 2160;
		}
		t_n_frame_size = t_n_width*t_n_height * 3 / 2;
	}

	if (t_p_file_in != NULL) {
		g_pp_frames = new uint8_t*[g_n_frames];
		for (int i = 0; i < g_n_frames; i++) {
			g_pp_frames[i] = NULL;
			g_pp_frames[i] = new uint8_t[t_n_frame_size];
		}

		for (int i = 0; i < g_n_frames; i++) {
			if (g_pp_frames[i] != NULL)
				fread(g_pp_frames[i], 1, t_n_frame_size, t_p_file_in);
		}

		t_b_loaded = true;
		fclose(t_p_file_in);
	}

	return t_b_loaded;
}

void unload_file()
{
	if (g_pp_frames != NULL) {
		for (int i = 0; i < g_n_frames; i++) {
			if (g_pp_frames[i] != NULL) {
				delete g_pp_frames[i];
				g_pp_frames[i] = NULL;
			}
		}
		delete g_pp_frames;
	}
}

void test_encode(
	whh_venc_gpu_info_s t_gpu,
	whh_venc_param_s t_param,
	whh_thread_fps_s*	t_p_fps)
{


#ifdef _WIN32
	DWORD t_dw_thread_id = GetCurrentThreadId();
#endif
#ifdef __linux
	pthread_t t_pid = pthread_self();
#endif

	char t_cs_platform[16] = { 0 };

	if (t_param.amd_mem_reserved == WHH_VENC_AMD_MEM_DX)
		sprintf(t_cs_platform, "amd_dx");
	else if (t_param.amd_mem_reserved == WHH_VENC_AMD_MEM_VULKAN)
		sprintf(t_cs_platform, "amd_vulkan");
	else
		sprintf(t_cs_platform, "amd_cpu");


	char t_cs_code_type[16] = { 0 };
	if (t_param.code_type == WHH_VENC_CODE_TYPE_H264)
		sprintf(t_cs_code_type, "h264");
	else if (t_param.code_type == WHH_VENC_CODE_TYPE_H265)
		sprintf(t_cs_code_type, "h265");
	else
		return;

	uint8_t** t_pp_framein = NULL;
	char t_cs_fourcc[16] = { 0 };
	if (t_param.fourcc == WHH_VENC_FOURCC_NV12) {
		sprintf(t_cs_fourcc, "nv12");
	}
	else
		return;

	//base main high
	char t_cs_profile[16] = { 0 };
	if (t_param.profile == WHH_VENC_PROFILE_H264_BASELINE)
		sprintf(t_cs_profile, "base");
	else if (t_param.profile == WHH_VENC_PROFILE_H264_MAIN)
		sprintf(t_cs_profile, "main");
	else if (t_param.profile == WHH_VENC_PROFILE_H264_HIGH)
		sprintf(t_cs_profile, "high");
	else if (t_param.profile == WHH_VENC_PROFILE_H265_MAIN)
		sprintf(t_cs_profile, "main");
	else
		return;

	// balanced  best speed  best quality
	char t_cs_usage[16] = { 0 };
	if (t_param.targetusage == WHH_VENC_TARGETUSAGE_BALANCED)
		sprintf(t_cs_usage, "balanced");
	else if (t_param.targetusage == WHH_VENC_TARGETUSAGE_BEST_QUALITY)
		sprintf(t_cs_usage, "best_quality");
	else if (t_param.targetusage == WHH_VENC_TARGETUSAGE_BEST_SPEED)
		sprintf(t_cs_usage, "best_speed");
	else
		return;

	// cqp cbr vbr
	char t_cs_rate_control[16] = { 0 };
	uint32_t t_u32_rate = 0;
	if (t_param.rate_control.mode == WHH_VENC_RATECONTROL_CBR) {
		sprintf(t_cs_rate_control, "cbr");
		t_u32_rate = t_param.rate_control.target_bitrate;
	}
	else
		return;

#ifdef _WIN32
	printf("thread %ld : %s %s %s %s %s %d %dx%d\n",
		t_dw_thread_id,
		t_cs_code_type,
		t_cs_fourcc,
		t_cs_profile,
		t_cs_usage,
		t_cs_rate_control,
		t_u32_rate,
		t_param.width,
		t_param.height);
#endif
#ifdef __linux
	printf("thread %ld : %s %s %s %s %s %d %dx%d\n",
		t_pid,
		t_cs_code_type,
		t_cs_fourcc,
		t_cs_profile,
		t_cs_usage,
		t_cs_rate_control,
		t_u32_rate,
		t_param.width,
		t_param.height);
#endif

	CWHHAMDEncoder *t_h_handle = new CWHHAMDEncoder();
	if (t_h_handle == NULL)
		return;

	bool t_b_ret = t_h_handle->init();
	if (!t_b_ret) {
		delete t_h_handle;
		return;
	}

	t_b_ret = t_h_handle->create_encoder(&t_param, data_callback, t_p_fps, t_gpu.device_id);


	int t_n_frame_index = 0;
	while (g_b_running) {
		clock_t t_begin = clock();
		t_h_handle->put_frame(g_pp_frames[t_n_frame_index]);
		t_n_frame_index++;
		t_n_frame_index = t_n_frame_index%g_n_frames;
		clock_t t_end = clock();

		/*
		if (t_gpu.platform == MW_VENC_PLATFORM_AMD) {
		int t_n_ms = (t_end - t_begin) * 1000 / CLOCKS_PER_SEC;
		int t_n_wait = 16 - t_n_ms;
		if(t_n_wait>0)
		Sleep(t_n_wait);
		}
		*/
	}


	if (t_h_handle != NULL) {
		t_h_handle->destory_encoder();
		t_h_handle->deinit();
		delete t_h_handle;
		t_h_handle = NULL;
	}

	printf("encode thread exited\n");
}

void data_callback(
	void* t_p_param,
	const uint8_t* t_p_frame,
	uint32_t t_u32_frame_len
)
{
	// calac fps

	if (t_p_param != NULL) {
		whh_thread_fps_s* t_p_fps = (whh_thread_fps_s*)t_p_param;

		t_p_fps->m_n_encode++;
		clock_t t_time = clock();
		if (t_p_fps->m_time == 0)
			t_p_fps->m_time = t_time;
		if (t_time - t_p_fps->m_time >= CLOCKS_PER_SEC) {
			float t_f_fps = t_p_fps->m_n_encode*1.0 / ((t_time - t_p_fps->m_time)*1.0 / CLOCKS_PER_SEC);
			t_p_fps->m_time = t_time;
			t_p_fps->m_n_encode = 0;
			t_p_fps->m_f_fps = t_f_fps;
		}
	}

}



#ifdef _WIN32

DWORD WINAPI thread_test_encode(LPVOID lpThreadParameter)
{
	DWORD t_dw_ret = 0;

	g_b_running = true;
	test_encode(g_gpu, g_param, (whh_thread_fps_s*)lpThreadParameter);

	return t_dw_ret;
}

DWORD WINAPI thread_print_fps(LPVOID lpThreadParameter)
{
	g_b_running = true;
	while (g_b_running) {
		Sleep(1000);
		for (int i = 0; i < g_n_thread; i++) {
			printf("fps:%.2f ", g_array_fps[i].m_f_fps);
		}
		printf("\r");
	}

	return  0;
}

#endif

int get_thread_nums()
{
	int t_n_thread = 1;

	bool t_b_selected = false;
	int t_n_selected = 1;
	while (!t_b_selected) {
		printf("Please input threads num you want create 1-8:\n");
		scanf("%d", &t_n_selected);
#if(_MSC_VER<1900)
		fflush(stdin);
#else
		rewind(stdin);
#endif
		if (t_n_selected>0 && t_n_selected <= 8) {
			t_n_thread = t_n_selected;
			t_b_selected = true;
		}
	}

	return t_n_thread;
}