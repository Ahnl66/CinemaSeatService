#!/usr/bin/env python3
"""Inspect actual linked runtime registration, not configure flag text."""
import subprocess, sys
expected = {'decoder dca', 'decoder truehd', 'decoder mlp', 'encoder flac'}
actual = set(subprocess.check_output([sys.argv[1], '--codec-registry'], text=True).splitlines())
if actual != expected:
    raise SystemExit('Unexpected runtime codec registry: ' + repr(sorted(actual)))
print('PASS runtime registry: only dca/truehd/mlp decoders and flac encoder')
