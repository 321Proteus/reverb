#include "vst2.hpp"
#include "common/analysis.hpp"
#include "util.hpp"
#include <auto.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <name.hpp>
#include <pro.h>
#include <search.hpp>
#include <bytes.hpp>
#include <segment.hpp>
#include <ua.hpp>

namespace rv::vst2 {

ea_t find_entry() {
	static const char* const kEntryNames[] = { "VSTPluginMain",
		"_VSTPluginMain", "main" };

	for (const char* name : kEntryNames) {
		const ea_t ea = get_name_ea(BADADDR, name);
		if (ea != BADADDR) return ea;
	}
	return BADADDR;
}

void label_entry() {
	const ea_t entry_ea = find_entry();
	if (entry_ea == BADADDR) {
		rv_log("No valid VST2 entry point found");
		return;
	}

	func_t* pfn = rv::ensure_func(entry_ea);
	if (pfn == nullptr) {
		rv_log("Couldn't create function at VST2 entry");
		return;
	}

	if (!rv::apply_definite_proto(pfn->start_ea,
			"AEffect *__cdecl vst2_entry(audioMasterCallback audioMaster);")) {
		rv_log("Couldn't apply the prototype on VST2 entry");
		return;
	}

	mark_cfunc_dirty(pfn->start_ea);
	rv_log("Typed VST2 entry point at %a", pfn->start_ea);
}
} // namespace rv::vst2
