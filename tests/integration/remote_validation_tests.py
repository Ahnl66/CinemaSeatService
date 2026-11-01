#!/usr/bin/env python3
"""Exercise the reusable remote PCM checker against the actual local service."""
import argparse, base64, copy, json, socket, subprocess, sys, time
from pathlib import Path
from transport_tests import request, transcode
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from remote_checks import Observation, checked_transcode, RemoteFailure
from pause_check import run as pause_check

def run(binary, validator, exporter, fixtures, scratch, source=None, expect_source_clock_error=False):
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    settings = scratch / 'remote-local-settings.json'
    settings.write_text(json.dumps({'version': 1, 'listen': {'address': '127.0.0.1', 'port': port}, 'timeouts': {'ping_seconds': 5, 'pong_seconds': 3}, 'logging': {'debug_enabled': True, 'file': str(scratch / 'remote-local-debug.jsonl')}}))
    with (scratch / 'remote-local-service.log').open('w') as log:
        process = subprocess.Popen([str(binary), '--settings', str(settings)], stdout=log, stderr=log)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError('service_startup_failed')
                try:
                    if request(port, '/healthz')[0] == 200:
                        break
                except OSError:
                    time.sleep(.03)
            else:
                raise RuntimeError('service_startup_timeout')
            command = [sys.executable, str(ROOT / 'scripts/remote_checks.py'), '--host', '127.0.0.1', '--port', str(port),
                '--validator', str(validator), '--output', str(scratch / 'remote-pcm-local.json')]
            if source:
                command += ['--source', str(source), '--exporter', str(exporter)]
            else:
                command += ['--fixture-dir', str(fixtures)]
            result = subprocess.run(command, timeout=180)
            if expect_source_clock_error:
                evidence = json.loads((scratch / 'remote-pcm-local.json').read_text())
                assert result.returncode != 0 and evidence['error_code'] == 'timeline_discontinuity'
                assert evidence['source_inspection']['source_timeline_accepted'] is False
                print('Source-clock rejection regression PASS; source playback remains rejected')
            elif result.returncode:
                raise RuntimeError('remote_pcm_check_failed')
            # Metadata-only recovery inspection must not accept a core-only or
            # non-DTS fixture as complete full-HD material.
            for name in ['dts', 'truehd']:
                candidate = json.loads((fixtures / (name + '.json')).read_text())
                probe_result = subprocess.run([str(validator), '--inspect-after-recovery'], input=json.dumps(candidate), text=True, capture_output=True, timeout=30)
                proof = json.loads(probe_result.stdout)
                assert probe_result.returncode != 0 and proof['status'] == 'failed'
                assert proof['decoder_observation']['xll_recovery_requested'] == '0'
            # Corrupt returned metadata/packet/timeline independently; no weakening
            # of the checker to obtain a positive result is allowed.
            fixture = json.loads((fixtures / 'mlp-6ch.json').read_text())
            observed = Observation()
            transcode(port, fixture, observe=observed)
            body = {'fixture': fixture, 'observation': observed.value}
            def validate(value):
                result = subprocess.run([str(validator), '--validate-remote'], input=json.dumps(value), text=True, capture_output=True, timeout=30)
                proof = json.loads(result.stdout)
                assert result.returncode == 0 if proof['status'] == 'passed' else result.returncode != 0
                return proof
            assert validate(body)['status'] == 'passed'
            broken_input = copy.deepcopy(fixture)
            packet = bytearray(base64.b64decode(broken_input['packets'][0])); packet[0] ^= 128
            broken_input['packets'][0] = base64.b64encode(packet).decode()
            try:
                checked_transcode('127.0.0.1', port, broken_input, validator)
            except RemoteFailure as error:
                assert isinstance(error.cause, AssertionError) and isinstance(error.cause.args[0], dict)
                assert error.cause.args[0]['code'] == 'invalid_packet'
                assert error.evidence['coverage'] == 'incomplete'
            else:
                raise AssertionError('invalid packet unexpectedly passed')
            wrong = copy.deepcopy(body)
            wrong['observation']['format']['channel_order'][-2:] = ['SL', 'SR']
            assert validate(wrong)['status'] == 'failed'
            wrong = copy.deepcopy(body)
            del wrong['observation']['format']['channel_order']
            assert validate(wrong)['status'] == 'failed'
            wrong = copy.deepcopy(body)
            packet = bytearray(base64.b64decode(wrong['observation']['packets'][0]))
            packet[-1] ^= 128
            wrong['observation']['packets'][0] = base64.b64encode(packet).decode()
            assert validate(wrong)['status'] == 'failed'
            wrong = copy.deepcopy(body)
            wrong['observation']['end']['first_pts'] = str(int(wrong['observation']['end']['first_pts']) + 1)
            assert validate(wrong)['status'] == 'failed'
            pause = pause_check('127.0.0.1', port, json.loads((fixtures / 'dts-6ch.json').read_text()), 7)
            (scratch / 'credit-pause-local.json').write_text(json.dumps(pause, indent=2) + '\n')
        finally:
            process.terminate()
            assert process.wait(timeout=12) == 0
    print('remote PCM checker PASS: exact values/layout/rate/anchor/trims; corrupt metadata, audio and timeline rejected')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ['binary', 'validator', 'exporter', 'fixtures', 'scratch']:
        parser.add_argument(name, type=Path)
    parser.add_argument('--test-source', type=Path)
    parser.add_argument('--expect-source-clock-error', action='store_true')
    args = parser.parse_args()
    run(args.binary, args.validator, args.exporter, args.fixtures, args.scratch, args.test_source, args.expect_source_clock_error)
