# Binance USD-M Futures — BTCUSDT protocol notes

Verified: 2026-09-27. Public endpoints only; no keys.

## Connection rules

Source: https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams/Connect (read 2026-09-27)

- Routed endpoints: `wss://fstream.binance.com/public` (high-frequency: depth, bookTicker),
  `/market` (aggTrade, markPrice, ticker, kline, forceOrder), `/private` (user data, not used).
  Unrouted connections receive Public data only. The MVP opens two sockets (owner decision,
  `cors-verification.md`).
- Combined stream frames: `{"stream":"<name>","data":{...}}`.
- A connection is valid for 24 h; the driver reconnects proactively before that.
- Server sends a ping frame every 3 minutes; browsers answer with pong automatically.
  No pong within 10 minutes ⇒ disconnect.
- Max 10 incoming (client→server) messages per second; max 1024 streams per connection.
  The MVP subscribes through the URL and sends no client messages.

## Streams

Source index: https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams (read 2026-09-27)

| Stream | Fields used |
|---|---|
| `btcusdt@aggTrade` | `a` agg trade id, `p` price, `q` qty, `T` trade time, `m` buyer is maker (`true` ⇒ aggressor Sell), `s` symbol |
| `btcusdt@depth@100ms` | `U` first update id, `u` final update id, `pu` previous final update id, `b`/`a` `[price, qty]` (qty `0` removes), `T` transaction time, `E` event time |
| `btcusdt@bookTicker` | `u` update id, `b`/`B` best bid px/qty, `a`/`A` best ask px/qty, `T`, `E` |
| `btcusdt@markPrice@1s` | `p` mark, `i` index, `r` funding rate, `T` next funding time, `E` |
| `btcusdt@ticker` | `c` last, `p` change, `P` change %, `o` open, `h`, `l`, `v` base vol, `q` quote vol, `E` |
| `btcusdt@kline_<i>` | `k.t` open time, `k.T` close time, `k.o/h/l/c`, `k.v` base vol, `k.q` quote vol, `k.x` closed |
| `btcusdt@forceOrder` | `o.s`, `o.S` side (`SELL` ⇒ long liquidated), `o.p` price, `o.ap` avg price, `o.q` qty, `o.T` |

## REST

| Endpoint | Weight | Notes |
|---|---|---|
| `GET /fapi/v1/exchangeInfo` | 1 | 1.1 MB; bridge trims to BTCUSDT (tickSize from `PRICE_FILTER`, stepSize from `LOT_SIZE`) |
| `GET /fapi/v1/depth?symbol=BTCUSDT&limit=1000` | 20 | `lastUpdateId`, `bids`, `asks` |
| `GET /fapi/v1/klines?...&limit=1000` | 5 | array rows `[openTime, o, h, l, c, v, closeTime, quoteVol, trades, ...]`; last row may be open |
| `GET /fapi/v1/openInterest?symbol=BTCUSDT` | 1 | `openInterest`, `time`; polled every 10 s (`OI_POLL_MS`) |

Startup weight budget: exchangeInfo 1 + 6 kline intervals × 5 + depth 20 + OI 1 = 52,
far below the 2400/min IP limit. Resyncs add 20 each; OI polling adds 6/min.

Order book sync procedure: https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams/How-to-manage-a-local-order-book-correctly
(read 2026-09-27), implemented per `docs/calculations/book-sync.md`.
