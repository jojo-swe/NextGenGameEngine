import re, os

errors_file = r'p:\Development\personal\games\NextGenGameEngine\all_errors2.txt'
errors = open(errors_file, encoding='utf-8', errors='replace').readlines()
test_files = set()
for l in errors:
    m = re.search(r'(tests\\unit\\test_\w+\.cpp)', l)
    if m and ('undeclared identifier' in l or 'syntax error: identifier' in l or 'missing type specifier' in l or 'unexpected token' in l):
        fp = os.path.join(r'p:\Development\personal\games\NextGenGameEngine', m.group(1))
        if os.path.exists(fp):
            test_files.add(fp)

fixed = 0
for fp in sorted(test_files):
    content = open(fp, 'r', encoding='utf-8').read()
    changed = False

    # Add types.h include if missing
    types_inc = '#include "engine/core/types.h"'
    if types_inc not in content:
        gtest_inc = '#include <gtest/gtest.h>'
        if gtest_inc in content:
            content = content.replace(gtest_inc, gtest_inc + '\n' + types_inc, 1)
        else:
            content = types_inc + '\n' + content
        changed = True

    # Add using namespace nge if missing
    if 'using namespace nge;' not in content:
        # Find first "using namespace nge::" line
        m2 = re.search(r'(using namespace nge::\w+;)', content)
        if m2:
            content = content.replace(m2.group(1), 'using namespace nge;\n' + m2.group(1), 1)
        else:
            # Insert after last #include line
            lines = content.split('\n')
            insert_idx = 0
            for i, line in enumerate(lines):
                if line.startswith('#include'):
                    insert_idx = i + 1
            lines.insert(insert_idx, '')
            lines.insert(insert_idx + 1, 'using namespace nge;')
            content = '\n'.join(lines)
        changed = True

    if changed:
        open(fp, 'w', encoding='utf-8').write(content)
        fixed += 1
        print(f'Fixed: {os.path.basename(fp)}')

print(f'\nTotal fixed: {fixed}')
