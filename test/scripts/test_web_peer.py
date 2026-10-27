"""The E2E WebSocket peer must keep frame boundaries across read deadlines."""
import importlib.util
from pathlib import Path
import socket
import struct


def test_partial_websocket_frame_survives_collect_timeout():
    path = Path(__file__).resolve().parents[1] / 'e2e/test_web.py'
    spec = importlib.util.spec_from_file_location('web_peer_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    class Socket:
        chunks = []
        def settimeout(self, _):
            pass
        def recv(self, _):
            if self.chunks:
                return self.chunks.pop(0)
            raise socket.timeout()

    payload = b'\x1b[38;2;32;221;235m' * 40
    frame = b'\x82\x7e' + struct.pack('!H', len(payload)) + payload
    peer = object.__new__(module.WS)
    peer.socket = Socket()
    peer.buffer = b''
    peer.socket.chunks = [frame[:4], frame[4:15]]
    assert peer.collect(.001) == (b'', [])
    pong = b'{"t":"pong"}'
    peer.socket.chunks = [frame[15:], bytes([0x81, len(pong)]) + pong]
    assert peer.collect(.001) == (payload, [{'t': 'pong'}])
