#include "whh_amf_factory_helper.h"
#include "whh_amf_thread.h"

CWHHAMFFactoryHelper g_amf_factory;

#ifdef AMF_CORE_STATIC
extern "C"
{
	extern AMF_CORE_LINK AMF_RESULT AMF_CDECL_CALL AMFInit(amf_uint64 version, amf::AMFFactory **ppFactory);
}
#endif

CWHHAMFFactoryHelper::CWHHAMFFactoryHelper():
m_h_dll(NULL),
	m_p_factory(NULL),
	m_p_debug(NULL),
	m_p_trace(NULL),
	m_u64_amf_rt_version(0),
	m_l_ref(0)
{

}

CWHHAMFFactoryHelper::~CWHHAMFFactoryHelper()
{
	terminate();
}

AMF_RESULT CWHHAMFFactoryHelper::init()
{
#ifndef AMF_CORE_STATIC
	if (m_h_dll != NULL) {
		amf_atomic_inc(&m_l_ref);
		return AMF_OK;
	}
	m_h_dll = amf_load_library(AMF_DLL_NAME);
	if (m_h_dll == NULL) 
		return AMF_FAIL;

	AMFInit_Fn init_fun = (AMFInit_Fn)::amf_get_proc_address(m_h_dll, AMF_INIT_FUNCTION_NAME);
	if (init_fun == NULL)
		return AMF_FAIL;

	AMF_RESULT t_res = init_fun(AMF_FULL_VERSION, &m_p_factory);
	if (t_res != AMF_OK)
		return t_res;

	AMFQueryVersion_Fn version_fun = (AMFQueryVersion_Fn)::amf_get_proc_address(m_h_dll, AMF_QUERY_VERSION_FUNCTION_NAME);
	if (version_fun == NULL)
		return AMF_FAIL;

	t_res = version_fun(&m_u64_amf_rt_version);
	if (t_res != AMF_OK)
		return t_res;
#else
	AMF_RESULT t_res = AMFInit(AMF_FULL_VERSION, &m_p_factory);
	if (t_res != AMF_OK)
		return t_res;
	m_u64_amf_rt_version = AMF_FULL_VERSION;
#endif
	m_p_factory->GetTrace(&m_p_trace);
	m_p_factory->GetDebug(&m_p_debug);

	amf_atomic_inc(&m_l_ref);

	return AMF_OK;
}

AMF_RESULT CWHHAMFFactoryHelper::terminate()
{
	if (m_h_dll != NULL) {
		amf_atomic_dec(&m_l_ref);
		if (m_l_ref == 0) {
			amf_free_library(m_h_dll);
			m_h_dll = NULL;
			m_p_factory = NULL;
			m_p_debug = NULL;
			m_p_trace = NULL;
		}
	}

	return AMF_OK;
}

AMF_RESULT CWHHAMFFactoryHelper::load_external_component(amf::AMFContext* p_context, const wchar_t* dll, const char* function, void* reserved, amf::AMFComponent** pp_component)
{
	// check passed in parameters
	if (!p_context || !dll || !function)
		return AMF_INVALID_ARG;

	// check if DLL has already been loaded
	amf_handle t_h_dll = NULL;
	for (std::vector<component_holder>::iterator it = m_vec_components.begin(); it != m_vec_components.end(); ++it) {
#if defined(_WIN32)
		if(wcsicmp(it->m_ws_dll.c_str(),dll)==0)
#elif defined(__linux)
		if (wcscmp(it->m_ws_dll.c_str(), dll) == 0)
#endif
		{
			if (it->m_h_dll != NULL) {
				t_h_dll = it->m_h_dll;
				amf_atomic_inc(&it->m_l_ref);
				break;
			}

			return AMF_UNEXPECTED;
		}
	}

	// dll wasn't loaded before so load it now and
	// add it to the internal list
	if (t_h_dll == NULL) {
		component_holder t_component;
		t_component.m_l_ref = 0;
		t_component.m_h_dll = NULL;
		t_component.m_ws_dll = dll;

		t_h_dll = amf_load_library(dll);
		if (t_h_dll == NULL)
			return AMF_FAIL;

		// since LoadLibrary succeeded add the information
		// into the internal list so we can properly free
		// the DLL later on, even if we fail to get the required
		// information from it...
		t_component.m_h_dll = t_h_dll;
		amf_atomic_inc(&t_component.m_l_ref);
		m_vec_components.push_back(t_component);
	}

	// look for function we want in the dll we just loaded
	typedef AMF_RESULT(AMF_CDECL_CALL* AMFCreateComponentFunc)(amf::AMFContext*, void* reserved, amf::AMFComponent**);
	AMFCreateComponentFunc t_init_fun = (AMFCreateComponentFunc)::amf_get_proc_address(t_h_dll,function);
	if (t_init_fun == NULL)
		return AMF_FAIL;

	return t_init_fun(p_context, reserved, pp_component);
}

AMF_RESULT CWHHAMFFactoryHelper::unload_external_component(const wchar_t* dll)
{
	if (!dll)
		return AMF_INVALID_ARG;

	for (std::vector<component_holder>::iterator it = m_vec_components.begin(); it != m_vec_components.end(); ++it) {
#if defined(_WIN32)
		if(wcsicmp(it->m_ws_dll.c_str(),dll)==0)
#elif defined(__linux)
		if (wcscmp(it->m_ws_dll.c_str(), dll) == 0)
#endif
		{
			if (it->m_h_dll == NULL)
				return AMF_UNEXPECTED;

			amf_atomic_dec(&it->m_l_ref);
			if (it->m_l_ref == 0) {
				amf_free_library(it->m_h_dll);
				m_vec_components.erase(it);
			}
			break;
		}
	}
	return AMF_OK;
}

amf::AMFFactory* CWHHAMFFactoryHelper::get_factory()
{
	return m_p_factory;
}

amf::AMFDebug* CWHHAMFFactoryHelper::get_debug()
{
	return m_p_debug;
}

amf::AMFTrace* CWHHAMFFactoryHelper::get_trace()
{
	return m_p_trace;
}

amf_uint64 CWHHAMFFactoryHelper::querry_amf_version()
{
	return m_u64_amf_rt_version;
}
