# Notes:
#   * Keep only ONE reader of uart: uart_reader_loop().
#   * Writer uses awrite() only; no toggling.
#   * AT replies are matched by tokens (OK, >, ERROR). You can expand this per command.
#
from machine import UART, Pin
import uasyncio as asyncio
import ujson
import gc
from queue import Queue
import ubinascii, uhashlib, os
import json
import re

# SHA256 simple - matches the C implementation in main.c
def sha256_simple(input_data):
    """
    Simplified *custom checksum* implementation (matches C version).
    Returns Base64-encoded 32-byte hash using ubinascii.
    
    NOTE: This is NOT the standard cryptographic SHA256 algorithm.
    """
    if isinstance(input_data, str):
        # MicroPython uses 'utf8' or 'utf-8'
        input_data = input_data.encode('utf8')
    
    input_len = len(input_data)
    # Use standard uctypes or struct module for C-like 32-bit operations 
    # or ensure Python's default large integers don't cause issues.
    # & 0xFFFFFFFF ensures 32-bit wrap-around behavior.
    sum_val = 0x5A5A5A5A & 0xFFFFFFFF 
    
    # --- Custom Checksum Logic ---
    
    # Mix all input bytes
    for i in range(input_len):
        sum_val = (((sum_val << 5) + sum_val) + input_data[i]) & 0xFFFFFFFF
        sum_val = (sum_val ^ (sum_val >> 16)) & 0xFFFFFFFF
    
    # Generate 32 bytes of "hash" from the checksum
    hash_bytes = bytearray(32)
    for i in range(32):
        hash_bytes[i] = (sum_val >> ((i % 4) * 8)) & 0xFF
        if i % 4 == 3:
            # Note: This line uses 'input_data[i % input_len]' which can cause an 
            # IndexError if i > input_len, but the custom logic seems to rely on this.
            sum_val = (((sum_val << 7) ^ (sum_val >> 11)) + input_data[i % input_len]) & 0xFFFFFFFF
            
    # --- Base64 Encoding Fix ---
    
    # Base64 encode the hash using ubinascii.b2a_base64
    # ubinascii.b2a_base64 returns bytes which usually includes a trailing newline.
    b64_encoded_bytes = ubinascii.b2a_base64(hash_bytes)
    
    # Decode the resulting bytes to a string and strip the trailing newline (\n)
    return b64_encoded_bytes.decode('utf8').strip()

# Example Usage:
# print(sha256_simple("hello world"))
SSID = 'Cudy24G'         # <-- change if needed
PASSWORD = 'ZAnne19991214'
PORT = '8080'
UART_ID = 0
BAUD = 115200
TX_PIN = None  # Use default pins for UART(0) on your board
RX_PIN = None
_in_hash_md5 = None
fout = None

# ----------------- UART + Streams -----------------
uart = UART(UART_ID, BAUD) if (TX_PIN is None or RX_PIN is None) else UART(UART_ID, BAUD, tx=TX_PIN, rx=RX_PIN)
sreader = asyncio.StreamReader(uart)
swriter = asyncio.StreamWriter(uart, {})
error_q = []
success_q = []

# ----------------- Globals -----------------
# AT waiter registry: token -> Future
_pending = {}  # e.g. {'OK': Future, '>': Future, 'ERROR': Future}

# Queues for your app
recv_q = Queue()  # inbound app payloads (e.g. from +IPD)
send_q = Queue()  # outbound app payloads (raw TCP writes)
_at_lock = asyncio.Lock()
uart = UART(1, 115200)
uart.read()

async def send_at_new(cmd, expect=('OK',), timeout_ms=5000, verbose=False):
    if verbose: print("AT>>", cmd)
    uart.write(cmd + "\r\n")
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    buf = b""
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        if uart.any():
            buf += uart.read() or b""
            if verbose and b"\r\n" in buf:
                for line in buf.split(b"\r\n"):
                    if line:
                        print("<", line)
            if any(tok.encode() in buf for tok in expect):
                return True
            if b"ERROR" in buf or b"FAIL" in buf:
                return False
        await asyncio.sleep_ms(5)
    return False

# Switch these to True if you choose transparent mode
TRANSPARENT_MODE  = [False]
TRANSPARENT_READY = [False]

async def tcp_connect(host, port, *, transparent=False):
    # escape any previous data mode
    try: uart.write(b'+++')
    except: pass
    await asyncio.sleep_ms(1200); _ = uart.read()

    ok  = await send_at("AT")
    ok &= await send_at("ATE0")
    ok &= await send_at("AT+CWMODE=1")
    ok &= await send_at('AT+CWJAP="SSID","PASS"', expect=('OK','ALREADY CONNECTED','WIFI CONNECTED'), timeout_ms=20000)
    ok &= await send_at("AT+CIFSR")
    ok &= await send_at("AT+CIPMUX=0")  # single link for simplicity
    ok &= await send_at('AT+CIPSTART="TCP","%s",%d' % (host, port), expect=('OK','ALREADY CONNECTED'), timeout_ms=8000, verbose=True)
    if not ok: return False

    if transparent:
        ok &= await send_at("AT+CIPMODE=1")
        ok &= await send_at("AT+CIPSEND", expect=('>',), timeout_ms=3000)
        TRANSPARENT_MODE[0] = TRANSPARENT_READY[0] = bool(ok)
    else:
        TRANSPARENT_MODE[0] = TRANSPARENT_READY[0] = False
    return ok

async def tcp_send(buf: bytes):
    if TRANSPARENT_MODE[0] and TRANSPARENT_READY[0]:
        uart.write(buf)
        return True
    # normal mode: must request prompt with exact length
    ok = await send_at('AT+CIPSEND=%d' % len(buf), expect=('>',), timeout_ms=3000)
    if not ok: return False
    uart.write(buf)
    return True

# Minimal CONNACK reader — works in both normal and transparent modes.
# In transparent, bytes arrive as-is; in normal mode they are wrapped in +IPD.

def _extract_ipd_frames(accum: bytearray):
    """Return list of payloads from +IPD frames; mutate 'accum'."""
    out = []
    while True:
        p = accum.find(b'+IPD,')
        if p < 0: break
        c = accum.find(b':', p)
        if c < 0: break
        header = accum[p:c]
        parts = header.split(b',')
        try:
            length = int(parts[-1])
        except:
            del accum[:p+5]; continue
        if len(accum) < c+1+length: break
        payload = bytes(accum[c+1:c+1+length])
        del accum[:c+1+length]
        out.append(payload)
    return out

def _maybe_set(token: str):
    evt = _pending.get(token)
    
    if evt:
        try:
            evt.set()
        except Exception:
            pass

async def send_at(cmd: str, expect=('OK',), timeout_ms=5000) -> bool:
    """Write an AT command and await one of expected tokens from the reader loop (MicroPython-friendly).
       Uses asyncio.Event in place of Future/create_future.
    """
    async with _at_lock:
        evt = asyncio.Event()
        
        for t in expect:
            _pending[t] = evt
            
        try:
            await swriter.awrite(cmd + '\r\n')
            await asyncio.wait_for(evt.wait(), timeout_ms/1000)
            
            return True
        except asyncio.TimeoutError:
            return False
        finally:
            for t in expect:
                _pending.pop(t, None)

async def wait_token(token: str, timeout_ms=3000) -> bool:
    """Wait for a UART line containing the given token without sending any AT command."""
    evt = asyncio.Event()
    _pending[token] = evt
    try:
        await asyncio.wait_for(evt.wait(), timeout_ms/1000)
        return True
    except asyncio.TimeoutError:
        return False
    finally:
        _pending.pop(token, None)

async def start_tcp_server_static_sta(ssid, pwd,
                                      ip="192.168.1.50",
                                      gw="192.168.1.1",
                                      mask="255.255.255.0",
                                      port=8080):
    # Basic sanity + disable echo
    ok = await send_at('AT', expect=('OK',), timeout_ms=1500)
    ok = ok and await send_at('ATE0', expect=('OK',), timeout_ms=1000)
    
    if not ok:
        print("AT sanity failed");
        return False

    # Station mode & join Wi-Fi
    ok = await send_at('AT+CWMODE=1', expect=('OK',), timeout_ms=1500)
    ok = ok and await send_at('AT+CWJAP="%s","%s"' % (ssid, pwd),
                              expect=('OK','ALREADY CONNECTED','FAIL'),
                              timeout_ms=20000)
    
    if not ok:
        print("CWJAP failed"); return False

    ok = ok and await send_at('AT+CIPSTA="%s","%s","%s"' % (ip, gw, mask),
                              expect=('OK',), timeout_ms=2000)
    
    if not ok:
        print("CIPSTA failed"); return False

    # Confirm IP
    ok = ok and await send_at('AT+CIFSR', expect=(ip, 'OK'), timeout_ms=2000)
    
    if not ok:
        print("CIFSR didn’t show the static IP"); return False

    # Server setup
    ok = ok and await send_at('AT+CIPMUX=1', expect=('OK',), timeout_ms=1000)
    
    if not ok:
        print("CIPMUX failed"); return False

    ok = ok and await send_at('AT+CIPSERVER=1,%d' % port, expect=('OK',), timeout_ms=1500)
    
    if not ok:
        print("CIPSERVER start failed"); return False

    # Note: AT+CIPSERVER? query not supported by all ESP8266 firmware versions
    # Server is already confirmed started by the OK response above
    
    print("TCP server listening on %s:%d" % (ip, port))

    ok &= await send_at('AT+CIPSTART=%d,"TCP","%s",%d' % (4, "192.168.10.174", 1883),
                        expect=('OK','ALREADY CONNECTED'), timeout_ms=8000)
    
    return True

async def start_esp_server(ssid: str, pwd: str, port: str = '8080') -> bool:
    """Bring up ESP8266 and start a multi-connection TCP server on the given port."""
    steps = [
        ('AT', ('OK',)),
        ('AT+CWMODE=3', ('OK',)),
        ('AT+CWJAP="%s","%s"' % (ssid, pwd), ('OK', 'ALREADY CONNECTED', 'FAIL')),
        ('AT+CIPMUX=1', ('OK',)),
        ('AT+CIPSERVER=1,%s' % port, ('OK', 'ERROR')),
        ('AT+CIFSR', ('OK',)),
    ]
    
    for cmd, expect in steps:
        ok = await send_at(cmd, expect=expect, timeout_ms=20000 if 'CWJAP' in cmd else 5000)
        
        if not ok:
            print('Failed step:', cmd)
            return False
        
        await asyncio.sleep_ms(50)  # small pacing
        
    print('ESP8266 TCP server started on port', port)
    return True

def clean_and_load_json(raw):
    # 1. Strip leading/trailing whitespace
    s = raw.strip()

    # 2. Remove common prefixes like +IPD,0,174:
    #    Looks for the first "{" and trims everything before it
    if not s.startswith("{") and "{" in s:
        s = s[s.index("{"):]

    # 3. Remove trailing junk after the last "}"
    if not s.endswith("}") and "}" in s:
        s = s[:s.rindex("}")+1]

    # 4. Debug: show exactly what string will be parsed
    print("Parsing JSON string:", repr(s))

    # 5. Try to parse
    return json.loads(s)

async def handle_json(obj):
    print("JSON received:", obj)
    
async def readline(sreader, limit=1024):
    """Minimal readline() for MicroPython StreamReader.
       Returns bytes up to and including b'\\n' or until limit reached.
       Special handling: if '>' is detected, return immediately to signal prompt."""
    buf = bytearray()
    
    while True:
        ch = await sreader.read(1)
        
        if not ch:
            # EOF or no data
            return bytes(buf)
        
        buf += ch
        
        # Special case: ESP-AT data prompt '>' can appear standalone or after OK
        # Return immediately when we see it to minimize latency
        if ch == b'>':
            return bytes(buf)

        if ch == b'\n' or len(buf) >= limit:
            return bytes(buf)

# inside your UART read loop
def extract_ipd_frames(buf: bytearray):
    frames = []
    while True:
        p = buf.find(b'+IPD,')
        if p < 0: break
        c = buf.find(b':', p)
        if c < 0: break
        header = buf[p:c]                 # e.g. b'+IPD,0,96'
        parts = header.split(b',')
        if len(parts) < 3:                # malformed
            del buf[:p+5]; continue
        link_id = int(parts[1])
        length  = int(parts[2])
        if len(buf) < c+1+length:         # wait for full payload
            break
        payload = bytes(buf[c+1:c+1+length])
        del buf[:c+1+length]
        frames.append((link_id, payload))
    return frames

# when you get a frame:
#for link_id, payload in extract_ipd_frames(rx_buf):
    # Here payload may be newline-terminated JSON your client sent
    #reply = payload  # pure echo; or build your own JSON/ACK
    #cmd = f'AT+CIPSEND={link_id},{len(reply)}'
    #ok = await send_at(cmd, expect=('>',), timeout_ms=4000)
    #if ok:
        #await swriter.awrite(reply)  # no extra '\n' unless you want it

async def json_line_reader_stream(
    recv_q,
    sreader,
    *,
    on_text=None,                 # async callback(str): for non-JSON lines
    encoding='utf-8',
    max_line_bytes=8192,
    ignore_non_json=False,        # if True and no on_text, non-JSON lines are dropped
    json_predicate=None           # optional: callable(str)->bool to decide if we try JSON
):
    """
    Read newline-terminated frames from a StreamReader and handle JSON or plain text.

    - sreader: asyncio StreamReader
    - on_json: async callback taking a Python object (json-decoded)
    - on_text: async callback taking a 'str' for non-JSON lines (optional)
    - encoding: input bytes→str codec
    - max_line_bytes: guardrail against runaway lines
    - ignore_non_json: if True and on_text is None, silently drop non-JSON lines
    - json_predicate: custom detector; default = str.lstrip startswith('{') or '['
    """
    if json_predicate is None:
        def json_predicate(s: str) -> bool:
            s = s.lstrip()
            return s.startswith('{') or s.startswith('[')

    # Accumulation buffer for incomplete +IPD frames - PERSISTS across all iterations
    ipd_buffer = ''

    while True:
        line = await readline(sreader)

        if not line:
            # EOF or no data; yield briefly to avoid a tight loop
            await asyncio.sleep_ms(1)
            continue

        # Guardrail
        if len(line) > max_line_bytes:
            # Try not to blow RAM — drop this line
            # You could optionally call on_text with a truncated preview here
            # if you want to observe oversized lines.
            continue

        # Save original line with CRLF before stripping (needed for proper +IPD accumulation)
        original_line = line
        
        # Trim LF then optional CR for token checking
        if line.endswith(b'\n'):
            line = line[:-1]
            
        if line.endswith(b'\r'):
            line = line[:-1]
            
        if not line:
            continue

        try:
            text = line.decode(encoding)
            #print("json_line_reader_stream:  " + str(text))            
        except Exception as ex:
            # Fallback: best-effort replacement chars
            text = line.decode(encoding, 'ignore')

        # Check for AT response tokens FIRST, regardless of +IPD presence
        # This ensures we don't miss '>' or 'SEND OK' when interleaved with data
        if b'OK' in line:
            _maybe_set('OK')
        if b'>' in line:
            print('DEBUG: Signaling > token from line:', line[:50])
            _maybe_set('>')
        if b'ERROR' in line:
            _maybe_set('ERROR')
        if b'FAIL' in line:
            _maybe_set('FAIL')
        if b'ALREADY CONNECTED' in line:
            _maybe_set('ALREADY CONNECTED')
        if b'SEND OK' in line:
            _maybe_set('SEND OK')

        # Determine if this line should be accumulated for +IPD processing
        # We need to filter out ALL AT responses and status messages
        # Only accumulate pure +IPD frames or their continuation data
        is_at_response = (
            b'OK' in line or 
            b'>' in line or 
            b'ERROR' in line or 
            b'FAIL' in line or 
            b'SEND' in line or  # Catches both 'SEND OK' and 'Recv X bytes'
            b'CONNECT' in line or
            b'CLOSED' in line or
            b'WIFI' in line or
            b'+CIFSR' in line or
            b'Recv' in line or  # ESP-AT specific feedback
            b'busy' in line or  # ESP-AT busy status
            (len(line) <= 2 and line.strip() in (b'', b' ', b'>'))  # Single char/whitespace
        )
        
        # Always accumulate ORIGINAL line (with CRLF) for +IPD frames
        # This preserves the exact byte count declared in the +IPD header
        if '+IPD' in text:
            # New frame starts - decode original line to preserve CRLF in buffer
            try:
                ipd_buffer += original_line.decode(encoding)
            except:
                ipd_buffer += original_line.decode(encoding, 'ignore')
        elif ipd_buffer and not is_at_response:
            # Continue accumulating if we have pending data AND this isn't an AT response
            try:
                ipd_buffer += original_line.decode(encoding)
            except:
                ipd_buffer += original_line.decode(encoding, 'ignore')
        
        # Try to extract complete frames from buffer whenever we have data
        if ipd_buffer:
            while '+IPD,' in ipd_buffer:
                ipd_start = ipd_buffer.find('+IPD,')
                colon_pos = ipd_buffer.find(':', ipd_start)
                
                if colon_pos == -1:
                    # Need more data for header
                    print(f'DEBUG: Waiting for complete header, buffer len={len(ipd_buffer)}')
                    break
                
                header = ipd_buffer[ipd_start+5:colon_pos]  # between '+IPD,' and ':'
                
                try:
                    # Parse header: <link_id>,<len>
                    parts = header.split(',')
                    if len(parts) != 2:
                        print('DEBUG: Malformed +IPD header:', header)
                        # Skip this frame by removing up to colon
                        ipd_buffer = ipd_buffer[colon_pos+1:]
                        continue
                    
                    link_id = int(parts[0])
                    declared_len = int(parts[1])
                    
                    # Check if we have full payload
                    payload_start = colon_pos + 1
                    payload_end = payload_start + declared_len
                    
                    if len(ipd_buffer) < payload_end:
                        # Incomplete; wait for more data
                        have = len(ipd_buffer) - payload_start
                        print(f'DEBUG: Incomplete +IPD frame link={link_id} len={declared_len}: have {have} need {declared_len}')
                        break  # Keep buffer intact, wait for next readline
                    
                    # We have enough data in buffer based on declared length
                    print(f'DEBUG: Complete frame! link={link_id} len={declared_len}')
                    print(f'DEBUG: Buffer positions: ipd_start={ipd_start} colon={colon_pos} pay_start={payload_start} pay_end={payload_end} buf_len={len(ipd_buffer)}')
                    print(f'DEBUG: Full buffer (first 250): {repr(ipd_buffer[:250])}')
                    
                    # Check if next frame starts BEFORE the declared payload end
                    # This indicates the current frame is corrupted/truncated
                    next_frame_pos = ipd_buffer.find('\r\n+IPD,', payload_start)
                    
                    if next_frame_pos != -1 and next_frame_pos < payload_end - 2:
                        # Next frame starts before expected end (with margin for the terminating \r\n)
                        # This frame is corrupted - skip it and move to next frame
                        print(f'DEBUG: CORRUPT frame detected - next frame at {next_frame_pos}, expected end at {payload_end}')
                        print(f'DEBUG: Skipping corrupted frame and moving to next')
                        # Remove everything up to the next frame marker (skip the corrupted frame)
                        ipd_buffer = ipd_buffer[next_frame_pos + 2:]  # +2 to skip \r\n
                        continue  # Skip to next iteration to process the next frame
                    
                    # Normal case: extract full declared length
                    payload_str = ipd_buffer[payload_start:payload_end]
                    ipd_buffer = ipd_buffer[payload_end:]
                    
                    print(f'DEBUG: Extracted payload len={len(payload_str)}, first 50: {repr(payload_str[:50])}')
                    print(f'DEBUG: Extracted payload last 50: {repr(payload_str[-50:])}')
                    print(f'DEBUG: Remaining buffer after extraction: {repr(ipd_buffer[:50])}')
                    
                    # The payload is the JSON + CRLF (total = declared_len)
                    # Strip CRLF from the end
                    json_str = payload_str.rstrip('\r\n')
                    
                    if json_predicate(json_str):
                        try:
                            msg = json.loads(json_str)
                            mid = msg.get('Id')
                            print(f'DEBUG: Queueing msg Id={mid} from link={link_id}')
                            await recv_q.put((link_id, msg))
                        except Exception as ex:
                            print('DEBUG: JSON parse error:', ex)
                            print('DEBUG: Payload length:', len(payload_str))
                            print('DEBUG: JSON string length:', len(json_str))
                            print('DEBUG: Failed JSON (first 100):', repr(json_str[:100]))
                            print('DEBUG: Failed JSON (last 50):', repr(json_str[-50:]))
                            
                except Exception as ex:
                    print('DEBUG: +IPD frame parse error:', ex)
                    # Skip malformed frame
                    ipd_buffer = ipd_buffer[colon_pos+1:] if colon_pos != -1 else ''
        
        # Print non-+IPD lines for debugging
        if not '+IPD' in text and not ipd_buffer:
            print('[UART]', line)

# -------------- Example "sender" (raw TCP writes) --------------
# You will still need to wrap messages with CIPSend/CIPSENDEX for a specific connection id.
# The higher-level app should place properly formatted CIP commands into send_q.
async def sender_loop(send_q):
    while True:
        item = await send_q.get()
        
        try:
            # item is expected to be (link_id, data)
            if isinstance(item, tuple) and len(item) == 2:
                link_id, data = item
            else:
                # fallback: default link 0
                link_id = 0
                data = item

            print('Data to send:')
            print(str(data))
            print(f'DEBUG: Sending to link_id={link_id}')

            # prepare payload with CRLF so client can split lines
            payload = data if isinstance(data, (bytes, bytearray)) else data.encode('utf-8')
            payload += b'\r\n'

            queue_depth = send_q.qsize() if hasattr(send_q, 'qsize') else 'NA'
            print(f'DEBUG: send_q depth before send: {queue_depth}')
            cmd = f'AT+CIPSEND={link_id},{len(payload)}'
            print(f'DEBUG: AT command: {cmd}')
            # expect prompt '>' for data send
            ok = await send_at(cmd, expect=('>',), timeout_ms=5000)
            print(f'DEBUG: Got prompt ok={ok}')
            
            if ok:
                print(f'DEBUG: About to send {len(payload)} bytes')
                await swriter.awrite(payload)
                # Pace on SEND OK to enforce strict framing (no extra AT)
                send_ok = await wait_token('SEND OK', timeout_ms=5000)
                print(f'DEBUG: SEND OK received: {send_ok}')
                if send_ok:
                    print('Msg sent OK...')
                    # Increased pacing delay to reduce burst interleaving and ESP-AT corruption
                    await asyncio.sleep_ms(50)
                else:
                    print('Msg sent but no SEND OK token')
            else:
                print('CIPSEND prompt not received; skipping payload send')
        except Exception as ex:
            print('sender_loop error:', ex)

# -------------- Simple dispatcher for +IPD lines --------------
# Message processing counters
recv_count = [0]
send_count = [0]

async def recv_queue_processor(recv_q, send_q):
    while True:
        link_id, msg = await recv_q.get()
        recv_count[0] += 1
        
        category = msg["Category"]        

        if category == 'Files':
            await handle_files(link_id, msg, send_q)
        elif category == 'Test':
            await handle_test(link_id, msg, send_q)
        else:
            print('RX:', msg)
        
        # Periodic garbage collection to reduce fragmentation
        if recv_count[0] % 3 == 0:
            gc.collect()
            print(f'DEBUG: GC after {recv_count[0]} msgs; free={gc.mem_free()}')

# -------- Concrete Handlers (ported) --------
# Files: 3-step protocol: Header -> Content -> End
#   Header: {'Category':'Files','Step':'Header','FileName': 'name.ext'}
#   Content: {'Category':'Files','Step':'Content','FileName': 'name.ext','FileData': base64,'ProgressPercentage': n,'FileBlockSequenceNumber': n}
#   End: {'Category':'Files','Step':'End','FileName': 'name.ext','HashData': base64_of_sha256}
async def handle_files(link_id, msg, send_queue):
    global _in_hash_md5, fout
    step = msg.get('Step')
    try:
        if step == 'Header':
            _in_hash_md5 = uhashlib.sha256()
            file_name = msg['FileName']
            # mirror original path behavior
            os.makedirs('backups', exist_ok=True)
            path = 'backups/copy-' + file_name
            fout = open(path, 'wb')
        elif step == 'Content':
            data_b64 = msg.get('FileData', '')
            chunk = ubinascii.a2b_base64(data_b64) if isinstance(data_b64, str) else data_b64
            if _in_hash_md5 is not None:
                _in_hash_md5.update(chunk)
            if fout:
                fout.write(chunk)
            pp = msg.get('ProgressPercentage')
            seq = msg.get('FileBlockSequenceNumber')
            if pp is not None: print('Progress Percentage:', pp)
            if seq is not None: print('Seq Nr:', seq)
        elif step == 'End':
            if fout:
                try: fout.flush()
                except: pass
                try: fout.close()
                except: pass
            got = (_in_hash_md5.digest() if _in_hash_md5 is not None else b'')
            base64_hash = ubinascii.b2a_base64(got)[:-1].decode('utf-8')
            in_msg_hash = msg.get('HashData', '')
            file_name = msg.get('FileName', '')
            if base64_hash == in_msg_hash:
                success_q.append('File copy OK - ' + file_name)
            else:
                error_q.append('File copy failed - ' + file_name)
                error_q.append('source hash: ' + in_msg_hash)
                error_q.append('dest hash: ' + base64_hash)
        else:
            error_q.append('Files: unknown step')
    finally:
        try:
            await send_queue.put((link_id, ujson.dumps(msg)))
        except Exception as ex:
            error_q.append('Files send_queue error: %s' % ex)

# Test: verify hash of Base64Message and respond with echoed payload + its hash
async def handle_test(link_id, msg, send_queue):
    print('Test Category...')
    
    try:
        msg_id = msg.get('Id')
        b64_in = msg.get('Base64Message', '')
        b64_hash_in = msg.get('Base64MessageHash', '')
        
        # Decode Base64 message and hash
        clear = ubinascii.a2b_base64(b64_in) if isinstance(b64_in, str) else b64_in
        
        # Compute hash using sha256_simple (matches C implementation)
        calc_hash = sha256_simple(clear)
        #calc_hash = 'aa'
        
        # Compare hashes
        if calc_hash != b64_hash_in:
            err = 'test msg hash diff'
            print(err)
            print('b64_in: ' + str(b64_in))
            print('b64_hash_in (received): ' + str(b64_hash_in))
            print('b64_hash_in (calculated): ' + str(calc_hash))
            error_q.append(err)
        else:
            print("Hash Matched OK: " + str(clear))
        
        # Generate response with same hash function
        rsp_hash = sha256_simple(clear)
        b64_rsp = ubinascii.b2a_base64(clear)[:-1].decode('utf-8')
        
        rsp = {
            'Id': msg_id,
            'Category': 'Test',
            'Base64Message': b64_rsp,
            'Base64MessageHash': rsp_hash,
            'RspReceivedOK': True,
        }
        
        await send_queue.put((link_id, ujson.dumps(rsp)))
        send_count[0] += 1
        print(f'DEBUG: Queued response for Id={msg_id}; recv={recv_count[0]} send={send_count[0]}')
    except Exception as ex:
        error_q.append('Test handler error: %s' % ex)

async def heartbeat():
    led = Pin(25, Pin.OUT)
    
    while True:
        await asyncio.sleep_ms(500)
        led(not led())

async def showMemUsage():
    while True:
        print(free(True))
        await asyncio.sleep(5)

def free(full=False):
    F = gc.mem_free()
    A = gc.mem_alloc()
    T = F+A
    P = '{0:.2f}%'.format(F/T*100)
    if not full: return P
    else : return ('Total:{0} Free:{1} ({2})'.format(T,F,P))

# -------------- Orchestration --------------
async def main():
       
    # Kick off the single reader and the dispatcher
    reader_task = asyncio.create_task(json_line_reader_stream(recv_q, sreader))    
    sender_task = asyncio.create_task(sender_loop(send_q))
    queue_processor_task = asyncio.create_task(recv_queue_processor(recv_q, send_q))
    asyncio.create_task(heartbeat())
#    asyncio.create_task(showMemUsage())        

    useStaticIP = True
    
    if (useStaticIP == False):
        ok = await start_esp_server(SSID, PASSWORD, PORT)
    else:
        ok = await start_tcp_server_static_sta(
            ssid="Cudy24G",
            pwd="ZAnne19991214",
            ip="192.168.10.250",
            gw="192.168.10.1",
            mask="255.255.255.0",
            port=8080)
    
    if not ok:
        print('ESP setup failed; stopping.')
        reader_task.cancel()
        sender_task.cancel()
        return

    # Example: periodically print free mem
    async def monitor():
        while True:
            gc.collect()
            free = gc.mem_free()
            alloc = gc.mem_alloc()
            total = free + alloc
            pct = free * 100 / total if total else 0
            print('Free mem: %d (%.1f%%)' % (free, pct))
            await asyncio.sleep(3)

#    mon_task = asyncio.create_task(monitor())
    await asyncio.gather(reader_task, sender_task, queue_processor_task)
#    await asyncio.gather(reader_task, mon_task, queue_processor_task)    

# -------------- Entry --------------
def run():
    try:
        asyncio.run(main())
    finally:
        # needed by MicroPython to allow subsequent asyncio.run()
        asyncio.new_event_loop()

if __name__ == '__main__':
    run()
