# Regenerates third_party/libpg_query_digest/scan.cpp from scan.l, in namespace
# duckdb_sql_digest_pgquery. scan.cpp is committed; the build does not run this.
#
# Run from the DuckDB root, so that the #line paths in scan.cpp stay relative:
#     python3 extension/sql_digest/scripts/generate_flex.py
#
# scan.cpp is generated with flex 2.6.4; another version rewrites most of it.
import os
import re
import subprocess
import sys
import tempfile
from sys import platform

flex_bin = 'flex'
extension_path = os.path.join('extension', 'sql_digest')
namespace = 'duckdb_sql_digest_pgquery'

for arg in sys.argv[1:]:
    if arg.startswith("--flex="):
        flex_bin = arg.replace("--flex=", "")
    else:
        raise Exception("Unrecognized argument: " + arg + ", expected --flex")


def open_utf8(fpath, flags, **kwargs):
    if sys.version_info[0] < 3:
        return open(fpath, flags, **kwargs)
    return open(fpath, flags, encoding="utf8", **kwargs)


pg_path = os.path.join(extension_path, 'third_party', 'libpg_query_digest')
flex_file_path = os.path.join(pg_path, 'scan.l')
target_file = os.path.join(pg_path, 'scan.cpp')

if not os.path.exists(flex_file_path):
    print("Run this from the DuckDB root: %s not found" % flex_file_path)
    exit(1)

# flex writes to a scratch file; scan.cpp is only replaced once every check
# below has passed.
scratch_fd, scratch_file = tempfile.mkstemp(suffix='.cpp')
os.close(scratch_fd)
try:
    proc = subprocess.Popen(
        [flex_bin, '--nounistd', '-o', scratch_file, flex_file_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    stdout, stderr = proc.communicate()
    stdout = stdout.decode('utf8')
    stderr = stderr.decode('utf8')
    if proc.returncode != 0 or len(stderr) > 0:
        print("Flex failed")
        print("stdout: ", stdout)
        print("stderr: ", stderr)
        exit(1)

    with open_utf8(scratch_file, 'r') as f:
        text = f.read()
finally:
    os.remove(scratch_file)

# the #line directives name flex's output file; name scan.cpp instead
text = text.replace('"%s"' % scratch_file, '"%s"' % target_file)

# convert this from 'int' to 'yy_size_t' to avoid triggering a warning
text = text.replace('int yy_buf_size;\n', 'yy_size_t yy_buf_size;\n')

# add the namespace
text = text.replace(
    '''
#ifndef FLEXINT_H
#define FLEXINT_H
''',
    '''
#ifndef FLEXINT_H
#define FLEXINT_H
namespace '''
    + namespace
    + ''' {
''',
)
text = text.replace('register ', '')

text = text + "\n} /* " + namespace + " */\n"

text = re.sub('(?:[(]void[)][ ]*)?fprintf', '//', text)
text = re.sub('exit[(]', 'throw std::runtime_error(msg); //', text)
text = re.sub(r'\n\s*if\s*[(]\s*!\s*yyin\s*[)]\s*\n\s*yyin\s*=\s*stdin;\s*\n', '\n', text)
text = re.sub(r'\n\s*if\s*[(]\s*!\s*yyout\s*[)]\s*\n\s*yyout\s*=\s*stdout;\s*\n', '\n', text)

file_null = 'NULL' if platform == 'linux' else '[(]FILE [*][)] 0'

text = re.sub(
    rf'[#]ifdef\s*YY_STDINIT\n\s*yyin = stdin;\n\s*yyout = stdout;\n[#]else\n\s*yyin = {file_null};\n\s*yyout = {file_null};\n[#]endif',
    '    yyin = (FILE *) 0;\n    yyout = (FILE *) 0;',
    text,
)

for forbidden in ['stdin;', 'stdout', 'fprintf(', 'exit(']:
    if forbidden in text:
        print("%s not removed!" % forbidden)
        exit(1)

# strip the trailing whitespace flex 2.6.4 leaves on some lines
with open_utf8(target_file, 'w+') as f:
    f.write('\n'.join(line.rstrip() for line in text.splitlines()) + '\n')
