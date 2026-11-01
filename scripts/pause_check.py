#!/usr/bin/env python3
"""Bounded credit-starvation/heartbeat/cancel test; synthetic audio only, no rendering."""
import argparse, base64, json, struct, sys, threading, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests/integration'))
from transport_tests import Peer, request

def run(host, port, fixture, seconds):
    assert 5 <= seconds <= 45
    packets = [base64.b64decode(p) for p in fixture['packets']]
    # The synthetic fixture has sample timestamps/durations and no trimming.
    fields = [struct.unpack('>4sBBHQQqqqIIII', p[:64]) for p in packets]
    assert all(p[3] & 3 == 3 and p[8] > 0 and p[10] == 0 and p[11] == 0 for p in fields)
    cycle_duration = sum(p[8] for p in fields)
    peer = Peer(port, host=host)
    peer.socket.settimeout(seconds + 15)
    window = int(peer.hello['limits']['packet_payload_bytes']) + 64
    start = dict(fixture['start'], output_limit_bytes=str(window))
    peer.send(start)
    peer.until('started')
    changed = threading.Condition()
    state = {'limit': int(peer.hello['input_limit_bytes']), 'sent': 0, 'received': 0, 'packets': 0,
        'waiting_credit': False, 'saw_credit_wait': False, 'format_seen': False, 'cancelled': False}
    stop = threading.Event()
    errors = []
    def producer():
        try:
            sequence = cycle = 0
            while not stop.is_set():
                for source, original in zip(packets, fields):
                    value = bytearray(source)
                    struct.pack_into('>Q', value, 16, sequence)
                    struct.pack_into('>q', value, 24, original[6] + cycle * cycle_duration)
                    struct.pack_into('>q', value, 32, original[7] + cycle * cycle_duration)
                    with changed:
                        while not stop.is_set() and state['sent'] + len(value) > state['limit']:
                            state['waiting_credit'] = state['saw_credit_wait'] = True
                            changed.wait(timeout=1)
                        state['waiting_credit'] = False
                        if stop.is_set():
                            return
                        state['sent'] += len(value)
                    peer.send(value)
                    sequence += 1
                cycle += 1
        except Exception as error:
            if not stop.is_set():
                errors.append(error)
    def receiver():
        try:
            while not stop.is_set():
                value = peer.receive()
                if isinstance(value, bytes):
                    assert state['format_seen']
                    state['received'] += len(value)
                    state['packets'] += 1
                    assert state['received'] <= window
                    # Intentionally withhold all output credit; controls must continue.
                elif value['type'] == 'output_format':
                    state['format_seen'] = True
                elif value['type'] == 'credit':
                    with changed:
                        assert int(value['limit_bytes']) >= state['limit']
                        state['limit'] = int(value['limit_bytes'])
                        changed.notify_all()
                elif value['type'] == 'cancelled':
                    state['cancelled'] = True
                    stop.set()
                    with changed:
                        changed.notify_all()
                    return
                else:
                    raise RuntimeError('unexpected_control')
        except Exception as error:
            if not stop.is_set():
                errors.append(error)
                stop.set()
                with changed:
                    changed.notify_all()
    sender = threading.Thread(target=producer)
    reader = threading.Thread(target=receiver)
    sender.start(); reader.start()
    began = time.monotonic()
    cancel_latency = None
    try:
        deadline = began + seconds
        while time.monotonic() < deadline and not errors:
            time.sleep(min(1, max(0, deadline - time.monotonic())))
        assert not errors and state['format_seen'] and state['saw_credit_wait']
        assert peer.ping_count >= 1
        assert request(port, '/healthz', host=host)[0] == 200
        cancelled_at = time.monotonic()
        peer.send({'type': 'cancel', 'generation': start['generation']})
        reader.join(timeout=5)
        cancel_latency = time.monotonic() - cancelled_at
        assert state['cancelled'] and not errors
    finally:
        stop.set()
        with changed:
            changed.notify_all()
        peer.close()
        sender.join(timeout=6); reader.join(timeout=6)
        assert not sender.is_alive() and not reader.is_alive()
    return {'criterion_id': 'external_audio.credit_pause_cancel.v1', 'status': 'passed',
        'pause_seconds': seconds, 'ping_count': peer.ping_count, 'output_credit_granted': '0',
        'received_audio_bytes': str(state['received']), 'sent_audio_bytes': str(state['sent']),
        'producer_credit_wait_observed': state['saw_credit_wait'], 'cancel_latency_seconds': cancel_latency,
        'physical_acceptance': False}

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=9078)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=35)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        evidence = run(args.host, args.port, json.loads(args.fixture.read_text()), args.seconds)
        code = 0
    except Exception:
        evidence = {'criterion_id': 'external_audio.credit_pause_cancel.v1', 'status': 'failed',
            'code': 'pause_or_cleanup_failed', 'physical_acceptance': False}
        code = 1
    args.output.write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence))
    sys.exit(code)
