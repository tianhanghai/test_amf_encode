#ifndef WHH_AMF_FACTORY_HELPER_H
#define WHH_AMF_FACTORY_HELPER_H

#include <string>
#include <vector>

#include "core/Factory.h"

class CWHHAMFFactoryHelper {
public:
	CWHHAMFFactoryHelper();
	~CWHHAMFFactoryHelper();

public:
	AMF_RESULT init();
	AMF_RESULT terminate();

	AMF_RESULT load_external_component(
		amf::AMFContext* p_context,
		const wchar_t* dll,
		const char* function,
		void* reserved,
		amf::AMFComponent** pp_component);

	AMF_RESULT unload_external_component(const wchar_t* dll);

	amf::AMFFactory* get_factory();
	amf::AMFDebug* get_debug();
	amf::AMFTrace* get_trace();

	amf_uint64 querry_amf_version();

protected:
	struct component_holder
	{
		amf_handle		m_h_dll;
		amf_long		m_l_ref;
		std::wstring	m_ws_dll;

		component_holder() {
			m_h_dll = NULL;
			m_l_ref = 0;
		}
	};

	amf_handle			m_h_dll;
	amf::AMFFactory*	m_p_factory;
	amf::AMFDebug*		m_p_debug;
	amf::AMFTrace*		m_p_trace;
	amf_uint64			m_u64_amf_rt_version;

	amf_long			m_l_ref;

	std::vector<component_holder>	m_vec_components;
};

extern ::CWHHAMFFactoryHelper g_amf_factory;

#endif