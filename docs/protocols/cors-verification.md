# Browser reachability verification (MVP slice 1)

Verified: 2026-09-27. Probe: `tools/cors-probe.html` served from
`http://localhost:8080` (python `http.server`), Chrome (Claude-in-Chrome session).
WebSocket per-stream delivery was cross-checked from Node 24.11 (built-in `WebSocket`).

## REST (fetch from page origin)

| Endpoint | Result |
|---|---|
| Binance `GET /fapi/v1/exchangeInfo` | 200, readable (1,127,529 bytes) |
| Binance `GET /fapi/v1/depth?symbol=BTCUSDT&limit=1000` | 200, readable (42,088 bytes) |
| Binance `GET /fapi/v1/klines?symbol=BTCUSDT&interval=1m&limit=1000` | 200, readable (129,906 bytes) |
| Binance `GET /fapi/v1/openInterest?symbol=BTCUSDT` | 200, readable (68 bytes) |
| Hyperliquid `POST /info {type:metaAndAssetCtxs}` | 200, readable (72,502 bytes) |
| Hyperliquid `POST /info {type:candleSnapshot}` | 200, readable (134,420 bytes) |

All REST endpoints are CORS-readable. Note: full `exchangeInfo` is 1.1 MB, above
the planned `k_max_raw_frame_bytes` (512 KiB); the bridge must trim it to the
BTCUSDT entry before forwarding (or the bound must change — owner decision).

## WebSocket

| Endpoint | Result |
|---|---|
| Hyperliquid `wss://api.hyperliquid.xyz/ws` (subscribe trades BTC) | open, first message received |
| Binance legacy `wss://fstream.binance.com/stream?streams=<all 7>` | opens, but **only `depth@100ms` and `bookTicker` deliver**; `aggTrade`, `markPrice@1s`, `ticker`, `kline_1m` silent for 15 s |
| Binance `wss://fstream.binance.com/public/stream?streams=<all 7>` | delivers `depth@100ms`, `bookTicker` only |
| Binance `wss://fstream.binance.com/market/stream?streams=<all 7>` | delivers `aggTrade`, `markPrice@1s`, `ticker`, `kline_1m` (`forceOrder` quiet in window) |

Upstream finding: Binance USD-M now splits market-data streams across `/public`
and `/market` routes. The official stream docs
(https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams,
read 2026-09-27) show `/public/ws` and `/public/stream` base paths and separate
"Public" and "Market" stream sections. This contradicts packet §9 "One WebSocket
connection per venue" for Binance: two connections are required. **Stopped for
owner decision.**

## Owner decisions (2026-09-27)

- Binance uses two sockets (`/public` + `/market`) under one venue feed state; packet §9 amended.
- ADR-0005 (yyjson 0.10.0) accepted.
- `exchangeInfo` is trimmed to the `BTCUSDT` symbol entry in the TypeScript bridge before being
  forwarded; `k_max_raw_frame_bytes` stays 512 KiB.
