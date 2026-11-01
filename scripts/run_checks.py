#!/usr/bin/env python3
"""Standalone host checks; --all fails on missing mandatory release evidence."""
import argparse, hashlib, json, os, platform, shutil, subprocess, sys, time
from pathlib import Path
from build_dependencies import build

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--all', action='store_true')
parser.add_argument('--host', action='store_true', help='Run independent native checks; not full release acceptance')
parser.add_argument('--scratch', type=Path, required=True)
args = parser.parse_args()
if not args.scratch.is_absolute():
    parser.error('--scratch must be absolute')
scratch = args.scratch.resolve()
scratch.mkdir(parents=True, exist_ok=True)
results = []

def run(name, command, timeout=900):
    print('+', name, flush=True)
    path = scratch / (name + '.log')
    started = time.monotonic()
    with path.open('w') as log:
        result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    results.append({'check': name, 'status': 'passed' if result.returncode == 0 else 'failed',
        'seconds': round(time.monotonic() - started, 3), 'log': path.name})
    if result.returncode:
        print(path.read_text()[-6000:])
        raise RuntimeError(name + ' failed')

def incomplete(name, reason):
    results.append({'check': name, 'status': 'not_executed', 'reason': reason})

exit_code = 0
try:
    production = build(scratch / 'dependencies')
    test = build(scratch / 'dependencies', test_codecs=True)
    for name, deps, testing in [('runtime', production, 'OFF'), ('host', test, 'ON')]:
        directory = scratch / name
        run(name + '-configure', ['cmake', '-S', ROOT, '-B', directory, '-DCSAD_DEPS=' + str(deps), '-DBUILD_TESTING=' + testing, '-DCMAKE_BUILD_TYPE=Debug'])
        run(name + '-build', ['cmake', '--build', directory, '-j', '4'])
    binary = scratch / 'runtime/cinemaseat-audio-decoder'
    run('registry', [sys.executable, ROOT / 'scripts/check_codec_registry.py', binary])
    run('core', [scratch / 'host/core_tests', ROOT / 'tests/fixtures', scratch / 'generated'])
    run('asan-configure', ['cmake', '-S', ROOT, '-B', scratch / 'asan', '-DCSAD_DEPS=' + str(test), '-DCSAD_SANITIZERS=address,undefined', '-DCMAKE_BUILD_TYPE=Debug'])
    run('asan-build', ['cmake', '--build', scratch / 'asan', '--target', 'core_tests', '-j', '4'])
    run('asan-core', [scratch / 'asan/core_tests', ROOT / 'tests/fixtures'])
    run('logging', [sys.executable, ROOT / 'tests/integration/logging_tests.py', binary, scratch / 'generated', scratch])
    run('transport', [sys.executable, ROOT / 'tests/integration/transport_tests.py', binary, scratch / 'generated', scratch])
    if args.all:
        if shutil.which('docker') is None:
            incomplete('containers-amd64-arm64', 'Docker absent on this host; user prohibits installing it here. Run native on external servers.')
        else:
            run('docker-version', ['docker', 'version'])
            run('docker-native-build', ['docker', 'build', '-f', ROOT / 'docker/Dockerfile', '-t', 'cinemaseat-audio-decoder:0.1.1', ROOT], timeout=1800)
            run('docker-native-registry', ['docker', 'run', '--rm', '--read-only', '--cap-drop=ALL', '--security-opt=no-new-privileges', 'cinemaseat-audio-decoder:0.1.1', '--codec-registry'])
            run('docker-native-runtime', [sys.executable, ROOT / 'scripts/container_checks.py', scratch])
            incomplete('container-audio', 'Native container audio roundtrip and both target hardware endurance runs remain required.')
            incomplete('other-native-architecture', 'One native build cannot prove both amd64 and arm64; collect separate native server evidence.')
        incomplete('dts-hd-ma-equivalence', 'Representative licensed DTS-HD MA packet fixture and reference PCM not supplied.')
        incomplete('hardware-acceptance', 'DS923+, ODROID-N2+ and physical Apple TV runs not performed on this host.')
        exit_code = 2
except Exception as error:
    results.append({'check': 'runner', 'status': 'failed', 'reason': str(error)})
    exit_code = 1
finally:
    # Exclude generated/build/media files from source fingerprint.
    files = sorted(p for p in ROOT.rglob('*') if p.is_file() and not any(x in p.relative_to(ROOT).parts for x in ['.git', 'build', 'local', '__pycache__']))
    digest = hashlib.sha256()
    for path in files:
        digest.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
    report = {'service_version': (ROOT / 'VERSION').read_text().strip(), 'source_sha256': digest.hexdigest(),
        'architecture': platform.machine(), 'system': platform.system(), 'checks': results,
        'full_acceptance': False, 'exit_code': exit_code}
    (scratch / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
sys.exit(exit_code)
