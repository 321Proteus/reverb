#pragma once
#include <pro.h>
#include <funcs.hpp>

#if IDA_SDK_VERSION >= 940

typedef function_item_iterator_t rv_fii_t;

inline ea_t rv_func_start(int i) { return get_func_ea_by_num(i); }
inline bool rv_fii_set(rv_fii_t& it, ea_t ea) { return it.set(ea); }

#else

typedef func_item_iterator_t rv_fii_t;

inline ea_t rv_func_start(int i) {
	func_t* f = getn_func(i);
	return f ? f->start_ea : BADADDR;
}

inline bool rv_fii_set(rv_fii_t& it, ea_t ea) {
	func_t* f = get_func(ea);
	return f && it.set(f);
}

#endif