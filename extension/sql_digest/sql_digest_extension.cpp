#include "sql_digest_extension.hpp"

#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "sql_digest_functions.hpp"

namespace duckdb {

namespace {

//! Registers FUN::GetFunction() with the documentation FUN declares
template <class FUN>
void RegisterDigestFunction(ExtensionLoader &loader) {
	CreateScalarFunctionInfo info(FUN::GetFunction());
	FunctionDescription description;
	description.parameter_names.emplace_back(FUN::Parameter);
	description.parameter_types.emplace_back(LogicalType::VARCHAR);
	description.description = FUN::Description;
	description.examples.emplace_back(FUN::Example);
	description.categories.emplace_back(FUN::Category);
	info.descriptions.push_back(std::move(description));
	loader.RegisterFunction(std::move(info));
}

} // namespace

static void LoadInternal(ExtensionLoader &loader) {
	RegisterDigestFunction<SQLDigestFun>(loader);
	RegisterDigestFunction<SQLDigestTextFun>(loader);
}

void SqlDigestExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string SqlDigestExtension::Name() {
	return "sql_digest";
}

std::string SqlDigestExtension::Version() const {
#ifdef EXT_VERSION_SQL_DIGEST
	return EXT_VERSION_SQL_DIGEST;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(sql_digest, loader) {
	duckdb::LoadInternal(loader);
}
}
