#!/usr/bin/env python3
import json, socket, subprocess, sys, time
from pathlib import Path
from transport_tests import Peer, request

def run(binary, fixtures, scratch):
    scratch = Path(scratch)
    fixture = json.loads((Path(fixtures) / 'dts.json').read_text())
    for mode in ['disabled', 'rotated', 'unwritable']:
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        logpath = scratch / (mode + '.jsonl')
        if mode == 'unwritable':
            logpath = scratch / 'nonexistent-log-parent/events.jsonl'
        settings = {'version': 1, 'listen': {'address': '127.0.0.1', 'port': port},
            'logging': {'debug_enabled': mode != 'disabled', 'file': str(logpath),
                        'maximum_file_bytes': 1024, 'retained_files': 2}}
        path = scratch / ('logging-' + mode + '.json')
        path.write_text(json.dumps(settings))
        with (scratch / ('logging-' + mode + '.stderr')).open('w') as stderr:
            process = subprocess.Popen([str(binary), '--settings', str(path)], stderr=stderr)
            try:
                for _ in range(100):
                    assert process.poll() is None
                    try:
                        assert request(port, '/healthz')[0] == 200
                        break
                    except OSError:
                        time.sleep(.03)
                for _ in range(30 if mode == 'rotated' else 1):
                    peer = Peer(port)
                    peer.send(fixture['start'])
                    peer.until('started')
                    peer.send({'type': 'cancel', 'generation': '1'})
                    peer.until('cancelled')
                    peer.close()
                    time.sleep(.01)
                assert request(port, '/healthz')[0] == 200
            finally:
                process.terminate()
                assert process.wait(timeout=12) == 0
        if mode == 'disabled':
            assert not logpath.exists()
        elif mode == 'rotated':
            files = list(scratch.glob('rotated.jsonl*'))
            assert len(files) == 3
            for file in files:
                assert file.stat().st_size <= 1024
                for line in file.read_text().splitlines():
                    value = json.loads(line)
                    assert set(value) <= {'event', 'session_id', 'dropped_log_count', 'input_high_water_bytes', 'output_high_water_bytes', 'wall_ms'}
        else:
            text = (scratch / 'logging-unwritable.stderr').read_text()
            assert text.count('warning: debug_logging_disabled') == 1
    print('logging PASS: disabled touches no file; bounded rotation; one sanitized warning on write failure; sessions survive')

if __name__ == '__main__':
    run(*sys.argv[1:])
