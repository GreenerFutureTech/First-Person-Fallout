// f2_server socket transport — SocketByteSink (P5-C STEP 3 "MAKE IT ACCEPT").
// See server_net.h for the model. This is an f2_server-only TU; it is never
// linked into f2_core, whose no-SDL / no-transport invariant this respects —
// the wire ENCODER is core, the wire TRANSPORT is here. Sockets go through
// platform_net.h so the same file serves the POSIX and Winsock builds.

#include "server_net.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "wire_defs.h"

namespace {

// A client's inbound line buffer is capped: a client that streams bytes with no
// newline (or one absurd "line") must not grow it without bound (memory DoS).
// The v1 command vocabulary is tiny "verb arg arg2"; 1 KB is generous.
constexpr size_t kMaxInbuf = 1024;

// Keep a slow viewer's queued stream bounded without blocking the simulation.
constexpr size_t kMaxClientOutqBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxSendBytesPerPump = 1u * 1024u * 1024u;
// Used for the small blocking preamble and the final closeAll flush only.
constexpr int kSendTimeoutSec = 5;

// Monotonic wall clock in ms — the outbox schedule's time base (§8.6). Anchored to
// wall, NOT the sim clock: the sim already runs ~1:1 real time, and a sim-clock anchor
// would park the queue for hours on a script time-skip.
long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// F2_SERVER_OUTBOX_PACE=1 arms per-frame release scheduling. Default OFF = increment-2
// behavior (every frame due immediately), so goldens/gates and today's demos are
// unchanged until an operator opts in. Read once.
bool outboxPaceEnabled()
{
    static bool enabled = getenv("F2_SERVER_OUTBOX_PACE") != nullptr;
    return enabled;
}

bool netTraceEnabled()
{
    static bool enabled = getenv("F2_NET_TRACE") != nullptr;
    return enabled;
}

std::string traceCommand(const std::string& line)
{
    std::string command(line);
    if (command.compare(0, 6, "login ") == 0) {
        size_t nameEnd = command.find(' ', 6);
        if (nameEnd != std::string::npos) {
            size_t tokenStart = command.find_first_not_of(' ', nameEnd);
            if (tokenStart != std::string::npos) {
                command.resize(tokenStart);
                command += "<redacted-token>";
            }
        }
    }
    return command;
}

} // namespace

namespace fallout {

// Write all `size` bytes to `fd`, retrying short writes and EINTR. Returns false
// on a genuine error (EPIPE/ECONNRESET on a dead client, etc.) so the caller can
// drop that client. SIGPIPE is ignored process-wide (netPlatformInit, see
// listen()), so a write to a closed peer returns an error rather than killing us.
static bool writeAll(NetSocket fd, const void* data, size_t size)
{
    const char* p = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining != 0) {
        long n = netSend(fd, p, remaining);
        if (n < 0) {
            if (netErrorInterrupted(netLastError())) {
                continue;
            }
            return false;
        }
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

// The per-client accept preamble: "F2NS" | LE u16 kWireVersion | LE i32 sessionId
// (wire_defs.h). This is the ONLY per-client byte run in the protocol — everything
// after it is the shared broadcast stream — which is exactly why the sessionId
// rides here and nowhere else (MP_PROPOSAL.md Ch 5.5).
//
// Written straight to the socket, deliberately NOT through write(): that path
// broadcasts to every client and tees to the wire log, and these bytes belong to
// one client. Hand-rolled little-endian to match the encoder's putters — the
// stream must not acquire a host-endian byte here of all places.
static bool writeClientPreamble(NetSocket fd, int sessionId)
{
    unsigned char preamble[kWirePreambleLen];
    memcpy(preamble, kWireMagic, sizeof(kWireMagic));
    preamble[4] = (unsigned char)(kWireVersion & 0xFF);
    preamble[5] = (unsigned char)((kWireVersion >> 8) & 0xFF);
    preamble[6] = (unsigned char)(sessionId & 0xFF);
    preamble[7] = (unsigned char)((sessionId >> 8) & 0xFF);
    preamble[8] = (unsigned char)((sessionId >> 16) & 0xFF);
    preamble[9] = (unsigned char)((sessionId >> 24) & 0xFF);
    return writeAll(fd, preamble, sizeof(preamble));
}

SocketByteSink::~SocketByteSink()
{
    closeAll();
}

bool SocketByteSink::wantsStreamPreamble() const
{
    // Per-client, at accept — see writeClientPreamble.
    return false;
}

bool SocketByteSink::listen(uint16_t port)
{
    closeAll();

    // A dead client must never kill the server via SIGPIPE; writeAll relies on
    // this to see an error instead. Process-wide, idempotent (and on Windows
    // this is where WSAStartup runs).
    netPlatformInit();

    NetSocket fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!netSocketValid(fd)) {
        fprintf(stderr, "f2_server: socket() failed: %s\n", netErrorString(netLastError()));
        return false;
    }

    // Let the port be reused immediately after a previous serve exits (TIME_WAIT).
    netSetReuseAddr(fd);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "f2_server: bind(:%u) failed: %s\n", port, netErrorString(netLastError()));
        netCloseSocket(fd);
        return false;
    }

    if (::listen(fd, 4) < 0) {
        fprintf(stderr, "f2_server: listen() failed: %s\n", netErrorString(netLastError()));
        netCloseSocket(fd);
        return false;
    }

    _listenFd = fd;
    _listenNonBlocking = false;
    return true;
}

bool SocketByteSink::acceptClients(int minClients)
{
    if (!netSocketValid(_listenFd)) {
        return false;
    }

    while (static_cast<int>(_clients.size()) < minClients) {
        NetSocket fd = ::accept(_listenFd, nullptr, nullptr);
        if (!netSocketValid(fd)) {
            if (netErrorInterrupted(netLastError())) {
                continue;
            }
            fprintf(stderr, "f2_server: accept() failed: %s\n", netErrorString(netLastError()));
            return false;
        }

        // The wire is many small frames; disable Nagle so a beat's bytes go out
        // without waiting to coalesce (latency over throughput).
        netSetNoDelay(fd);

        // Bound the preamble write and the eventual closeAll flush. Live frame
        // sends become non-blocking after the preamble.
        netSetSendTimeout(fd, kSendTimeoutSec);

        int sessionId = _nextSessionId++;
        if (!writeClientPreamble(fd, sessionId)) {
            fprintf(stderr, "f2_server: client dropped on preamble write\n");
            netCloseSocket(fd);
            continue;
        }
        if (!netSetNonBlocking(fd, true)) {
            fprintf(stderr, "f2_server: client dropped: cannot set non-blocking mode\n");
            netCloseSocket(fd);
            continue;
        }
        if (netTraceEnabled()) {
            fprintf(stderr, "f2_server: NETTRACE TX preamble session=%d magic=F2NS version=%u bytes=%d\n",
                sessionId, kWireVersion, kWirePreambleLen);
        }

        _clients.push_back(Client { fd, std::string(), sessionId });
        _clients.back().lastReleaseAtMs = nowMs(); // seed the release cursor (§8.6)
        // Keep "client connected (N total)" intact ahead of the session id:
        // gate scripts grep these lines (scripts/check_midjoin.sh et al).
        fprintf(stderr, "f2_server: client connected (%zu total) as session %d\n",
            _clients.size(), sessionId);
    }

    return true;
}

int SocketByteSink::acceptPending()
{
    if (!netSocketValid(_listenFd)) {
        return 0;
    }

    // acceptClients() used the listener blocking at boot; mid-stream polling
    // needs would-block instead of a stall. Flipped once, tracked in a flag
    // (Winsock cannot read the mode back the way fcntl(F_GETFL) can).
    if (!_listenNonBlocking) {
        netSetNonBlocking(_listenFd, true);
        _listenNonBlocking = true;
    }

    int accepted = 0;
    for (;;) {
        NetSocket fd = ::accept(_listenFd, nullptr, nullptr);
        if (!netSocketValid(fd)) {
            if (netErrorInterrupted(netLastError())) {
                continue;
            }
            break; // would-block — nothing more pending
        }

        // Winsock children inherit the listener's non-blocking mode (POSIX ones
        // do not); use blocking mode for the bounded preamble write, then switch
        // to non-blocking once the session is registered.
        if (!netSetNonBlocking(fd, false)) {
            fprintf(stderr, "f2_server: joiner dropped: cannot set blocking mode for preamble\n");
            netCloseSocket(fd);
            continue;
        }
        netSetNoDelay(fd);
        netSetSendTimeout(fd, kSendTimeoutSec);

        int sessionId = _nextSessionId++;
        if (!writeClientPreamble(fd, sessionId)) {
            fprintf(stderr, "f2_server: joiner dropped on preamble write\n");
            netCloseSocket(fd);
            continue;
        }
        if (!netSetNonBlocking(fd, true)) {
            fprintf(stderr, "f2_server: joiner dropped: cannot set non-blocking mode\n");
            netCloseSocket(fd);
            continue;
        }
        if (netTraceEnabled()) {
            fprintf(stderr, "f2_server: NETTRACE TX preamble session=%d magic=F2NS version=%u bytes=%d\n",
                sessionId, kWireVersion, kWirePreambleLen);
        }

        _clients.push_back(Client { fd, std::string(), sessionId });
        _clients.back().lastReleaseAtMs = nowMs(); // seed the release cursor (§8.6)
        accepted++;
        fprintf(stderr, "f2_server: client joined mid-stream (%zu total) as session %d\n",
            _clients.size(), sessionId);
    }
    return accepted;
}

void SocketByteSink::setTeeFile(const char* path)
{
    if (_tee != nullptr) {
        fclose(_tee);
        _tee = nullptr;
    }
    if (path != nullptr) {
        _tee = fopen(path, "wb");
        if (_tee == nullptr) {
            fprintf(stderr, "f2_server: tee: cannot open '%s' for writing: %s\n", path, strerror(errno));
        }
    }
}

void SocketByteSink::write(const void* data, unsigned int size)
{
    if (size == 0) {
        return;
    }
    // Direct-bytes fallback (no frame boundary / cost): copy into a shared buffer,
    // tee, enqueue to every client with zero-cost meta, pump. Not on the hot path —
    // the encoder emits through writeFrame; this covers any raw-byte caller.
    const unsigned char* p = static_cast<const unsigned char*>(data);
    auto buf = std::make_shared<std::vector<unsigned char>>(p, p + size);
    if (_tee != nullptr) {
        fwrite(buf->data(), 1, buf->size(), _tee);
    }
    for (Client& c : _clients) {
        c.queuedBytes += buf->size();
        c.outq.push_back(QueuedFrame { buf, WireFrameMeta {}, 0 });
    }
    pump();
}

void SocketByteSink::writeFrame(const unsigned char* header, unsigned int headerLen,
    const unsigned char* payload, unsigned int payloadLen, const WireFrameMeta& meta)
{
    if (netTraceEnabled()) {
        fprintf(stderr,
            "f2_server: NETTRACE TX frame seq=%u simTs=%u payload=%u events=%u entryBase=%u clients=%zu total=%u\n",
            meta.seq, meta.simTs, payloadLen, meta.eventCount, meta.entryBase,
            _clients.size(), headerLen + payloadLen);
    }
    // One encoded frame → one SHARED buffer, referenced by every client's queue (one
    // copy, N pointers). The tee gets the canonical bytes in EMISSION order (the
    // single stream every client receives a contiguous copy of, §8.6).
    auto buf = std::make_shared<std::vector<unsigned char>>();
    buf->reserve(headerLen + payloadLen);
    buf->insert(buf->end(), header, header + headerLen);
    if (payloadLen != 0) {
        buf->insert(buf->end(), payload, payload + payloadLen);
    }

    if (_tee != nullptr) {
        fwrite(buf->data(), 1, buf->size(), _tee);
    }

    const bool paced = outboxPaceEnabled();
    const long long now = nowMs();
    for (Client& c : _clients) {
        QueuedFrame qf { buf, meta, 0, 0 };
        c.queuedBytes += buf->size();
        if (paced) {
            // release = max(now, previous frame's release + its cost). The frame
            // itself releases at that time; ITS cost defers the NEXT frame. max(now)
            // re-anchors an idle client so lag never accrues phantom credit (§8.6).
            long long chained = c.lastReleaseAtMs + (long long)c.lastCostMs;
            qf.releaseAtMs = now > chained ? now : chained;
            c.lastReleaseAtMs = qf.releaseAtMs;
            c.lastCostMs = meta.costMs;
        }
        c.outq.push_back(std::move(qf));
    }
    pump();
}

void SocketByteSink::pump()
{
    // Drain each client's queue toward its socket without blocking the simulation.
    // A frame is sent only once its releaseAtMs has passed (0 = due now, the unpaced
    // default); partial sends remain at the head until the next pump. Queue growth
    // is bounded for clients that stay stalled.
    const long long now = nowMs();
    size_t out = 0;
    for (size_t i = 0; i < _clients.size(); i++) {
        Client& c = _clients[i];
        bool alive = true;
        int sendError = 0;
        bool zeroWrite = false;
        size_t sentThisPump = 0;
        while (!c.outq.empty()) {
            QueuedFrame& qf = c.outq.front();
            if (qf.releaseAtMs > now) {
                break; // not due yet; later frames are scheduled no earlier
            }
            const std::vector<unsigned char>& b = *qf.bytes;
            long n = netSend(c.fd, b.data() + qf.sent, b.size() - qf.sent);
            if (n > 0) {
                qf.sent += (size_t)n;
                c.queuedBytes -= (size_t)n;
                sentThisPump += (size_t)n;
                if (qf.sent == b.size()) {
                    c.outq.pop_front();
                }
                if (sentThisPump >= kMaxSendBytesPerPump) {
                    break;
                }
                continue;
            }
            if (n == 0) {
                alive = false;
                zeroWrite = true;
                break;
            }
            int err = netLastError();
            if (netErrorInterrupted(err)) {
                continue;
            }
            if (netErrorWouldBlock(err)) {
                break;
            }
            sendError = err;
            alive = false;
            break;
        }
        if (alive && c.queuedBytes <= kMaxClientOutqBytes) {
            if (out != i) {
                _clients[out] = std::move(_clients[i]);
            }
            out++;
        } else {
            if (sendError != 0) {
                fprintf(stderr, "f2_server: client dropped on write: %s\n", netErrorString(sendError));
            } else if (zeroWrite) {
                fprintf(stderr, "f2_server: client dropped on write: send returned 0\n");
            } else {
                fprintf(stderr, "f2_server: client dropped on outbound backlog: %zu bytes queued\n",
                    c.queuedBytes);
            }
            netCloseSocket(_clients[i].fd);
        }
    }
    _clients.resize(out);
}

void SocketByteSink::pollInbound(const std::function<void(int sessionId, const char* line)>& onLine)
{
    // Service outbound first: this is the one call every socket-service site makes —
    // the serve loop each beat AND every modal barrier pump (dialog/barter/movie/
    // worldmap). Pumping here is what releases a HELD frame while the sim is parked in
    // a barrier waiting on the client's response to it — without this the paced path
    // would deadlock (server blocked on an ack for a frame still in the outbox, §8.6).
    // No-op when unpaced or queues are empty.
    pump();

    char buf[4096];

    // Walk clients by index; dropClient() compacts in place, so on a drop we do
    // NOT advance (the next client slid into this slot).
    for (size_t i = 0; i < _clients.size();) {
        Client& client = _clients[i];

        bool closed = false;
        for (;;) {
            long n = netRecvNoWait(client.fd, buf, sizeof(buf));
            if (n > 0) {
                if (netTraceEnabled()) {
                    fprintf(stderr, "f2_server: NETTRACE RX session=%d bytes=%ld\n",
                        client.sessionId, n);
                }
                client.inbuf.append(buf, static_cast<size_t>(n));
                // A large read may hold several lines; keep draining until the
                // socket has nothing more pending (would-block below).
                continue;
            }
            if (n == 0) {
                if (netTraceEnabled()) {
                    fprintf(stderr, "f2_server: NETTRACE RX orderly-close session=%d\n",
                        client.sessionId);
                }
                closed = true; // orderly peer shutdown
                break;
            }
            // n < 0
            int err = netLastError();
            if (netErrorInterrupted(err)) {
                continue;
            }
            if (netErrorWouldBlock(err)) {
                break; // nothing more pending this beat
            }
            if (netTraceEnabled()) {
                fprintf(stderr, "f2_server: NETTRACE RX socket-error session=%d error=%s code=%d\n",
                    client.sessionId, netErrorString(err), err);
            }
            closed = true; // genuine error
            break;
        }

        // Extract every complete line accumulated so far into a local list; the
        // partial tail stays in inbuf for the next poll. We collect FIRST and
        // dispatch AFTER, so no reference into _clients is held across onLine
        // (which runs commandDispatch). Contract: onLine must not mutate the
        // client set — it dispatches a sim command and returns; it never touches
        // the sink. If that ever changes, this index walk needs revisiting.
        std::vector<std::string> lines;
        size_t startPos = 0;
        size_t nl;
        while ((nl = client.inbuf.find('\n', startPos)) != std::string::npos) {
            size_t end = nl;
            if (end > startPos && client.inbuf[end - 1] == '\r') {
                end--; // tolerate CRLF
            }
            if (end > startPos) {
                lines.emplace_back(client.inbuf, startPos, end - startPos);
            }
            startPos = nl + 1;
        }
        client.inbuf.erase(0, startPos);

        // Memory-DoS guard: a client that never terminates a line grows inbuf
        // without bound. Past the cap, drop it (the residual is not a valid v1
        // command anyway).
        if (client.inbuf.size() > kMaxInbuf) {
            if (netTraceEnabled()) {
                fprintf(stderr, "f2_server: NETTRACE RX overlong-command session=%d buffered=%zu limit=%zu\n",
                    client.sessionId, client.inbuf.size(), kMaxInbuf);
            }
            closed = true;
        }

        // Capture the identity before dispatch: onLine may not mutate the client
        // set, but a copy is cheap and keeps no _clients reference live across it.
        int sessionId = client.sessionId;
        for (const std::string& line : lines) {
            if (netTraceEnabled()) {
                std::string safeLine = traceCommand(line);
                fprintf(stderr, "f2_server: NETTRACE RX command session=%d bytes=%zu line=%s\n",
                    sessionId, line.size() + 1, safeLine.c_str());
            }
            onLine(sessionId, line.c_str());
        }

        if (closed) {
            if (netTraceEnabled() && !client.inbuf.empty()) {
                fprintf(stderr, "f2_server: NETTRACE RX incomplete session=%d buffered=%zu\n",
                    client.sessionId, client.inbuf.size());
            }
            dropClient(i, "closed/overlong");
            continue; // do not advance i
        }
        i++;
    }
}

bool SocketByteSink::hasSession(int sessionId) const
{
    for (const Client& client : _clients) {
        if (client.sessionId == sessionId) {
            return true;
        }
    }
    return false;
}

bool SocketByteSink::closeSession(int sessionId)
{
    for (size_t i = 0; i < _clients.size(); i++) {
        if (_clients[i].sessionId == sessionId) {
            dropClient(i, "kicked");
            return true;
        }
    }
    return false;
}

void SocketByteSink::dropClient(size_t index, const char* why)
{
    fprintf(stderr, "f2_server: client dropped (%s), %zu remain\n", why, _clients.size() - 1);
    netCloseSocket(_clients[index].fd);
    _clients.erase(_clients.begin() + static_cast<std::ptrdiff_t>(index));
}

void SocketByteSink::flush()
{
    // No application-level buffer for the sockets; write() already handed bytes
    // to the kernel. The tee is buffered stdio, so push it to match the sockets'
    // per-frame delivery (keeps the log and the socket capture aligned).
    if (_tee != nullptr) {
        fflush(_tee);
    }
}

void SocketByteSink::closeAll()
{
    // Best-effort flush of any HELD frames before closing, so a paced shutdown doesn't
    // truncate a client's capture below the tee (§8.6 trap 3). Restore blocking mode
    // only for this final bounded flush; live sends are non-blocking.
    for (Client& client : _clients) {
        if (!netSetNonBlocking(client.fd, false)) {
            continue;
        }
        while (!client.outq.empty()) {
            QueuedFrame& qf = client.outq.front();
            const std::vector<unsigned char>& b = *qf.bytes;
            if (!writeAll(client.fd, b.data() + qf.sent, b.size() - qf.sent)) {
                break; // dead/stuck client — abandon its remaining queue
            }
            client.outq.pop_front();
        }
    }

    for (const Client& client : _clients) {
        netCloseSocket(client.fd);
    }
    _clients.clear();

    if (netSocketValid(_listenFd)) {
        netCloseSocket(_listenFd);
        _listenFd = kNetInvalidSocket;
    }
    _listenNonBlocking = false;

    if (_tee != nullptr) {
        fclose(_tee);
        _tee = nullptr;
    }
}

// ---------------------------------------------------------------------------
// CommandListener — inbound-only control channel (see server_net.h).
// ---------------------------------------------------------------------------

CommandListener::~CommandListener()
{
    closeAll();
}

// A reply sink bound to one control-client fd. The fd is captured by value and no
// caller lets the sink outlive the poll that made it, so a client dropped later in
// that same poll cannot be written to through a stale copy.
static CommandListener::ReplyFn commandReplySink(NetSocket replyFd)
{
    return [replyFd](const char* text) {
        if (text == nullptr) {
            return;
        }
        std::string out(text);
        if (out.empty() || out.back() != '\n') {
            out.push_back('\n');
        }
        // A long listing can go out short (a full send buffer, or a send timeout).
        // Retry a bounded number of times rather than spinning: a control client
        // too wedged to drain a few KB gets its output truncated, which must not
        // stall the simulation.
        size_t sent = 0;
        for (int attempt = 0; attempt < 100 && sent < out.size(); attempt++) {
            long n = netSend(replyFd, out.data() + sent, out.size() - sent);
            if (n > 0) {
                sent += static_cast<size_t>(n);
                continue;
            }
            int err = netLastError();
            if (n < 0 && (netErrorInterrupted(err) || netErrorWouldBlock(err))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            break; // closed or hard error — the drain loop will reap it
        }
    };
}

bool CommandListener::listen(uint16_t port)
{
    closeAll();

    // A control client that dies mid-write must not signal us (we never write, but
    // keep the same discipline as SocketByteSink); idempotent.
    netPlatformInit();

    NetSocket fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!netSocketValid(fd)) {
        fprintf(stderr, "f2_server: cmd socket() failed: %s\n", netErrorString(netLastError()));
        return false;
    }

    netSetReuseAddr(fd);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "f2_server: cmd bind(:%u) failed: %s\n", port, netErrorString(netLastError()));
        netCloseSocket(fd);
        return false;
    }

    if (::listen(fd, 4) < 0) {
        fprintf(stderr, "f2_server: cmd listen() failed: %s\n", netErrorString(netLastError()));
        netCloseSocket(fd);
        return false;
    }

    // Non-blocking accept: pollCommands must never block the single-threaded serve
    // loop waiting for a control connection that may never come.
    netSetNonBlocking(fd, true);

    _listenFd = fd;
    return true;
}

void CommandListener::pollCommands(const std::function<void(const char* line)>& onLine)
{
    pollCommands([&onLine](const char* line, const ReplyFn&) { onLine(line); });
}

void CommandListener::pollCommands(const std::function<void(const char* line, const ReplyFn& reply)>& onLine)
{
    if (!netSocketValid(_listenFd)) {
        return;
    }

    // Accept any control clients that have arrived since the last poll (may accept
    // several at once; accept can connect at ANY time — no baseline is owed here).
    for (;;) {
        NetSocket fd = ::accept(_listenFd, nullptr, nullptr);
        if (!netSocketValid(fd)) {
            if (netErrorInterrupted(netLastError())) {
                continue;
            }
            break; // would-block — nothing more pending
        }
        // Same Winsock-inheritance note as SocketByteSink::acceptPending: force
        // blocking mode so replies behave the same on both platforms (the reply
        // sink's retry loop covers the short-send cases either way).
        netSetNonBlocking(fd, false);
        netSetNoDelay(fd);
        _clients.push_back(Client { fd, std::string() });
        fprintf(stderr, "f2_server: control client connected (%zu total)\n", _clients.size());

        // Greet before this client has said anything. A send failure here needs no
        // handling: the drain loop below reaps the fd on its next pass.
        if (_greeting) {
            _greeting(commandReplySink(fd));
        }
    }

    // Drain each control client into a local dispatch list FIRST, holding no
    // reference or index into _clients across onLine below.
    //
    // ►► onLine RE-ENTERS pollCommands: the movie/dialog/barter/worldmap barriers
    // pump this very command channel while they block. A re-entrant poll accepts
    // and drops control clients, which reallocates/erases _clients — so a live
    // `Client&` (or the loop index) held across onLine dangles, and the unwinding
    // iteration then frees garbage (observed SIGSEGV: __libc_free in pollCommands,
    // repro = `movie` sent by an `nc -q1` that disconnects mid-cutscene). This is
    // the exact hazard SocketByteSink::pollInbound documents avoiding — extract
    // (fd, line) here, dispatch after. The fd is copied by value, so a client
    // dropped mid-dispatch just makes its reply send fail harmlessly.
    struct PendingLine {
        NetSocket fd;
        std::string text;
    };
    std::vector<PendingLine> pending;

    char buf[4096];
    for (size_t i = 0; i < _clients.size();) {
        Client& client = _clients[i];

        bool closed = false;
        for (;;) {
            long n = netRecvNoWait(client.fd, buf, sizeof(buf));
            if (n > 0) {
                client.inbuf.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                closed = true;
                break;
            }
            int err = netLastError();
            if (netErrorInterrupted(err)) {
                continue;
            }
            if (netErrorWouldBlock(err)) {
                break;
            }
            closed = true;
            break;
        }

        size_t startPos = 0;
        size_t nl;
        while ((nl = client.inbuf.find('\n', startPos)) != std::string::npos) {
            size_t end = nl;
            if (end > startPos && client.inbuf[end - 1] == '\r') {
                end--;
            }
            if (end > startPos) {
                pending.push_back({ client.fd, client.inbuf.substr(startPos, end - startPos) });
            }
            startPos = nl + 1;
        }
        client.inbuf.erase(0, startPos);

        if (client.inbuf.size() > kMaxInbuf) {
            closed = true;
        }

        if (closed) {
            dropClient(i);
            continue;
        }
        i++;
    }

    // Dispatch AFTER the drain — no _clients reference is live, so a re-entrant
    // pollCommands (a barrier pump) that mutates _clients cannot corrupt this walk.
    for (const PendingLine& line : pending) {
        ReplyFn reply = commandReplySink(line.fd);
        onLine(line.text.c_str(), reply);
    }
}

void CommandListener::dropClient(size_t index)
{
    fprintf(stderr, "f2_server: control client dropped, %zu remain\n", _clients.size() - 1);
    netCloseSocket(_clients[index].fd);
    _clients.erase(_clients.begin() + static_cast<std::ptrdiff_t>(index));
}

void CommandListener::closeAll()
{
    for (const Client& client : _clients) {
        netCloseSocket(client.fd);
    }
    _clients.clear();

    if (netSocketValid(_listenFd)) {
        netCloseSocket(_listenFd);
        _listenFd = kNetInvalidSocket;
    }
}

} // namespace fallout
