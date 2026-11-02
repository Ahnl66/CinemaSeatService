#!/usr/bin/env python3
"""RFC6455 peer for black-box tests only. Production uses Boost.Beast."""
import base64, hashlib, http.client, json, os, socket, struct, subprocess, threading, time
from pathlib import Path

class Peer:
    def __init__(self, port, origin=None, protocol='cs-audio.v1', host='127.0.0.1'):
        self.lock = threading.Lock()
        self.ping_count = 0
        self.socket = socket.create_connection((host, port), timeout=5)
        self.socket.settimeout(5)
        key = base64.b64encode(os.urandom(16)).decode()
        headers = f'GET /v1/session HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: {protocol}\r\n'
        if origin:
            headers += 'Origin: ' + origin + '\r\n'
        self.socket.sendall((headers + '\r\n').encode())
        response = b''
        while not response.endswith(b'\r\n\r\n'):
            response += self.exact(1)
            assert len(response) <= 16384
        self.status = int(response.split(b' ')[1])
        if self.status == 101:
            accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
            assert accept in response and b'cs-audio.v1' in response
            self.hello = self.receive()
            assert self.hello['type'] == 'hello'

    def exact(self, n):
        result = b''
        while len(result) < n:
            block = self.socket.recv(n - len(result))
            if not block:
                raise EOFError('peer closed')
            result += block
        return result

    def send(self, value, opcode=None, final=True):
        if isinstance(value, dict):
            value = json.dumps(value, separators=(',', ':')).encode()
            opcode = 1
        if isinstance(value, str):
            value = value.encode()
            opcode = 1
        if opcode is None:
            opcode = 2
        mask = os.urandom(4)
        length = len(value)
        prefix = bytes([(128 if final else 0) | opcode])
        if length < 126:
            prefix += bytes([128 | length])
        elif length < 65536:
            prefix += bytes([254]) + struct.pack('>H', length)
        else:
            prefix += bytes([255]) + struct.pack('>Q', length)
        wire = prefix + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(value))
        with self.lock:
            self.socket.sendall(wire)

    def receive(self):
        data, opcode = b'', None
        while True:
            a, b = self.exact(2)
            assert not b & 128
            n = b & 127
            if n == 126:
                n, = struct.unpack('>H', self.exact(2))
            if n == 127:
                n, = struct.unpack('>Q', self.exact(8))
            assert n <= 1048640
            body = self.exact(n)
            kind = a & 15
            if kind == 9:
                self.ping_count += 1
                self.send(body, opcode=10)
                continue
            if kind == 10:
                continue
            if kind == 8:
                self.send(body, opcode=8)
                return {'type': 'closed'}
            if kind in (1, 2):
                opcode = kind
            else:
                assert kind == 0 and opcode
            data += body
            assert len(data) <= 1048640
            if a & 128:
                return json.loads(data) if opcode == 1 else data

    def until(self, kind):
        for _ in range(10000):
            value = self.receive()
            if isinstance(value, dict) and value.get('type') == kind:
                return value
            assert not isinstance(value, dict) or value.get('type') not in ('error', 'closed'), value
        raise AssertionError('bounded receive exceeded')

    def close(self):
        self.socket.close()

def request(port, path, method='GET', host='127.0.0.1'):
    connection = http.client.HTTPConnection(host, port, timeout=3)
    connection.request(method, path)
    response = connection.getresponse()
    result = response.status, json.loads(response.read(65537))
    connection.close()
    return result

def transcode(port, fixture, cancel=False, host='127.0.0.1', observe=None):
    began = time.monotonic()
    peer = Peer(port, host=host)
    assert peer.status == 101
    start = fixture['start'].copy()
    window = max(16448, int(peer.hello['limits']['packet_payload_bytes']) + 64)
    start['output_limit_bytes'] = str(window)
    peer.send(start)
    peer.until('started')
    condition = threading.Condition()
    limit = int(peer.hello['input_limit_bytes'])
    sent = 0
    errors = []
    stop = False
    def produce():
        nonlocal sent
        try:
            for index, encoded in enumerate(fixture['packets']):
                packet = base64.b64decode(encoded)
                with condition:
                    assert condition.wait_for(lambda: stop or sent + len(packet) <= limit, timeout=5)
                    if stop:
                        return
                    sent += len(packet)
                if index == 0:
                    peer.send(packet[:15], opcode=2, final=False)
                    peer.send(packet[15:], opcode=0)
                else:
                    peer.send(packet)
            peer.send({'type': 'end_input', 'generation': '1', 'input_packets': str(len(fixture['packets']))})
        except Exception as error:
            errors.append(error)
    thread = threading.Thread(target=produce)
    thread.start()
    samples = total_bytes = count = 0
    next_pts = None
    format_seen = False
    rate = None
    first_output_seconds = None
    completed = False
    try:
        for _ in range(10000):
            value = peer.receive()
            if isinstance(value, bytes):
                if first_output_seconds is None:
                    first_output_seconds = time.monotonic() - began
                assert format_seen and value[:4] == b'CSAD'
                _, version, kind, flags, generation, sequence, pts, dts, duration, size, ss, se, reserved = struct.unpack('>4sBBHQQqqqIIII', value[:64])
                assert (version, kind, flags, generation, sequence, dts, size, ss, se, reserved) == (1, 2, 3, 1, count, pts, len(value) - 64, 0, 0, 0)
                assert duration > 0 and (next_pts is None or pts == next_pts)
                if observe is not None:
                    observe('packet', value)
                next_pts = pts + duration
                samples += duration
                count += 1
                total_bytes += len(value)
                if cancel and total_bytes > 4000:
                    # Withhold credit until the encoder/output queue is blocked.
                    time.sleep(.3)
                    assert request(port, '/healthz', host=host)[0] == 200
                    peer.send({'type': 'cancel', 'generation': '1'})
                    peer.until('cancelled')
                    return
                peer.send({'type': 'credit', 'direction': 'output', 'limit_bytes': str(total_bytes + window)})
            elif value['type'] == 'credit':
                with condition:
                    limit = int(value['limit_bytes'])
                    condition.notify_all()
            elif value['type'] == 'output_format':
                assert not format_seen and len(base64.b64decode(value['extradata_base64'])) == 34
                if observe is not None:
                    observe('format', value)
                format_seen = True
                rate = value['sample_rate']
            elif value['type'] == 'end_output':
                assert int(value['total_samples']) == samples
                assert int(value['output_packets']) == count
                assert int(value['end_pts_exclusive']) == next_pts
                assert not cancel
                if observe is not None:
                    observe('end', value)
                peer.send(struct.pack('>H', 1000), opcode=8)
                completed = True
                return {'valid_samples': samples, 'sample_rate': rate, 'output_packets': count, 'output_bytes': total_bytes, 'wall_seconds': time.monotonic() - began, 'first_output_seconds': first_output_seconds}
            else:
                raise AssertionError(value)
        raise AssertionError('bounded session exceeded')
    finally:
        with condition:
            stop = True
            condition.notify_all()
        peer.close()
        thread.join(timeout=6)
        assert not thread.is_alive()
        # A producer broken pipe during teardown must not replace the primary
        # service/protocol failure already raised by receive.
        if not cancel and completed:
            assert not errors, errors

def run(binary, fixture_dir, scratch):
    scratch = Path(scratch)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    settings = {'version': 1, 'listen': {'address': '127.0.0.1', 'port': port},
        'sessions': {'maximum': 2}, 'limits': {'packet_payload_bytes': 16384, 'input_window_bytes': 65536, 'output_window_bytes': 16448, 'queued_output_bytes': 65536},
        'timeouts': {'start_seconds': 1, 'ping_seconds': 5, 'pong_seconds': 1}}
    path = scratch / 'test-settings.json'
    path.write_text(json.dumps(settings))
    with (scratch / 'transport-service.log').open('w') as log:
        process = subprocess.Popen([str(binary), '--settings', str(path)], stdout=log, stderr=log)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise AssertionError('service failed startup')
                try:
                    assert request(port, '/healthz') == (200, {'status': 'ok', 'protocol': 1})
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError('startup deadline exceeded')
            subprocess.run([str(binary), '--settings', str(path), '--healthcheck'], check=True, timeout=5)
            assert request(port, '/missing')[0] == 404
            assert request(port, '/healthz', 'POST')[0] == 405
            for kwargs in ({'origin': 'https://example.invalid'}, {'protocol': 'wrong'}):
                peer = Peer(port, **kwargs)
                assert peer.status == 400
                peer.close()
            peers = [Peer(port), Peer(port), Peer(port)]
            assert [p.status for p in peers] == [101, 101, 503]
            assert request(port, '/healthz')[0] == 200
            for peer in peers:
                peer.close()
            time.sleep(.1)
            peer = Peer(port)
            peer.send('{"type":"start","type":"cancel"}')
            assert peer.until('error')['code'] == 'protocol_error'
            assert peer.until('closed')['type']=='closed'  # error must precede a completed close handshake
            peer.close()
            time.sleep(.1)
            peer = Peer(port)
            assert peer.until('error')['code'] == 'timeout'
            assert peer.until('closed')['type']=='closed'  # error must precede a completed close handshake
            peer.close()
            time.sleep(.1)
            for codec in ['truehd', 'mlp', 'dts']:
                fixture = json.loads((Path(fixture_dir) / (codec + '.json')).read_text())
                transcode(port, fixture)
                time.sleep(.05)
            fixture = json.loads((Path(fixture_dir) / 'dts.json').read_text())
            # Failure-only telemetry must traverse the real WebSocket close path.
            peer=Peer(port);peer.send(dict(fixture['start'],output_limit_bytes='16448'));peer.until('started')
            for index,encoded in enumerate(fixture['packets'][:33]):
                packet=bytearray(base64.b64decode(encoded))
                if index==32:
                    pts=struct.unpack_from('>q',packet,24)[0];struct.pack_into('>q',packet,24,pts+48000)
                peer.send(bytes(packet))
            received=0
            for _ in range(1000):
                failure=peer.receive()
                if isinstance(failure,bytes):
                    received+=len(failure);peer.send({'type':'credit','direction':'output','limit_bytes':str(received+16448)})
                elif failure.get('type')=='error':break
            else:raise AssertionError('timeline failure not delivered')
            details=failure['timeline']
            assert failure['code']=='timeline_discontinuity' and details['contract']=='external_audio.timeline_failure.v1'
            assert int(details['expected_pts'])==int(details['first_pts'])+int(details['accepted_samples'])
            assert abs(int(details['observed_pts'])-int(details['expected_pts']))>int(details['tolerance_samples'])
            assert details['input']['sequence']=='32' and len(details['input']['payload_sha256'])==64
            assert details['input']['payload_sha256']==hashlib.sha256(bytes(packet)[64:]).hexdigest()
            assert len(json.dumps(details))<4096 and 'payload' not in details['input']
            assert peer.until('closed')['type']=='closed'
            peer.close();time.sleep(.05)
            # Reject out-of-range generations/sequences/reserved bits/length before decode.
            for offset in [8, 16, 60, 48]:
                peer = Peer(port)
                peer.send(dict(fixture['start'], output_limit_bytes='16448'))
                peer.until('started')
                bad = bytearray(base64.b64decode(fixture['packets'][0]))
                bad[offset] ^= 128
                peer.send(bytes(bad))
                assert peer.until('error')['code'] == 'invalid_packet'
                assert peer.until('closed')['type']=='closed'  # error must precede a completed close handshake
                peer.close()
                time.sleep(.03)
            peer = Peer(port)
            peer.send(dict(fixture['start'], output_limit_bytes='16448'))
            peer.until('started')
            peer.send({'type': 'end_input', 'generation': '1', 'input_packets': '1'})
            assert peer.until('error')['code'] == 'protocol_error'
            assert peer.until('closed')['type']=='closed'  # error must precede a completed close handshake
            peer.close()
            time.sleep(.03)
            # Concurrent independent sessions, not an estimate of hardware capacity.
            parallel_errors = []
            def concurrent():
                try:
                    transcode(port, fixture)
                except Exception as error:
                    parallel_errors.append(error)
            threads = [threading.Thread(target=concurrent) for _ in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=15)
                assert not thread.is_alive()
            assert not parallel_errors, parallel_errors
            time.sleep(.1)
            transcode(port, fixture, cancel=True)
            time.sleep(.1)
            assert request(port, '/v1/capabilities')[1]['available_sessions'] == 2
            # Cancellation while input and output production are both idle.
            peer = Peer(port)
            peer.send(dict(fixture['start'], output_limit_bytes='16448'))
            peer.until('started')
            time.sleep(5.2)  # crosses ping; no data-idle timeout is permitted
            peer.send({'type': 'cancel', 'generation': '1'})
            peer.until('cancelled')
            peer.close()
            peer = Peer(port)
            peer.send(dict(fixture['start'], output_limit_bytes='16448'))
            peer.until('started')
            process.terminate()
            assert peer.until('error')['code'] == 'server_shutdown'
            assert peer.until('closed')['type']=='closed'  # error must precede a completed close handshake
            peer.close()
            assert process.wait(timeout=12) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    print('transport PASS: health, capacity, strict JSON, fragmented packets, real codecs, credits, pause/ping, cancel, shutdown, slot recovery')

if __name__ == '__main__':
    import sys
    run(*sys.argv[1:])
