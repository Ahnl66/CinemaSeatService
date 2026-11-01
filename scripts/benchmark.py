#!/usr/bin/env python3
"""Unpaced component throughput only; never physical or sustained playback proof."""
import argparse, concurrent.futures, http.client, json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests/integration'))
from transport_tests import transcode

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--port', type=int, default=9078)
parser.add_argument('--fixture', type=Path, required=True, help='Local packet fixture, not a media path sent to the service')
parser.add_argument('--sessions', type=int, choices=[1, 2, 4], default=1)
parser.add_argument('--iterations', type=int, default=1)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not 1 <= args.iterations <= 1000:
    parser.error('iterations must be 1..1000')
fixture = json.loads(args.fixture.read_text())
connection = http.client.HTTPConnection(args.host, args.port, timeout=5)
connection.request('GET', '/v1/capabilities')
response = connection.getresponse()
if response.status != 200:
    raise SystemExit('Capabilities unavailable')
capabilities = json.loads(response.read(65537))
connection.close()
results = []
began = time.monotonic()
for iteration in range(args.iterations):
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.sessions) as pool:
        futures = [pool.submit(transcode, args.port, fixture, host=args.host) for _ in range(args.sessions)]
        for future in futures:
            result = future.result(timeout=120)
            result['throughput_realtime'] = result['valid_samples'] / result['sample_rate'] / result['wall_seconds']
            results.append(result)
report = {'kind': 'unpaced_component_throughput.v1', 'service': capabilities,
    'sessions': args.sessions, 'iterations': args.iterations, 'wall_seconds': time.monotonic() - began,
    'measurements': results, 'physical_acceptance': False,
    'missing': ['60-minute continuous realtime session', 'client preheader/fMP4', 'physical rendering/lipsync',
                'pause/seek/track changes', 'server CPU/RSS/temperature/throttling measurements']}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print('Component measurements saved; physical and sustained playback acceptance remain open.')
