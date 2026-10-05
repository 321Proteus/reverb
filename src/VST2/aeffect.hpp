#include <hexrays.hpp>
#include <pro.h>
#include <vector>

namespace rv::vst2 {

// A location consisting of a lvar index and offset
struct loc_t {
	int var = -1;
	int64 off = 0;
};

// What was stored in a memory location
struct val_t {
	enum val_type_t { UNKNOWN, NUM, ADDR, LOCAL } t = UNKNOWN;
	uint64 num = 0;
	ea_t ea = BADADDR;
	loc_t loc;
	uchar raw[16] = {};
	int raw_len = 0;
};

// A resolved AEffect field
struct field_val_t {
	val_t v;
	ea_t src_ea = BADADDR;
};

struct store_t {
	loc_t loc;
	int size = 0;
	val_t val;
	bool is_or = false;
	ea_t ea = BADADDR;
	qstring text;
};

struct aeffect_hit_t {
	ea_t func = BADADDR;
	ea_t magic_ea = BADADDR;
	int64 base_off = 0;
	int root = -1;
	std::map<qstring, field_val_t> fields;
	uint32 flags = 0;
};

enum fkind_t { F_INT, F_FN, F_PTR };

struct aeffect_field_t {
	const char* name;
	int off; // bytes from AEffect::magic
	int sz;	 // bytes
	fkind_t kind;
};

static const char* const members[] = {
	"dispatcher",
	"process",
	"setParameter",
	"getParameter",
	"numPrograms",
	"numParams",
	"numInputs",
	"numOutputs",
	"flags",
	"initialDelay",
	"ioRatio",
	"object",
	"uniqueID",
	"version",
	"processReplacing",
	"processDoubleReplacing",
};

std::vector<aeffect_hit_t> extract_aeffects(
	const std::vector<aeffect_field_t>& fields, int aeffect_size, cfunc_t* cf);

void apply_aeffect(const std::vector<aeffect_field_t>& fields,
	const aeffect_hit_t& h, bool multiple);

} // namespace rv::vst2