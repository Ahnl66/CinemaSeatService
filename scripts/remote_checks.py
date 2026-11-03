#!/usr/bin/env python3
"""Real remote codec checks; audio is held only in RAM, reports contain hashes/metadata."""
import argparse, base64, hashlib, concurrent.futures, http.client, json, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests/integration'))
from transport_tests import transcode

class RemoteFailure(Exception):
    def __init__(self, cause, evidence):
        self.cause = cause
        self.evidence = evidence

class ValidationFailure(Exception):
    def __init__(self, proof):
        self.proof = proof

class Observation:
    def __init__(self):
        self.value = {'packets': []}
        self.bytes = 0
        self.samples = 0
    def __call__(self, kind, value):
        if kind == 'packet':
            self.bytes += len(value)
            if self.bytes > 32 * 1024 * 1024 or len(self.value['packets']) >= 65536:
                raise RuntimeError('observation_limit')
            self.value['packets'].append(base64.b64encode(value).decode())
            self.samples += int.from_bytes(value[40:48], 'big', signed=True)
        else:
            if kind in self.value:
                raise RuntimeError('duplicate_observation')
            self.value[kind] = value

def capabilities(host, port):
    connection = http.client.HTTPConnection(host, port, timeout=5)
    try:
        connection.request('GET', '/v1/capabilities')
        response = connection.getresponse()
        body = response.read(65537)
        if response.status != 200 or len(body) > 65536:
            raise RuntimeError('capabilities_failed')
        return json.loads(body)
    finally:
        connection.close()

def checked_transcode(host, port, fixture, validator):
    observation = Observation()
    try:
        metrics = transcode(port, fixture, host=host, observe=observation)
    except Exception as error:
        # Data was captured while receive/control was alive; retain only derived
        # metadata if the socket or decoder failed before complete output.
        format = observation.value.get('format', {})
        safe_format = {key: format[key] for key in ['codec', 'sample_rate', 'bits_per_sample', 'channel_order', 'time_base', 'source_profile'] if key in format}
        partial = {'received_packets': len(observation.value['packets']), 'received_bytes': str(observation.bytes),
            'observed_valid_samples': str(observation.samples), 'output_format': safe_format,
            'end_output_seen': 'end' in observation.value, 'coverage': 'incomplete', 'physical_acceptance': False}
        raise RemoteFailure(error, partial) from None
    result = subprocess.run([str(validator), '--validate-remote'],
        input=json.dumps({'fixture': fixture, 'observation': observation.value}),
        text=True, capture_output=True, timeout=120)
    proof = json.loads(result.stdout)
    if result.returncode or proof.get('status') != 'passed':
        raise ValidationFailure(proof)
    metrics['throughput_realtime'] = metrics['valid_samples'] / metrics['sample_rate'] / metrics['wall_seconds']
    return {'metrics': metrics, 'pcm_equivalence': proof}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=9078)
    parser.add_argument('--validator', type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--fixture-dir', type=Path)
    inputs.add_argument('--source', type=Path, help='Authorized local source; path is never sent or reported')
    parser.add_argument('--exporter', type=Path)
    parser.add_argument('--audio-ordinal', type=int, default=0)
    parser.add_argument('--layout-policy', choices=['dts-side-5.1-to-7.1-silence-v1','dts-native-or-side-7.1-v1'], help='Explicit original-source side padding investigation')
    parser.add_argument('--seconds', type=int, choices=range(1, 16), default=5)
    parser.add_argument('--sessions', type=int, choices=[1, 2, 4], default=1)
    parser.add_argument('--iterations', type=int, default=1)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.layout_policy and not args.source:parser.error('--layout-policy requires --source')
    if not 1 <= args.iterations <= 100:
        parser.error('iterations must be 1..100')
    if args.source and not args.exporter:
        parser.error('--source requires --exporter')
    report = {'criterion_id': 'external_audio.remote_codec.v1', 'status': 'running',
        'sessions': args.sessions, 'measurements': [], 'physical_acceptance': False,
        'missing': ['60-minute continuous session', 'client preheader/fMP4/AVPlayerItem',
            'physical channel/lip-sync/HDR/DV review', 'source seeks and track switches',
            'server CPU/RSS/temperature unless separately measured']}
    code = 1
    try:
        caps = capabilities(args.host, args.port)
        report['validator_sha256'] = hashlib.sha256(args.validator.read_bytes()).hexdigest()
        report['service'] = {key: caps[key] for key in ['protocol', 'service_version', 'build_id', 'ffmpeg_revision']}
        baseline = caps['available_sessions']
        if baseline < args.sessions:
            raise RuntimeError('capacity')
        if args.source:
            export = subprocess.run([str(args.exporter), str(args.source.resolve()), str(args.audio_ordinal), str(args.seconds)], capture_output=True, text=True, timeout=35)
            if export.returncode:
                raise RuntimeError('source_fixture_failed')
            fixture = json.loads(export.stdout)
            if args.layout_policy:
                if args.layout_policy not in caps.get('output_layout_policies',[]):raise RuntimeError('unsupported_layout_policy')
                if fixture['start']['input']['codec']!='dts':raise RuntimeError('unsupported_layout_policy')
                fixture['start']['output']['layout_policy']=args.layout_policy
                report['desired_settings']={'layout_policy':args.layout_policy,'source_channel_order':['FL','FR','FC','LFE','SL','SR'],'output_channel_order':['FL','FR','FC','LFE','BL','BR','SL','SR'],'silent_channels':['BL','BR']}
            source = fixture['start']['input']
            report['source_description'] = {key: source[key] for key in ['codec', 'sample_rate_hint', 'bits_per_sample_hint', 'channel_order_hint', 'time_base']}
            report['source_description']['expected_source_profile'] = fixture.get('expected_source_profile')
            inspection = subprocess.run([str(args.validator), '--inspect-source'], input=export.stdout, capture_output=True, text=True, timeout=120)
            report['source_inspection'] = json.loads(inspection.stdout)
            if inspection.returncode:
                raise RuntimeError('source_reference_decode_failed')
            fixtures = [('authorized-source', fixture)]
        else:
            fixtures = [(path.stem, json.loads(path.read_text())) for path in sorted(args.fixture_dir.glob('*.json'))]
        if not fixtures:
            raise RuntimeError('missing_fixtures')
        report['fixture_coverage'] = []
        for fixture_id, fixture in fixtures:
            source = fixture['start']['input']
            report['fixture_coverage'].append({'fixture_id': fixture_id, 'codec': source['codec'],
                'packet_count': len(fixture['packets']), 'sample_rate_hint': source['sample_rate_hint'],
                'bits_per_sample_hint': source['bits_per_sample_hint'], 'channel_order_hint': source['channel_order_hint'],
                'expected_source_profile': fixture.get('expected_source_profile'),
                'fixture_sha256': hashlib.sha256(json.dumps(fixture, sort_keys=True).encode()).hexdigest()})
            for iteration in range(args.iterations):
                with concurrent.futures.ThreadPoolExecutor(max_workers=args.sessions) as pool:
                    futures = [pool.submit(checked_transcode, args.host, args.port, fixture, args.validator) for _ in range(args.sessions)]
                    for future in futures:
                        measurement = future.result(timeout=130)
                        measurement['fixture_id'] = fixture_id
                        measurement['iteration'] = iteration
                        report['measurements'].append(measurement)
                deadline = time.monotonic() + 5
                while capabilities(args.host, args.port)['available_sessions'] < baseline:
                    if time.monotonic() >= deadline:
                        raise RuntimeError('session_cleanup_failed')
                    time.sleep(.05)
        report['status'] = 'passed'
        report['available_sessions_restored'] = True
        code = 0
    except Exception as error:
        if isinstance(error, RemoteFailure):
            report['partial_output'] = error.evidence
            error = error.cause
        # Fixed error categories only; no filenames, endpoint or raw codec errors.
        allowed = {'source_fixture_failed', 'source_reference_decode_failed', 'missing_fixtures', 'capacity', 'session_cleanup_failed', 'output_validation_failed', 'observation_limit', 'unsupported_layout_policy'}
        report['status'] = 'failed'
        report['error_kind'] = type(error).__name__
        report['error_code'] = str(error) if str(error) in allowed else 'remote_check_failed'
        if isinstance(error, ValidationFailure):
            report['error_code'] = 'output_validation_failed'
            report['validation_failure'] = {key: error.proof[key] for key in ['code', 'check_line'] if key in error.proof}
        if isinstance(error, AssertionError) and error.args and isinstance(error.args[0], dict):
            server_code = error.args[0].get('code')
            if server_code in {'protocol_error', 'unsupported_format', 'format_changed', 'decode_failed', 'encode_failed', 'missing_timeline', 'timeline_discontinuity', 'invalid_packet', 'limit_exceeded', 'timeout'}:
                report['error_code'] = server_code
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'measurements': len(report['measurements']), 'physical_acceptance': False}))
    return code

if __name__ == '__main__':
    sys.exit(main())
