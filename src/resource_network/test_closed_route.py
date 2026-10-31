"""Drive the shipped oneshot adapter through the recovered closed route.

Cleartext goes through necko_socket_* / necko_exchange_*. HTTP/3, when
libnecko_quic is present, is the second request after a response Alt-Svc.
"""

import importlib.util
import os
import secrets
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
_SOURCE = _ROOT / "src" / "resource_network"


def _load(fullname, filename):
    """Load a maintenance-tree module under the name the oneshot imports."""
    path = _SOURCE / filename
    spec = importlib.util.spec_from_file_location(fullname, path)
    module = importlib.util.module_from_spec(spec)
    parent_name, _, child = fullname.rpartition(".")
    module.__package__ = parent_name or fullname
    sys.modules[fullname] = module
    spec.loader.exec_module(module)
    if parent_name:
        setattr(sys.modules[parent_name], child, module)
    return module


_necko = _load("necko", "necko_pkg_init.py")
_necko.__path__ = [str(_SOURCE)]
_load("necko.client", "necko_client.py")
_load("necko.closed_route", "necko_closed_route.py")
_load("necko.adapters", "necko_adapters.py")
_load("ruyijs_necko", "necko_ruyi_init.py")

from requests import Request

from necko.adapters import HTTPAdapter
import necko.adapters
import necko.closed_route
from ruyijs_necko import RequestControl, open_response


def _relative(module):
    return Path(module.__file__).resolve().relative_to(_ROOT).as_posix()


def _serve(token: str):
    body = token.encode("ascii")

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            payload = body
            raw = (
                b"HTTP/1.1 200 OK\r\n"
                + b"Content-Length: " + str(len(payload)).encode("ascii") + b"\r\n"
                + b"X-Token: " + payload + b"\r\n"
                + b"Connection: close\r\n\r\n"
                + payload
            )
            self.connection.sendall(raw)

        def log_message(self, fmt, *args):
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def _unit_present():
    directory = _ROOT / "dist" / "necko_tls"
    library = directory / ("libnecko_quic.dylib" if sys.platform == "darwin" else "necko_quic.dll")
    server = directory / ("neqo-server" if sys.platform != "win32" else "neqo-server.exe")
    return library.is_file() and server.is_file()


def _donor_bin():
    directory = _ROOT.parent / "firefox-159" / "obj-necko" / "dist" / "bin"
    if (directory / "certutil").is_file() and (directory / "pk12util").is_file():
        return directory
    return None


def _leaf(ca_key, ca_cert, name, issuer):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    import datetime
    import ipaddress

    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, name)])
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject if issuer is None else issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1))
        .not_valid_after(datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=2))
        .add_extension(
            x509.SubjectAlternativeName([x509.IPAddress(ipaddress.IPv4Address("127.0.0.1"))]),
            critical=False,
        )
        .add_extension(
            x509.SubjectKeyIdentifier.from_public_key(key.public_key()),
            critical=False,
        )
    )
    if ca_cert is None:
        builder = builder.add_extension(
            x509.BasicConstraints(ca=True, path_length=None), critical=True
        )
        builder = builder.add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=True,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        signed = builder.sign(key, hashes.SHA256())
        return key, signed
    builder = builder.add_extension(
        x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),
        critical=False,
    )
    builder = builder.add_extension(
        x509.BasicConstraints(ca=False, path_length=None), critical=True
    )
    builder = builder.add_extension(
        x509.KeyUsage(
            digital_signature=True,
            content_commitment=False,
            key_encipherment=False,
            data_encipherment=False,
            key_agreement=False,
            key_cert_sign=False,
            crl_sign=False,
            encipher_only=False,
            decipher_only=False,
        ),
        critical=True,
    )
    builder = builder.add_extension(
        x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]),
        critical=False,
    )
    signed = builder.sign(ca_key, hashes.SHA256())
    return key, signed


def _write_p12(path, key, cert, extra):
    from cryptography.hazmat.primitives import serialization

    key_path = path.with_suffix(".key.pem")
    cert_path = path.with_suffix(".cert.pem")
    key_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption(),
        )
    )
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    command = [
        "openssl",
        "pkcs12",
        "-export",
        "-inkey",
        str(key_path),
        "-in",
        str(cert_path),
        "-name",
        "key",
        "-keypbe",
        "NONE",
        "-certpbe",
        "NONE",
        "-passout",
        "pass:",
        "-out",
        str(path),
    ]
    if extra:
        ca_path = path.with_suffix(".ca.pem")
        ca_path.write_bytes(
            b"".join(item.public_bytes(serialization.Encoding.PEM) for item in extra)
        )
        command[7:7] = ["-certfile", str(ca_path)]
    _tool(command)


def _tool(args):
    completed = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        lines = completed.stderr.decode("utf-8", "replace").strip().splitlines()
        line = lines[-1][:180] if lines else ""
        print(f"tool_fail={Path(args[0]).name} code={completed.returncode} line={line}")
        raise subprocess.CalledProcessError(completed.returncode, args)


def _nss_db(directory, p12, donor):
    directory.mkdir()
    _tool([str(donor / "certutil"), "-N", "-d", f"sql:{directory}", "--empty-password"])
    _tool([str(donor / "pk12util"), "-i", str(p12), "-d", f"sql:{directory}", "-W", ""])


def _https(token: bytes, alt_port: int, cert_path, key_path):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            raw = (
                b"HTTP/1.1 200 OK\r\n"
                + b"Content-Length: " + str(len(token)).encode("ascii") + b"\r\n"
                + b"Alt-Svc: h3=\"127.0.0.1:" + str(alt_port).encode("ascii") + b"\"\r\n"
                + b"Connection: close\r\n\r\n"
                + token
            )
            self.connection.sendall(raw)

        def log_message(self, fmt, *args):
            return

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert_path, key_path)
    context.set_alpn_protocols(["http/1.1"])
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)

    def wrap(sock, address):
        return context.wrap_socket(sock, server_side=True), address

    server.get_request = lambda: wrap(*ThreadingHTTPServer.get_request(server))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def _udp_port():
    import socket

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def _spawn_neqo(directory, port, donor):
    binary = _ROOT / "dist" / "necko_tls" / "neqo-server"
    env = os.environ.copy()
    env["DYLD_LIBRARY_PATH"] = str(donor)
    log_path = directory.parent / f"neqo-{port}.err"
    log = open(log_path, "wb")
    process = subprocess.Popen(
        [str(binary), "-d", str(directory), "-k", "key", f"127.0.0.1:{port}"],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=log,
    )
    process.stderr_log = log
    time.sleep(0.5)
    code = process.poll()
    log.flush()
    print(f"neqo_alive={int(code is None)} code={-1 if code is None else code} port={port}")
    if code is not None:
        lines = log_path.read_text(encoding="utf-8", errors="replace").strip().splitlines()
        line = lines[-1][:180] if lines else ""
        print(f"neqo_line={line}")
    return process


def _http3_proof():
    donor = _donor_bin()
    if donor is None:
        print("http3_unit=0")
        print("http3_blocked=donor certutil missing")
        return False
    from cryptography.hazmat.primitives import serialization

    ca_key, ca_cert = _leaf(None, None, "necko-local-ca", None)
    good_key, good_cert = _leaf(ca_key, ca_cert, "127.0.0.1", ca_cert.subject)
    bad_key, bad_cert = _leaf(None, None, "127.0.0.1", None)
    work = Path(tempfile.mkdtemp(prefix="necko-h3-"))
    ca_path = work / "ca.pem"
    cert_path = work / "good.pem"
    key_path = work / "good.key"
    ca_path.write_bytes(ca_cert.public_bytes(serialization.Encoding.PEM))
    cert_path.write_bytes(good_cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(
        good_key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption(),
        )
    )
    good_p12 = work / "good.p12"
    bad_p12 = work / "bad.p12"
    _write_p12(good_p12, good_key, good_cert, [ca_cert])
    _write_p12(bad_p12, bad_key, bad_cert, None)
    good_db = work / "good-db"
    bad_db = work / "bad-db"
    _nss_db(good_db, good_p12, donor)
    _nss_db(bad_db, bad_p12, donor)
    bad_port = _udp_port()
    good_port = _udp_port()
    bad_server = _spawn_neqo(bad_db, bad_port, donor)
    good_server = _spawn_neqo(good_db, good_port, donor)
    os.environ["NECKO_EXTRA_CA"] = str(ca_path)
    bad_https = _https(b"bad-origin", bad_port, cert_path, key_path)
    good_body = secrets.token_hex(16).encode("ascii")
    good_https = _https(good_body, good_port, cert_path, key_path)
    adapter = HTTPAdapter()
    try:
        bad_url = f"https://{bad_https.server_address[0]}:{bad_https.server_address[1]}/first"
        first = open_response(
            adapter,
            Request("GET", bad_url).prepare(),
            RequestControl(8, max_attempts=1),
            timeout=(8, 8),
        )
        print(f"http3_bad_first_version={first.version}")
        print(f"http3_bad_first_status={first.status}")
        failed = 0
        try:
            again = open_response(
                adapter,
                Request("GET", bad_url).prepare(),
                RequestControl(8, max_attempts=1),
                timeout=(8, 8),
            )
            print(f"http3_bad_second_status={again.status}")
        except Exception as error:
            failed = 1
            text = str(error)
            print(f"http3_bad_second_error={text}")
            print(f"http3_bad_has_804b0033={int('804b0033' in text)}")
        print(f"http3_bad_rejected={failed}")
        good_url = f"https://{good_https.server_address[0]}:{good_https.server_address[1]}/64"
        primed = open_response(
            adapter,
            Request("GET", good_url).prepare(),
            RequestControl(8, max_attempts=1),
            timeout=(8, 8),
        )
        print(f"http3_good_first_version={primed.version}")
        second = open_response(
            adapter,
            Request("GET", good_url).prepare(),
            RequestControl(8, max_attempts=1),
            timeout=(8, 8),
        )
        received = b"".join(second)
        expected = bytes(64)
        print(f"http3_good_second_status={second.status}")
        print(f"http3_good_second_version={second.version}")
        print(f"http3_good_length={len(received)}")
        print(f"http3_good_match={int(received == expected)}")
        print(f"http3_has_804b0033={int('804b0033' in received.decode('ascii', 'replace'))}")
        return (
            failed == 1
            and primed.version == 11
            and second.version == 30
            and second.status == 200
            and received == expected
        )
    finally:
        adapter.close()
        bad_https.shutdown()
        good_https.shutdown()
        for process in (bad_server, good_server):
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
            log = getattr(process, "stderr_log", None)
            if log is not None and not log.closed:
                log.close()


def main():
    os.environ.pop("RUYIJS_HTTP_PROXY", None)
    adapters_file = Path(necko.adapters.__file__).resolve()
    closed_file = Path(necko.closed_route.__file__).resolve()
    print("necko.adapters.__file__=" + _relative(necko.adapters))
    print("necko.closed_route.__file__=" + _relative(necko.closed_route))
    if adapters_file != (_SOURCE / "necko_adapters.py").resolve():
        raise SystemExit(1)
    if closed_file != (_SOURCE / "necko_closed_route.py").resolve():
        raise SystemExit(1)
    token = secrets.token_hex(16)
    server = _serve(token)
    host, port = server.server_address
    adapter = HTTPAdapter()
    try:
        control = RequestControl(5, max_attempts=1)
        prepared = Request("GET", f"http://{host}:{port}/closed").prepare()
        reader = open_response(adapter, prepared, control, timeout=(5, 5))
        received = b"".join(reader)
        print(f"cleartext_status={reader.status}")
        print(f"cleartext_version={reader.version}")
        print(f"cleartext_match={int(received == token.encode('ascii'))}")
        print(f"cleartext_has_804b0033={int('804b0033' in received.decode('ascii', 'replace'))}")
    finally:
        adapter.close()
        server.shutdown()
    if received != token.encode("ascii") or reader.version != 11:
        raise SystemExit(1)
    if not _unit_present():
        print("http3_unit=0")
        return
    sys.path.insert(0, str(_ROOT / "native" / "necko_tls"))
    _load("necko.core_client", "necko_core_client.py")
    print("http3_unit=1")
    if not _http3_proof():
        raise SystemExit(1)


if __name__ == "__main__":
    main()
