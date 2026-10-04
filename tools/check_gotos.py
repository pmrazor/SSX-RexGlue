"""Report gotos in generated code whose label is not defined in the same function.

Prints the target address of each dangling goto (one per line).
"""
import glob
import re
import sys

out = set()
for path in glob.glob('generated/default/ssx_recomp.*.cpp'):
    text = open(path).read()
    for body in re.split(r'^DEFINE_REX_FUNC\(', text, flags=re.M)[1:]:
        labels = set(re.findall(r'^(loc_[0-9A-F]+):', body, re.M))
        for g in set(re.findall(r'goto (loc_[0-9A-F]+);', body)):
            if g not in labels:
                out.add(g[4:])
                print(f'{path}: {body.split(")")[0]} -> {g}', file=sys.stderr)
print('\n'.join(sorted(out)))
