#pragma once
/**
 * @file til.hpp
 * @brief Loading of the bundled type libraries into the database.
 */
#include <pro.h>

namespace rv::til {

/**
 * @brief Which type library to load
 */
struct Request {
	const char* label;			   /**< Name used in log messages */
	const char* name;			   /**< The files: `rv_<name>[_v<version>]_<abi>.til` */
	const char* version = nullptr; /**< nullptr if unversioned */
	bool import_types = true;	   /**< Also copy every type into the local types */
};

/**
 * @brief Adds the library to the database's type libraries and, if asked,
 * copies its types into the local types
 *
 * @return true if a library was added
 */
bool load(const Request& req);

/**
 * @brief Moves the local types with ordinals in [@p first, @p last) into a
 * folder of the Local Types view
 *
 * @return How many were moved
 */
size_t group_local_types(uint32 first, uint32 last, const char* folder);

} // namespace rv::til
