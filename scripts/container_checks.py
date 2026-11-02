#!/usr/bin/env python3
"""Run only where Docker already exists. Never install Docker or use emulation."""
import json, subprocess, sys, time, uuid
from pathlib import Path
scratch = Path(sys.argv[1]).resolve()
image = 'cinemaseat-audio-decoder:0.1.4'
name = 'csad-check-' + uuid.uuid4().hex[:12]
path = scratch / 'container-settings.json'
path.write_text('{"version":1}')
path.chmod(0o644)

def command(*args):
    return subprocess.check_output(['docker', *args], text=True, stderr=subprocess.STDOUT).strip()

started = False
try:
    server_arch = json.loads(command('info', '--format', '{{json .Architecture}}'))
    image_info = json.loads(command('image', 'inspect', image))[0]
    image_arch = image_info['Architecture']
    native = {'amd64': 'amd64', 'x86_64': 'amd64', 'arm64': 'arm64', 'aarch64': 'arm64'}
    if native.get(server_arch) != image_arch:
        raise RuntimeError('Image/daemon architecture mismatch; no emulated acceptance')
    command('run', '-d', '--name', name, '--read-only', '--cap-drop=ALL',
        '--security-opt=no-new-privileges', '--memory=512m', '--pids-limit=96',
        '--mount', 'type=bind,src=' + str(path) + ',dst=/config/settings.json,readonly', image)
    started = True
    for _ in range(50):
        result = subprocess.run(['docker', 'exec', name, '/opt/csad/bin/cinemaseat-audio-decoder', '--healthcheck'], capture_output=True, timeout=5)
        if result.returncode == 0:
            break
        time.sleep(.1)
    else:
        raise RuntimeError('Read-only container health deadline exceeded')
    info = json.loads(command('inspect', name))[0]
    assert info['Config']['User'] == '10001:10001'
    assert info['HostConfig']['ReadonlyRootfs']
    assert len(info['Mounts']) == 1
    assert info['Mounts'][0]['Destination'] == '/config/settings.json'
    report = {'status': 'passed', 'architecture': image_arch, 'image_id': image_info['Id'],
        'checks': ['native architecture', 'actual read-only runtime startup', 'non-root', 'healthcheck', 'settings-only mount'],
        'audio_roundtrip_in_container': 'not_executed', 'hardware_acceptance': False}
    (scratch / 'container-checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
finally:
    if started:
        subprocess.run(['docker', 'stop', '--time', '15', name], check=True, capture_output=True, timeout=20)
        subprocess.run(['docker', 'rm', name], check=True, capture_output=True, timeout=10)
