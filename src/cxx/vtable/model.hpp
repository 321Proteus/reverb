#pragma once

#include "cxx/rtti/abi.hpp"
#include <map>
#include <typeinf.hpp>
#include <vector>

/*
	What the vtables of a binary say about its classes and their slot
	functions. Collect first, decide after; nothing here writes to the
	database. No Hex-Rays.
*/
namespace rv::vtbl {

struct table_rec {
	ea_t ea = BADADDR;
	size_t extent = 0;
	rtti::class_info cls; // from RTTI, empty without
	qstring iface; // chosen generated interface, e.g. "IPlugView" ("" if none)
	tinfo_t iface_vtbl; // "<iface>_vtbl"
};

struct slot_rec {
	ea_t func = BADADDR;
	ea_t vt = BADADDR;
	size_t index = 0;
	qstring cls; // owning class, the interface name without RTTI
	qstring iface;
	qstring method;
	sval_t sub_offset = 0;	// of this in the class, after the thunk adjustment
	tinfo_t type;			// the _vtbl member, a function pointer
	bool via_thunk = false; // reached through an adjustor thunk in the slot
};

enum class func_kind { OWN, INHERITED, FOLDED, PURECALL, THUNK, SKIP };

struct func_rec {
	func_kind kind = func_kind::SKIP;
	// slot refs
	int refs = 0;

	// slot record the method and prototype come from
	int rec = -1;

	// class the function is named after
	qstring owner;

	// of this in owner
	sval_t this_off = 0;

	// "owner::method", suffixed for thunks and aliases
	qstring name;

	// the thunk target
	ea_t thunk_target = BADADDR;
};

/*
	Adjustor thunk: this += imm; jmp target. Allows endbr and a
	push rbp; mov rbp, rsp ... pop rbp frame around the adjustment
*/
bool adjustor_thunk(ea_t ea, ea_t* target, sval_t* delta = nullptr);

struct model {
	std::vector<table_rec> vtables;
	std::vector<slot_rec> slots;
	std::map<qstring, rtti::class_info> classes; // by name
	std::map<ea_t, func_rec> funcs;

	/*
		Adds a vtable at its address point. vtbl_type forces the interface,
		otherwise the largest generated base interface that fits is used, or
		the first fitting of fallback (smallest last). False if not a vtable.
	*/
	bool add(ea_t vt, const char* vtbl_type = nullptr,
		const std::vector<const char*>& fallback = {});

	// Vtables named by RTTI (??_7 and _ZTV symbols); returns how many
	size_t add_rtti_vtables();

	// Slot records and function classification, after all add calls
	void collect();

	// True if base is a base class of derived per RTTI
	bool is_base_of(const qstring& base, const qstring& derived) const;

private:
	std::map<ea_t, size_t> index_;
	void classify(ea_t f, const std::vector<int>& recs);
	bool resolve_owner(const std::vector<int>& recs, func_rec& fr) const;
	void assign_names();
};

} // namespace rv::vtbl
