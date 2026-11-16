#include "til.hpp"
#include "core/log.hpp"
#include "db/til/pick.hpp"
#include <diskio.hpp>
#include <dirtree.hpp>
#include <ida.hpp>
#include <loader.hpp>
#include <set>
#include <string>
#include <typeinf.hpp>
#include <vector>

namespace rv::til {

// The ABI the type libraries are built for, nullptr if there are none
static const char* get_til_abi() {
	if (inf_get_filetype() != f_PE) return nullptr;
	return inf_is_64bit() ? "msvc64" : "msvc32";
}

struct scanner_t : file_enumerator_t {
	std::string name;
	std::string abi;
	bool versioned = false;
	std::vector<TilEntry> found;

	int visit_file(const char* file) override {
		const char* base = qbasename(file);
		TilEntry e;
		const bool ok = versioned
							? parse_file_name(base, name, abi, &e)
							: parse_unversioned_file_name(base, name, abi, &e);
		if (ok) {
			e.file = file;
			found.push_back(e);
		}
		return 0;
	}
};

/*
	The tils are looked up where IDA itself looks for them ($IDAUSR/til/<proc>,
	then $IDADIR/til/<proc>); that search is not recursive
*/
static std::vector<TilEntry> search_tils(
	const Request& req, const char* abi, qstring* searched) {
	qstrvec_t dirs;

	// get_ida_subdirs(
	// 	&dirs, TIL_SUBDIR, IDA_SUBDIR_IDP | IDA_SUBDIR_ONLY_EXISTING);

	qstring user_dir = get_user_idadir();
	user_dir += "/til";
	dirs.add(user_dir);

	scanner_t sc;
	sc.name = req.name;
	sc.abi = abi;
	sc.versioned = req.version != nullptr;

	qstring pattern;
	if (sc.versioned) pattern.sprnt("rv_%s_v*_%s.til", req.name, abi);
	else
		pattern.sprnt("rv_%s_%s.til", req.name, abi);

	char answer[QMAXPATH];
	for (const qstring& d : dirs) {
		if (!searched->empty()) *searched += ", ";
		*searched += d;
		enumerate_files(answer, sizeof(answer), d.c_str(), pattern.c_str(), sc);
	}

	return std::move(sc.found);
}

static size_t group_types(uint32 first, uint32 last, const char* folder) {

	dirtree_t* tree = get_std_dirtree(DIRTREE_LOCAL_TYPES);
	if (tree == nullptr || first >= last) return 0;

	qstring path;
	path.sprnt("/%s", folder);
	tree->mkdir(path.c_str()); // may exist from an earlier import

	tree->chdir("/");
	for (uint32 o = first; o < last; o++)
		tree->unlink(inode_t(o));

	size_t moved = 0;
	if (tree->chdir(path.c_str()) != DTE_OK) return 0;
	for (uint32 o = first; o < last; o++)
		if (tree->link(inode_t(o)) == DTE_OK) moved++;

	tree->chdir("/");
	return moved;
}

static void import_types(til_t* src, const char* label, const char* folder) {
	til_t* local = get_idati();

	qstrvec_t names;
	for (const char* n = first_named_type(src, NTF_TYPE); n != nullptr;
		n = next_named_type(src, n, NTF_TYPE))
		names.push_back(n);

	const uint64 t0 = get_nsec_stamp();
	const uint32 before = get_ordinal_count(local);
	const uint32 first = get_ordinal_limit(local);
	size_t failed = 0;

	// A name that is local when its turn comes was either there before or
	// came in as the dependency of an earlier type: left alone either way
	for (const qstring& n : names) {
		if (get_named_type(local, n.c_str(), NTF_TYPE | NTF_NOBASE) != 0)
			continue;
		if (copy_named_type(local, src, n.c_str()) == 0) failed++;
	}

	// copy_named_type gives its types consecutive new ordinals
	const size_t grouped = group_types(first, get_ordinal_limit(local), folder);

	rv_log("%s types: %u imported into local types in %u ms (%u in the type "
		   "library), %u failed; %u in the folder \"%s\"",
		label, unsigned(get_ordinal_count(local) - before),
		unsigned((get_nsec_stamp() - t0) / 1000000), unsigned(names.size()),
		unsigned(failed), unsigned(grouped), folder);
}

bool load(const Request& req) {
	const char* abi = get_til_abi();
	if (abi == nullptr) {
		rv_log(
			"%s types: no type libraries for this kind of binary", req.label);
		return false;
	}

	Version v;
	if (req.version != nullptr &&
		(*req.version == '\0' || !parse_version(req.version, &v))) {
		rv_log(
			"%s types: no version detected, no type library loaded", req.label);
		return false;
	}

	qstring searched;
	std::vector<TilEntry> tils = search_tils(req, abi, &searched);

	if (tils.empty()) {
		static std::set<std::string> reported;

		if (reported.insert(req.name).second) {
			rv_log("%s type libraries (rv_%s%s_%s.til) are not installed. "
				   "Download the type library archive from "
				   "the releases page and extract it to %s, no restart is "
				   "needed",
				req.label, req.name, req.version != nullptr ? "_v*" : "", abi,
				searched.c_str(), get_user_idadir());
		}

		return false;
	}

	const TilEntry* chosen = &tils[0];

	if (req.version != nullptr) {

		const Pick p = pick(tils, v, !req.vst3);

		if (p.entry == nullptr) {
			rv_log("%s types: %s is older than the oldest type library, none "
				   "loaded",
				req.label, req.version);
			return false;
		}

		if (p.partial_skipped)
			rv_log("%s types: the type library for %s holds only VST2 types "
				   "(its VST3 wrapper crashes IDA's parser); using %s",
				req.label, req.version, p.entry->name.c_str());

		if (p.too_new)
			rv_log("%s types: warning: %s is newer than every type library; "
				   "using the newest (%s)",
				req.label, req.version, p.entry->name.c_str());

		chosen = p.entry;
	}

	til_t* local = get_idati();
	til_t* t = local->find_base(chosen->name.c_str());

	if (t == nullptr) {

		const int r =
			add_til(chosen->file.c_str(), ADDTIL_INCOMP | ADDTIL_SILENT);

		if (r == ADDTIL_FAILED || r == ADDTIL_ABORTED) {
			rv_log(
				"%s types: could not add %s", req.label, chosen->file.c_str());
			return false;
		}

		if (r == ADDTIL_COMP) {
			rv_log(
				"%s types: %s is not for the compiler used in this IDB; using "
				"it anyway",
				req.label, chosen->name.c_str());
		}

		t = local->find_base(chosen->name.c_str());
	}

	if (t == nullptr) {
		rv_log("%s types: %s was added but is not among the type libraries",
			req.label, chosen->name.c_str());
		return false;
	}

	rv_log("%s types: using %s", req.label, chosen->file.c_str());
	if (req.import_types) {
		// "JUCE 6.1.2", "JUCE 7.0.13-security"
		qstring folder = req.label;
		if (req.version != nullptr)
			folder.cat_sprnt(" %d.%d.%d%s%s", chosen->v.major, chosen->v.minor,
				chosen->v.patch, chosen->suffix.empty() ? "" : "-",
				chosen->suffix.c_str());
		import_types(t, req.label, folder.c_str());
	}

	return true;
}

} // namespace rv::til
