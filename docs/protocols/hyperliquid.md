# Hyperliquid — BTC perp protocol notes

Verified: 2026-09-27. Public endpoints only.

## Connection rules

- WebSocket `wss://api.hyperliquid.xyz/ws`. Subscribe with
  `{"method":"subscribe","subscription":{...}}`; server acks with
  `{"channel":"subscriptionResponse","data":{...}}`.
  https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/subscriptions (read 2026-09-27)
- Server closes connections that have not received a client message for 60 s.
  Heartbeat `{"method":"ping"}` → `{"channel":"pong"}`; the driver pings every 30 s.
  https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/timeouts-and-heartbeats (read 2026-09-27)
- After reconnect all subscriptions are re-sent.

## Channels (all `coin: "BTC"`)

| Channel | Fields used |
|---|---|
| `trades` | `data[]`: `coin`, `side` (`B` ⇒ aggressor Buy, `A` ⇒ Sell), `px`, `sz`, `time`, `tid` |
| `l2Book` | `data.coin`, `data.time`, `data.levels[0]` bids / `[1]` asks, each `{px, sz, n}`; full snapshot every message |
| `bbo` | `data.coin`, `data.time`, `data.bbo[0]` bid / `[1]` ask (`{px,sz,n}` or null) |
| `activeAssetCtx` | `data.coin`, `data.ctx`: `markPx`, `oraclePx`, `funding`, `openInterest` (base units), `dayNtlVlm`, `prevDayPx`, `midPx` |
| `candle` (`interval:"1m"` etc.) | `t` open, `T` close, `s` coin, `i` interval, `o/h/l/c`, `v` base vol, `n` trades |

## Info endpoint (`POST https://api.hyperliquid.xyz/info`)

https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint/perpetuals (read 2026-09-27)

| Request | Notes |
|---|---|
| `{"type":"metaAndAssetCtxs"}` | `[meta, ctxs]`; BTC entry gives `szDecimals`; price tick derived from 5 significant figures / `6 - szDecimals` decimals rule |
| `{"type":"candleSnapshot","req":{coin,interval,startTime,endTime}}` | up to 5000 most recent candles |

## Liquidations

No public liquidation stream is documented. The MVP shows `Unsupported` and never
infers liquidations from trades (packet §4.2).

## Normalization choices (adapter, 2026-09-27)

- `activeAssetCtx` has no timestamp: source time = bridge receive time.
- It yields three events: `AssetMetrics` (mark, oracle, funding), `OpenInterest`
  (`openInterest` base units, notional = OI × markPx exact, `sample_interval_ms = 0` =
  streamed), `MarketSummary` (`midPx`, `change_24h = markPx − prevDayPx`, `dayNtlVlm`).
- A `bbo` with a null side is valid upstream but produces no event (`Ignored`).
- Live `candle` frames have no closed flag; closed ⇔ `T < receive_time`. In a
  `candleSnapshot` every bar but the newest is closed.
- Trade notional treats USDC as USD.
- `trades` replays recent trades on subscribe: in the 2026-09-27 capture the first frame held
  30 trades spanning 33 s before the subscription. The subscriptions page (read 2026-09-27)
  only documents snapshots for user feeds. The engine therefore drops trades whose `tid` was
  seen in the last 1,024 trades (`runtime::k_trade_dedupe_window`), so reconnects do not
  double-count CVD/footprint.
