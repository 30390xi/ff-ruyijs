//! JSON bridge in front of the Firefox 159 Neqo 0.32.0 crates.
//!
//! Python owns the UDP socket and certificate decision. This library does not
//! start a socket process and does not link neqo_glue. Profile numbers are
//! read from the JSON object the caller already built.

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::net::SocketAddr;
use std::os::raw::{c_char, c_int};
use std::rc::Rc;
use std::time::{Duration, Instant};

use neqo_common::event::Provider;
use neqo_common::{Datagram, Header, Tos};
use neqo_http3::{Http3Client, Http3ClientEvent, Http3Parameters, Http3State, Priority};
use neqo_transport::{
    AppError, CloseReason, ConnectionParameters, Output, RandomConnectionIdGenerator, StreamId,
    StreamType, Version,
};
use nss::AuthenticationStatus;
use serde_json::{Value, json};

const VERSION_OK: &str = "neqo-0.32.0/nss-3.131";
const COMMAND_LIMIT: usize = 0x200001;
const CANCEL_DEFAULT: AppError = 0x10c;

type DecoderFn = unsafe extern "C" fn(u16, *const u8, usize, *mut u8, usize) -> c_int;

struct Session {
    client: Http3Client,
    fd: Option<i32>,
    local: SocketAddr,
    remote: SocketAddr,
    profile: String,
}

thread_local! {
    static SESSIONS: RefCell<HashMap<u64, Session>> = RefCell::new(HashMap::new());
    static NEXT_HANDLE: RefCell<u64> = const { RefCell::new(1) };
    static LAST_JSON: RefCell<CString> = RefCell::new(CString::new("{}").unwrap());
    static DECODER: RefCell<Option<DecoderFn>> = const { RefCell::new(None) };
    static NSS_OK: RefCell<bool> = const { RefCell::new(false) };
}

unsafe extern "C" {
    fn NSS_GetVersion() -> *const c_char;
}

fn nss_text() -> String {
    unsafe {
        if NSS_GetVersion().is_null() {
            return String::new();
        }
        CStr::from_ptr(NSS_GetVersion()).to_string_lossy().into_owned()
    }
}

fn version_label() -> &'static str {
    let ok = NSS_OK.with(|slot| *slot.borrow());
    if ok {
        VERSION_OK
    } else if nss_text().contains("3.131") {
        VERSION_OK
    } else {
        "neqo-0.32.0/nss-mismatch"
    }
}

fn store_reply(text: String) -> *const c_char {
    let owned = CString::new(text).unwrap_or_else(|_| CString::new("{\"error\":\"encode\"}").unwrap());
    LAST_JSON.with(|slot| {
        *slot.borrow_mut() = owned;
        slot.borrow().as_ptr()
    })
}

fn reply(value: Value) -> *const c_char {
    let text = serde_json::to_string(&value).unwrap_or_else(|_| "{\"error\":\"encode\"}".to_string());
    store_reply(text)
}

fn fail(message: &str) -> *const c_char {
    reply(json!({"error": message}))
}

fn parse_addr(text: &str) -> Result<SocketAddr, ()> {
    text.parse::<SocketAddr>().map_err(|_| ())
}

fn json_u64(value: &Value, key: &str) -> Option<u64> {
    value.get(key).and_then(Value::as_u64)
}

fn json_bool(value: &Value, key: &str) -> Option<bool> {
    value.get(key).and_then(Value::as_bool)
}

fn apply_profile(profile: &Value) -> (ConnectionParameters, bool) {
    let mut params = ConnectionParameters::default();
    if let Some(size) = json_u64(profile, "datagram_size") {
        params = params.datagram_size(size);
    }
    if let Some(enabled) = json_bool(profile, "grease_quic_bit") {
        params = params.grease(enabled);
    }
    if let Some(enabled) = json_bool(profile, "disable_migration") {
        params = params.disable_migration(enabled);
    }
    if let Some(enabled) = json_bool(profile, "sni_slicing") {
        params = params.sni_slicing(enabled);
    }
    if let Some(enabled) = json_bool(profile, "reliable_stream_reset") {
        params = params.reliable_stream_reset(enabled);
    }
    if let Some(streams) = json_u64(profile, "max_streams") {
        params = params
            .max_streams(StreamType::BiDi, streams)
            .max_streams(StreamType::UniDi, streams);
    }
    if json_bool(profile, "version2") == Some(true) {
        params = params.versions(
            Version::Version1,
            vec![Version::Version1, Version::Version2],
        );
    }
    let datagrams = json_bool(profile, "h3_datagram").unwrap_or(true);
    (params, datagrams)
}

fn stream_id(value: u64) -> StreamId {
    StreamId::new(value)
}

fn stream_number(stream: StreamId) -> u64 {
    stream.as_u64()
}

fn b64_encode(data: &[u8]) -> String {
    const TABLE: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    let mut index = 0;
    while index + 3 <= data.len() {
        let block = u32::from(data[index]) << 16 | u32::from(data[index + 1]) << 8 | u32::from(data[index + 2]);
        out.push(TABLE[((block >> 18) & 63) as usize] as char);
        out.push(TABLE[((block >> 12) & 63) as usize] as char);
        out.push(TABLE[((block >> 6) & 63) as usize] as char);
        out.push(TABLE[(block & 63) as usize] as char);
        index += 3;
    }
    let rest = data.len() - index;
    if rest == 1 {
        let block = u32::from(data[index]) << 16;
        out.push(TABLE[((block >> 18) & 63) as usize] as char);
        out.push(TABLE[((block >> 12) & 63) as usize] as char);
        out.push('=');
        out.push('=');
    } else if rest == 2 {
        let block = u32::from(data[index]) << 16 | u32::from(data[index + 1]) << 8;
        out.push(TABLE[((block >> 18) & 63) as usize] as char);
        out.push(TABLE[((block >> 12) & 63) as usize] as char);
        out.push(TABLE[((block >> 6) & 63) as usize] as char);
        out.push('=');
    }
    out
}

fn b64_value(ch: u8) -> Option<u8> {
    match ch {
        b'A'..=b'Z' => Some(ch - b'A'),
        b'a'..=b'z' => Some(ch - b'a' + 26),
        b'0'..=b'9' => Some(ch - b'0' + 52),
        b'+' => Some(62),
        b'/' => Some(63),
        _ => None,
    }
}

fn b64_decode(text: &str) -> Result<Vec<u8>, ()> {
    let bytes = text.as_bytes();
    if bytes.len() % 4 != 0 {
        return Err(());
    }
    let mut out = Vec::new();
    for chunk in bytes.chunks(4) {
        let pad = chunk.iter().filter(|byte| **byte == b'=').count();
        if pad > 2 || chunk[..4 - pad].contains(&b'=') {
            return Err(());
        }
        let mut block = 0u32;
        for (shift, byte) in chunk.iter().take(4 - pad).enumerate() {
            let value = b64_value(*byte).ok_or(())?;
            block |= u32::from(value) << (18 - 6 * shift);
        }
        out.push((block >> 16) as u8);
        if pad < 2 {
            out.push((block >> 8) as u8);
        }
        if pad == 0 {
            out.push(block as u8);
        }
    }
    Ok(out)
}

fn certificates(client: &Http3Client) -> Vec<Value> {
    let Some(info) = client.peer_certificate() else {
        return Vec::new();
    };
    info.iter().map(|der| Value::String(b64_encode(der))).collect()
}

fn close_fields(reason: &CloseReason) -> (bool, String) {
    match reason {
        CloseReason::Transport(error) => (true, error.to_string()),
        CloseReason::Application(code) => (false, code.to_string()),
    }
}

fn push_event(events: &mut Vec<Value>, event: Http3ClientEvent) {
    match event {
        Http3ClientEvent::AuthenticationNeeded => {}
        Http3ClientEvent::EchFallbackAuthenticationNeeded { .. } => {
            events.push(json!({"type": "ech_retry"}));
        }
        Http3ClientEvent::ResumptionToken(_) => {
            events.push(json!({"type": "token", "data": "", "expires_in": 0}));
        }
        Http3ClientEvent::StateChange(Http3State::Connected) => {
            events.push(json!({"type": "state", "state": "connected"}));
        }
        Http3ClientEvent::StateChange(Http3State::GoingAway(stream)) => {
            events.push(json!({"type": "state", "state": "going_away", "last": stream_number(stream)}));
        }
        Http3ClientEvent::StateChange(Http3State::Closing(reason) | Http3State::Closed(reason)) => {
            let (protocol_error, text) = close_fields(&reason);
            events.push(json!({
                "type": "state",
                "state": "closed",
                "protocol_error": protocol_error,
                "reason": text,
            }));
        }
        Http3ClientEvent::HeaderReady { stream_id, headers, interim, fin } => {
            let fields: Vec<Value> = headers
                .iter()
                .map(|header| json!([header.name().to_string(), b64_encode(header.value())]))
                .collect();
            events.push(json!({
                "type": "headers",
                "stream": stream_number(stream_id),
                "headers": fields,
                "interim": interim,
                "fin": fin,
            }));
        }
        Http3ClientEvent::DataReadable { stream_id } => {
            events.push(json!({"type": "readable", "stream": stream_number(stream_id)}));
        }
        Http3ClientEvent::Reset { stream_id, error, .. } => {
            events.push(json!({"type": "reset", "stream": stream_number(stream_id), "error": error}));
        }
        Http3ClientEvent::StopSending { stream_id, error } => {
            events.push(json!({"type": "stop_sending", "stream": stream_number(stream_id), "error": error}));
        }
        _ => {}
    }
}

fn take_events(session: &mut Session) -> Value {
    let mut events = Vec::new();
    let mut saw_auth = false;
    for _ in 0..64 {
        if !session.client.has_events() {
            break;
        }
        let Some(event) = session.client.next_event() else {
            break;
        };
        if matches!(event, Http3ClientEvent::AuthenticationNeeded) {
            saw_auth = true;
        }
        push_event(&mut events, event);
        if events.len() >= 32 {
            break;
        }
    }
    if saw_auth {
        let certs = certificates(&session.client);
        events.insert(0, json!({"type": "auth", "certificates": certs}));
    }
    let more = session.client.has_events();
    json!({"events": events, "more": more})
}

fn recv_payload(fd: i32) -> Result<Option<Vec<u8>>, String> {
    let mut buffer = vec![0u8; 65535];
    let size = unsafe { libc::recv(fd, buffer.as_mut_ptr().cast(), buffer.len(), 0) };
    if size < 0 {
        let error = std::io::Error::last_os_error();
        if error.raw_os_error() == Some(libc::EAGAIN) || error.raw_os_error() == Some(libc::EWOULDBLOCK) {
            return Ok(None);
        }
        return Err(error.to_string());
    }
    if size == 0 {
        return Ok(None);
    }
    buffer.truncate(size as usize);
    Ok(Some(buffer))
}

fn send_payload(fd: i32, data: &[u8]) -> Result<(), String> {
    let mut sent = 0;
    while sent < data.len() {
        let size = unsafe {
            libc::send(fd, data[sent..].as_ptr().cast(), data.len() - sent, 0)
        };
        if size < 0 {
            let error = std::io::Error::last_os_error();
            if error.raw_os_error() == Some(libc::EAGAIN) || error.raw_os_error() == Some(libc::EWOULDBLOCK) {
                return Err("UDP send would block".to_string());
            }
            return Err(error.to_string());
        }
        if size == 0 {
            return Err("UDP send returned no bytes".to_string());
        }
        sent += size as usize;
    }
    Ok(())
}

fn pump(session: &mut Session) -> Result<f64, String> {
    let fd = session.fd.ok_or("HTTP/3 socket is not attached")?;
    let now = Instant::now();
    loop {
        match recv_payload(fd)? {
            Some(payload) => {
                let datagram = Datagram::new(session.remote, session.local, Tos::from(0u8), payload);
                session.client.process_input(datagram, now);
            }
            None => break,
        }
    }
    let mut delay = 0.05_f64;
    for _ in 0..64 {
        match session.client.process_output(now) {
            Output::Datagram(datagram) => send_payload(fd, &datagram)?,
            Output::Callback(duration) => {
                delay = duration.as_secs_f64();
                break;
            }
            Output::None => {
                delay = 0.05;
                break;
            }
        }
    }
    Ok(delay)
}

fn command_new(body: &Value) -> Result<Value, String> {
    let host = body.get("host").and_then(Value::as_str).unwrap_or("");
    if host.is_empty() {
        return Err("HTTP/3 new requires a host".to_string());
    }
    let local = parse_addr(body.get("local").and_then(Value::as_str).unwrap_or(""))
        .map_err(|_| "HTTP/3 local address is invalid".to_string())?;
    let remote = parse_addr(body.get("remote").and_then(Value::as_str).unwrap_or(""))
        .map_err(|_| "HTTP/3 remote address is invalid".to_string())?;
    let profile = body.get("profile").cloned().unwrap_or_else(|| json!({}));
    let (params, h3_datagram) = apply_profile(&profile);
    let webtransport = body.get("webtransport").and_then(Value::as_bool).unwrap_or(false);
    let http3 = Http3Parameters::default()
        .connection_parameters(params)
        .http3_datagram(h3_datagram)
        .webtransport(webtransport);
    let client = Http3Client::new(
        host,
        Rc::new(RefCell::new(RandomConnectionIdGenerator::new(8))),
        local,
        remote,
        http3,
        Instant::now(),
    )
    .map_err(|error| error.to_string())?;
    let handle = NEXT_HANDLE.with(|slot| {
        let mut next = slot.borrow_mut();
        let handle = *next;
        *next = next.saturating_add(1);
        handle
    });
    SESSIONS.with(|slot| {
        slot.borrow_mut().insert(
            handle,
            Session {
                client,
                fd: None,
                local,
                remote,
                profile: profile.to_string(),
            },
        );
    });
    Ok(json!({"handle": handle}))
}

fn with_session<T>(handle: u64, body: impl FnOnce(&mut Session) -> Result<T, String>) -> Result<T, String> {
    SESSIONS.with(|slot| {
        let mut sessions = slot.borrow_mut();
        let session = sessions.get_mut(&handle).ok_or("unknown HTTP/3 handle")?;
        body(session)
    })
}

fn dispatch(handle: u64, body: &Value) -> Result<Value, String> {
    let op = body.get("op").and_then(Value::as_str).unwrap_or("");
    match op {
        "new" => command_new(body),
        "attach" => {
            let fd = body.get("socket").and_then(Value::as_i64).ok_or("attach requires a socket")?;
            with_session(handle, |session| {
                let fd = i32::try_from(fd).map_err(|_| "attach socket does not fit".to_string())?;
                let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
                if flags >= 0 {
                    unsafe { libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK); }
                }
                session.fd = Some(fd);
                let _ = &session.profile;
                Ok(json!({}))
            })
        }
        "pump" => with_session(handle, |session| Ok(json!({"delay": pump(session)?}))),
        "events" => with_session(handle, |session| Ok(take_events(session))),
        "auth" => {
            let accepted = body.get("accepted").and_then(Value::as_bool).unwrap_or(false);
            with_session(handle, |session| {
                let status = if accepted {
                    AuthenticationStatus::Ok
                } else {
                    AuthenticationStatus::CertUntrusted
                };
                session.client.authenticated(status, Instant::now());
                Ok(json!({}))
            })
        }
        "info" => with_session(handle, |session| {
            Ok(json!({"tls": null, "certificates": certificates(&session.client)}))
        }),
        "fetch" => with_session(handle, |session| {
            let method = body.get("method").and_then(Value::as_str).unwrap_or("GET");
            let authority = body.get("authority").and_then(Value::as_str).unwrap_or("");
            let path = body.get("path").and_then(Value::as_str).unwrap_or("/");
            let urgency = body.get("urgency").and_then(Value::as_u64).unwrap_or(4).min(7) as u8;
            let incremental = body.get("incremental").and_then(Value::as_bool).unwrap_or(false);
            let mut headers = Vec::new();
            if let Some(rows) = body.get("headers").and_then(Value::as_array) {
                for row in rows {
                    let pair = row.as_array().ok_or("fetch header is not a pair")?;
                    let name = pair.first().and_then(Value::as_str).unwrap_or("");
                    let encoded = pair.get(1).and_then(Value::as_str).unwrap_or("");
                    let value = b64_decode(encoded).map_err(|_| "fetch header value is not base64".to_string())?;
                    headers.push(Header::new(name, value));
                }
            }
            let stream = session
                .client
                .fetch(
                    Instant::now(),
                    method,
                    ("https", authority, path),
                    &headers,
                    Priority::new(urgency, incremental),
                )
                .map_err(|error| error.to_string())?;
            Ok(json!({"stream": stream_number(stream)}))
        }),
        "read" => with_session(handle, |session| {
            let stream = stream_id(body.get("stream").and_then(Value::as_u64).ok_or("read requires a stream")?);
            let size = body.get("size").and_then(Value::as_u64).unwrap_or(65536).clamp(1, 65536) as usize;
            let mut buffer = vec![0u8; size];
            let (count, fin) = session
                .client
                .read_data(Instant::now(), stream, &mut buffer)
                .map_err(|error| error.to_string())?;
            Ok(json!({"data": b64_encode(&buffer[..count]), "fin": fin}))
        }),
        "write" => with_session(handle, |session| {
            let stream = stream_id(body.get("stream").and_then(Value::as_u64).ok_or("write requires a stream")?);
            let encoded = body.get("data").and_then(Value::as_str).unwrap_or("");
            let data = b64_decode(encoded).map_err(|_| "write data is not base64".to_string())?;
            let count = session
                .client
                .send_data(stream, &data, Instant::now())
                .map_err(|error| error.to_string())?;
            Ok(json!({"count": count}))
        }),
        "finish" => with_session(handle, |session| {
            let stream = stream_id(body.get("stream").and_then(Value::as_u64).ok_or("finish requires a stream")?);
            session
                .client
                .stream_close_send(stream, Instant::now())
                .map_err(|error| error.to_string())?;
            Ok(json!({}))
        }),
        "cancel" => with_session(handle, |session| {
            let stream = stream_id(body.get("stream").and_then(Value::as_u64).ok_or("cancel requires a stream")?);
            let code = body.get("error").and_then(Value::as_u64).unwrap_or(CANCEL_DEFAULT);
            session
                .client
                .cancel_fetch(stream, code)
                .map_err(|error| error.to_string())?;
            Ok(json!({}))
        }),
        "close" => {
            SESSIONS.with(|slot| {
                slot.borrow_mut().remove(&handle);
            });
            Ok(json!({}))
        }
        _ => Err(format!("unknown HTTP/3 command {op}")),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn necko_quic_init() -> c_int {
    // Same NoDB startup the cleartext/TLS library uses. If that library
    // already called NSS_NoDB_Init, nss::init records the external state.
    if nss::init().is_err() {
        NSS_OK.with(|slot| *slot.borrow_mut() = false);
        return 1;
    }
    let ok = nss_text().contains("3.131");
    NSS_OK.with(|slot| *slot.borrow_mut() = ok);
    if ok { 0 } else { 1 }
}

#[unsafe(no_mangle)]
pub extern "C" fn necko_quic_version() -> *const c_char {
    store_reply(version_label().to_string())
}

#[unsafe(no_mangle)]
pub extern "C" fn necko_quic_set_decoder(callback: Option<DecoderFn>) {
    DECODER.with(|slot| *slot.borrow_mut() = callback);
    let _kept = DECODER.with(|slot| slot.borrow().is_some());
    let _ = _kept;
}

#[unsafe(no_mangle)]
pub extern "C" fn necko_quic_call(handle: u64, data: *const c_char, length: usize) -> *const c_char {
    if data.is_null() || length == 0 || length >= COMMAND_LIMIT {
        return fail("Invalid command buffer");
    }
    let bytes = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), length) };
    let text = match std::str::from_utf8(bytes) {
        Ok(text) => text,
        Err(_) => return fail("Invalid command buffer"),
    };
    let parsed = match serde_json::from_str::<Value>(text) {
        Ok(value) => value,
        Err(error) => return fail(&error.to_string()),
    };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| dispatch(handle, &parsed)));
    match result {
        Ok(Ok(value)) => reply(value),
        Ok(Err(message)) => fail(&message),
        Err(_) => fail("HTTP/3 command panicked"),
    }
}

#[allow(dead_code)]
fn engine_linked() -> usize {
    std::mem::size_of::<Http3Client>() + std::mem::size_of::<Duration>()
}
