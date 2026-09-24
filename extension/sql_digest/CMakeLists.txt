cmake_minimum_required(VERSION 3.5...3.29)

project(SqlDigestExtension)

include_directories(include)

set(SQL_DIGEST_EXTENSION_FILES
    sql_digest_extension.cpp function/sql_digest.cpp
    parser/sql_digest_normalizer.cpp crypto/sha256_generic.cpp
    crypto/sha256_ni.cpp)

# Appends the private scanner's objects to SQL_DIGEST_EXTENSION_FILES.
add_subdirectory(third_party/libpg_query_digest)

build_static_extension(sql_digest ${SQL_DIGEST_EXTENSION_FILES})
set(PARAMETERS "-warnings")
build_loadable_extension(sql_digest ${PARAMETERS} ${SQL_DIGEST_EXTENSION_FILES})

install(
  TARGETS sql_digest_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
