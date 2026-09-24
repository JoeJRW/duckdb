# The digest's private copy of the libpg_query scanner, in namespace
# duckdb_sql_digest_pgquery.
#
# An OBJECT library that does not link duckdb_static: that would put core
# libpg_query's include directory, whose headers share names with the ones here,
# on the include path. The objects are added to SQL_DIGEST_EXTENSION_FILES and
# so end up in libsql_digest_extension.a, the archive the MySQL build collects.

add_library(sql_digest_pg_query OBJECT scan.cpp keywords.cpp scansup.cpp
                                       pg_functions.cpp parser_digest.cpp)

# The generator of kwlist_index.hpp. The header is committed rather than
# generated during the build, which a cross build could not run; this target is
# excluded from ALL, see generate_keyword_index.cpp for regenerating the header.
add_executable(sql_digest_keyword_index EXCLUDE_FROM_ALL
               generate_keyword_index.cpp)
target_include_directories(sql_digest_keyword_index
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
disable_target_warnings(sql_digest_keyword_index)

target_include_directories(sql_digest_pg_query
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Hidden visibility, as core libpg_query is built: with -fPIC and default
# visibility, GCC must assume these functions can be interposed and does not
# inline them. It also keeps them out of a loadable build's exported symbols.
set_target_properties(
  sql_digest_pg_query PROPERTIES POSITION_INDEPENDENT_CODE ON
                                 CXX_VISIBILITY_PRESET hidden)
disable_target_warnings(sql_digest_pg_query)

set(SQL_DIGEST_EXTENSION_FILES
    ${SQL_DIGEST_EXTENSION_FILES} $<TARGET_OBJECTS:sql_digest_pg_query>
    PARENT_SCOPE)
