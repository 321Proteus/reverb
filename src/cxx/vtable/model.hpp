#pragma once

#include "cxx/rtti/abi.hpp"
#include <map>
#include <typeinf.hpp>
#include <vector>

/**
 * @file model.hpp
 * @brief What the vtables of a binary say about its classes and their slot
 * functions.
 *
 * Collect first, decide after; nothing here writes to the database. No
 * Hex-Rays.
 */
namespace rv::vtbl {

/**
 * @brief One vtable
 */
struct table_rec {
	ea_t ea = BADADDR;	  /**< Address point */
	size_t extent = 0;	  /**< Number of slots */
	rtti::class_info cls; /**< From RTTI, empty without */
	qstring
		iface; /**< Chosen generated interface, e.g. `IPlugView` ("" if none) */
	tinfo_t iface_vtbl; /**< `<iface>_vtbl` */
};

/**
 * @brief One vtable slot and the function it holds
 */
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

/**
 * @brief How a slot function relates to the classes that hold it
 */
enum class func_kind { OWN, INHERITED, FOLDED, PURECALL, THUNK, SKIP };

/**
 * @brief Verdict on one slot function
 */
struct func_rec {
	func_kind kind = func_kind::SKIP;
	int refs = 0;		 /**< Slot refs */
	int rec = -1;		 /**< Slot record the method and prototype come from */
	qstring owner;		 /**< Class the function is named after */
	sval_t this_off = 0; /**< Offset of this in owner */
	qstring name; /**< "owner::method", suffixed for thunks and aliases */
	ea_t thunk_target = BADADDR; /**< The thunk target */
};

/**
 * @brief Recognizes an adjustor thunk: `this += imm; jmp target`
 *
 * Allows endbr and a `push rbp; mov rbp, rsp ... pop rbp` frame around the
 * adjustment.
 *
 * @param ea     Thunk address
 * @param target Receives the jump target
 * @param delta  Receives the adjustment, may be null
 */
bool adjustor_thunk(ea_t ea, ea_t* target, sval_t* delta = nullptr);

/**
 * @brief Collected vtables, slots and slot function verdicts
 */
struct model {
	std::vector<table_rec> vtables;
	std::vector<slot_rec> slots;
	std::map<qstring, rtti::class_info> classes; // by name
	std::map<ea_t, func_rec> funcs;

	/**
	 * @brief Adds a vtable at its address point
	 *
	 * @p vtbl_type forces the interface, otherwise the largest generated base
	 * interface that fits is used, or the first fitting of @p fallback
	 * (smallest last).
	 *
	 * @return false if not a vtable
	 */
	bool add(ea_t vt, const char* vtbl_type = nullptr,
		const std::vector<const char*>& fallback = {});

	/**
	 * @brief Adds the vtables named by RTTI (`??_7` and `_ZTV` symbols)
	 *
	 * @return How many were added
	 */
	size_t add_rtti_vtables();

	/**
	 * @brief Builds slot records and classifies functions. Call after all add()
	 */
	void collect();

	/**
	 * @brief Whether @p base is a base class of @p derived per RTTI
	 */
	bool is_base_of(const qstring& base, const qstring& derived) const;

private:
	std::map<ea_t, size_t> index_;
	void classify(ea_t f, const std::vector<int>& recs);
	bool resolve_owner(const std::vector<int>& recs, func_rec& fr) const;
	void assign_names();
};

} // namespace rv::vtbl
