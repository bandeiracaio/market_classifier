#include "market_classifier/venues/binance_adapter.hpp"

#include "adapter_common.hpp"

// Field meanings: docs/protocols/binance.md (verified 2026-09-27 against
// https://developers.binance.com/docs/derivatives/usds-margined-futures/).

namespace market_classifier::venues {
namespace {

using detail::Parse;
constexpr auto k_venue = domain::Venue::BinanceUsdM;

bool symbol_ok(Parse &p, const json::Value &obj, std::string_view key) {
    std::string_view s;
    if (!detail::read_string(p, obj, key, s)) {
        return false;
    }
    return s == instrument_for(k_venue).native_symbol || p.fail(AdapterError::WrongSymbol);
}

// `[["price","qty"], ...]` — qty 0 means removal in diffs.
bool read_levels(Parse &p, const json::Value &obj, std::string_view key,
                 std::vector<domain::BookLevel> &out) {
    const auto arr = obj.get(key);
    if (!arr || !arr->is_array()) {
        return p.fail(AdapterError::Malformed);
    }
    if (!detail::level_count_ok(arr->size())) {
        return p.fail(AdapterError::OutOfBounds);
    }
    out.reserve(arr->size());
    for (std::size_t i = 0; i < arr->size(); ++i) {
        const auto level = arr->at(i);
        const auto px    = level ? level->at(0) : std::nullopt;
        const auto qty   = level ? level->at(1) : std::nullopt;
        const auto price = px ? px->decimal() : std::nullopt;
        const auto quant = qty ? qty->decimal() : std::nullopt;
        if (!price || !quant) {
            return p.fail(AdapterError::Malformed);
        }
        if (!detail::positive(*price) || !detail::non_negative(*quant)) {
            return p.fail(AdapterError::OutOfBounds);
        }
        out.push_back({*price, *quant, std::nullopt});
    }
    return true;
}

class Frame {
  public:
    Frame(const bridge::RawFrame &frame, std::uint64_t &sequence)
        : frame_(frame), sequence_(sequence) {}

    AdapterResult ws(const json::Value &root) {
        std::string_view stream;
        const auto data = root.get("data");
        if (!detail::read_string(p_, root, "stream", stream) || !data || !data->is_object()) {
            return detail::failed(p_.error == AdapterError::None ? AdapterError::Malformed
                                                                 : p_.error);
        }
        if (stream == "btcusdt@aggTrade") {
            agg_trade(*data);
        } else if (stream == "btcusdt@depth@100ms") {
            depth_update(*data);
        } else if (stream == "btcusdt@bookTicker") {
            book_ticker(*data);
        } else if (stream == "btcusdt@markPrice@1s") {
            mark_price(*data);
        } else if (stream == "btcusdt@ticker") {
            ticker(*data);
        } else if (stream.starts_with("btcusdt@kline_")) {
            kline(*data);
        } else if (stream == "btcusdt@forceOrder") {
            force_order(*data);
        } else {
            p_.fail(AdapterError::UnknownStream);
        }
        return finish();
    }

    AdapterResult depth_snapshot(const json::Value &root) {
        domain::BookSnapshot s;
        std::int64_t t = frame_.receive_time_ms;
        if (const auto tv = root.get("T")) {
            if (const auto ti = tv->int64(); ti && *ti >= 0) {
                t = *ti;
            }
        }
        std::uint64_t last_update_id{};
        if (detail::read_u64(p_, root, "lastUpdateId", last_update_id) &&
            read_levels(p_, root, "bids", s.bids) && read_levels(p_, root, "asks", s.asks)) {
            if (auto meta = meta_for(t)) {
                s.meta            = *meta;
                s.source_sequence = last_update_id;
                detail::push(result_, std::move(s));
            }
        }
        return finish();
    }

    // Rows: [openTime, o, h, l, c, v, closeTime, quoteVol, trades, ...]. The newest row
    // may still be open, so every row but the last is closed.
    AdapterResult klines_rest(const json::Value &root) {
        if (!root.is_array() || root.size() == 0) {
            p_.fail(AdapterError::Malformed);
            return finish();
        }
        if (root.size() > bridge::k_max_raw_frame_bytes / 64) {
            p_.fail(AdapterError::OutOfBounds);
            return finish();
        }
        for (std::size_t i = 0; i < root.size() && p_.error == AdapterError::None; ++i) {
            const auto row = root.at(i);
            if (!row || !row->is_array() || row->size() < 9) {
                p_.fail(AdapterError::Malformed);
                break;
            }
            const auto cell_dec = [&](std::size_t k) {
                const auto c = row->at(k);
                return c ? c->decimal() : std::nullopt;
            };
            const auto cell_int = [&](std::size_t k) {
                const auto c = row->at(k);
                return c ? c->int64() : std::nullopt;
            };
            const auto open_time = cell_int(0), close_time = cell_int(6), trades = cell_int(8);
            const auto o = cell_dec(1), h = cell_dec(2), l = cell_dec(3), c = cell_dec(4),
                       v = cell_dec(5), q = cell_dec(7);
            if (!open_time || !close_time || !trades || !o || !h || !l || !c || !v || !q) {
                p_.fail(AdapterError::Malformed);
                break;
            }
            domain::Candle candle{};
            candle.interval_ms   = *close_time + 1 - *open_time;
            candle.open_time_ms  = *open_time;
            candle.close_time_ms = *close_time;
            candle.open = *o, candle.high = *h, candle.low = *l, candle.close = *c;
            candle.base_volume  = *v;
            candle.quote_volume = *q;
            candle.closed       = i + 1 < root.size();
            if (*trades < 0 || !valid_candle(candle)) {
                p_.fail(AdapterError::OutOfBounds);
                break;
            }
            candle.trade_count = static_cast<std::uint64_t>(*trades);
            if (auto meta = meta_for(*open_time)) {
                candle.meta = *meta;
                detail::push(result_, std::move(candle));
            }
        }
        return finish();
    }

    AdapterResult open_interest(const json::Value &root) {
        domain::OpenInterest oi{};
        std::int64_t t{};
        if (symbol_ok(p_, root, "symbol") &&
            detail::read_quantity(p_, root, "openInterest", oi.native_quantity) &&
            detail::read_time(p_, root, "time", t)) {
            oi.sample_interval_ms = k_binance_oi_poll_ms;
            if (auto meta = meta_for(t)) {
                oi.meta = *meta;
                detail::push(result_, std::move(oi));
            }
        }
        return finish();
    }

    AdapterResult exchange_info(const json::Value &root) {
        const auto symbols = root.get("symbols");
        if (!symbols || !symbols->is_array()) {
            p_.fail(AdapterError::Malformed);
            return finish();
        }
        // The bridge trims to BTCUSDT; still search so an untrimmed payload works.
        for (std::size_t i = 0; i < symbols->size(); ++i) {
            const auto sym  = symbols->at(i);
            const auto name = sym ? sym->get("symbol") : std::nullopt;
            if (!name || name->string() != instrument_for(k_venue).native_symbol) {
                continue;
            }
            definition(*sym);
            return finish();
        }
        p_.fail(AdapterError::WrongSymbol);
        return finish();
    }

  private:
    static bool valid_candle(const domain::Candle &c) {
        return c.interval_ms > 0 && detail::positive(c.open) && detail::positive(c.high) &&
               detail::positive(c.low) && detail::positive(c.close) &&
               detail::non_negative(c.base_volume) && c.low <= c.high;
    }

    std::optional<domain::EventMeta> meta_for(std::int64_t source_time) {
        return detail::make_meta(p_, k_venue, source_time, frame_.receive_time_ms, sequence_);
    }

    AdapterResult finish() {
        if (p_.error != AdapterError::None) {
            return detail::failed(p_.error);
        }
        return std::move(result_);
    }

    void agg_trade(const json::Value &d) {
        domain::Trade t{};
        std::int64_t time{};
        std::uint64_t id{};
        const auto maker = d.get("m");
        const auto m     = maker ? maker->boolean() : std::nullopt;
        if (!symbol_ok(p_, d, "s") || !detail::read_u64(p_, d, "a", id) ||
            !detail::read_price(p_, d, "p", t.price) ||
            !detail::read_decimal(p_, d, "q", t.quantity) || !detail::read_time(p_, d, "T", time)) {
            return;
        }
        if (!m) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        if (!detail::positive(t.quantity)) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        // Buyer is maker => the seller crossed the spread (aggressor Sell).
        t.aggressor_side    = *m ? domain::AggressorSide::Sell : domain::AggressorSide::Buy;
        t.source_id         = detail::to_text(id);
        const auto notional = domain::mul(t.price, t.quantity);
        if (!notional) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        t.usd_notional = notional.value; // USDT treated as USD for notional
        if (auto meta = meta_for(time)) {
            t.meta = *meta;
            detail::push(result_, std::move(t));
        }
    }

    void depth_update(const json::Value &d) {
        domain::BookDelta delta{};
        std::int64_t time{};
        std::uint64_t first{}, last{}, prev{};
        if (!symbol_ok(p_, d, "s") || !detail::read_time(p_, d, "T", time) ||
            !detail::read_u64(p_, d, "U", first) || !detail::read_u64(p_, d, "u", last) ||
            !detail::read_u64(p_, d, "pu", prev) || !read_levels(p_, d, "b", delta.changed_bids) ||
            !read_levels(p_, d, "a", delta.changed_asks)) {
            return;
        }
        if (first > last) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        if (auto meta = meta_for(time)) {
            delta.meta                  = *meta;
            delta.first_source_sequence = first;
            delta.last_source_sequence  = last;
            result_.events.emplace_back(std::move(delta));
            result_.binance_prev_final_update_ids.push_back(prev);
        }
    }

    void book_ticker(const json::Value &d) {
        domain::Bbo b{};
        std::int64_t time{};
        if (!symbol_ok(p_, d, "s") || !detail::read_price(p_, d, "b", b.bid_price) ||
            !detail::read_quantity(p_, d, "B", b.bid_quantity) ||
            !detail::read_price(p_, d, "a", b.ask_price) ||
            !detail::read_quantity(p_, d, "A", b.ask_quantity) ||
            !detail::read_time(p_, d, "T", time)) {
            return;
        }
        if (auto meta = meta_for(time)) {
            b.meta = *meta;
            detail::push(result_, std::move(b));
        }
    }

    void mark_price(const json::Value &d) {
        domain::AssetMetrics m{};
        domain::Decimal index{};
        std::int64_t time{}, next{};
        if (!symbol_ok(p_, d, "s") || !detail::read_price(p_, d, "p", m.mark_price) ||
            !detail::read_price(p_, d, "i", index) ||
            !detail::read_decimal(p_, d, "r", m.funding_rate) ||
            !detail::read_time(p_, d, "T", next) || !detail::read_time(p_, d, "E", time)) {
            return;
        }
        m.index_price          = index;
        m.next_funding_time_ms = next;
        if (auto meta = meta_for(time)) {
            m.meta = *meta;
            detail::push(result_, std::move(m));
        }
    }

    // change_24h is the absolute price change `p`; volume_24h is quote (USD) volume `q`
    // so it is comparable with Hyperliquid's notional `dayNtlVlm`.
    void ticker(const json::Value &d) {
        domain::MarketSummary s{};
        domain::Decimal last{}, change{}, volume{};
        std::int64_t time{};
        if (!symbol_ok(p_, d, "s") || !detail::read_price(p_, d, "c", last) ||
            !detail::read_decimal(p_, d, "p", change) ||
            !detail::read_quantity(p_, d, "q", volume) || !detail::read_time(p_, d, "E", time)) {
            return;
        }
        s.last_price = last;
        s.change_24h = change;
        s.volume_24h = volume;
        if (auto meta = meta_for(time)) {
            s.meta = *meta;
            detail::push(result_, std::move(s));
        }
    }

    void kline(const json::Value &d) {
        const auto k = d.get("k");
        std::int64_t time{};
        if (!symbol_ok(p_, d, "s") || !detail::read_time(p_, d, "E", time)) {
            return;
        }
        if (!k || !k->is_object()) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        domain::Candle c{};
        std::string_view interval;
        std::int64_t trades{};
        domain::Decimal quote{};
        const auto closed = k->get("x");
        const auto x      = closed ? closed->boolean() : std::nullopt;
        if (!detail::read_string(p_, *k, "i", interval) ||
            !detail::read_time(p_, *k, "t", c.open_time_ms) ||
            !detail::read_time(p_, *k, "T", c.close_time_ms) ||
            !detail::read_decimal(p_, *k, "o", c.open) ||
            !detail::read_decimal(p_, *k, "h", c.high) ||
            !detail::read_decimal(p_, *k, "l", c.low) ||
            !detail::read_decimal(p_, *k, "c", c.close) ||
            !detail::read_decimal(p_, *k, "v", c.base_volume) ||
            !detail::read_decimal(p_, *k, "q", quote) || !detail::read_int(p_, *k, "n", trades)) {
            return;
        }
        const auto ms = interval_ms(interval);
        if (!ms) {
            p_.fail(AdapterError::UnknownStream);
            return;
        }
        if (!x) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        c.interval_ms  = *ms;
        c.quote_volume = quote;
        c.closed       = *x;
        if (trades < 0 || !valid_candle(c)) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        c.trade_count = static_cast<std::uint64_t>(trades);
        if (auto meta = meta_for(time)) {
            c.meta = *meta;
            detail::push(result_, std::move(c));
        }
    }

    // Binance pushes at most the latest liquidation per symbol per 1000 ms, so this feed
    // is a sample, not a complete record (docs/protocols/binance.md).
    void force_order(const json::Value &d) {
        const auto o = d.get("o");
        if (!o || !o->is_object()) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        domain::Liquidation l{};
        std::string_view side;
        std::int64_t time{};
        if (!symbol_ok(p_, *o, "s") || !detail::read_string(p_, *o, "S", side) ||
            !detail::read_price(p_, *o, "ap", l.price) ||
            !detail::read_decimal(p_, *o, "z", l.quantity) ||
            !detail::read_time(p_, *o, "T", time)) {
            return;
        }
        // A forced SELL closes a long position; a forced BUY closes a short.
        if (side == "SELL") {
            l.side = domain::LiquidationSide::Long;
        } else if (side == "BUY") {
            l.side = domain::LiquidationSide::Short;
        } else {
            p_.fail(AdapterError::Malformed);
            return;
        }
        if (!detail::positive(l.quantity)) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        const auto notional = domain::mul(l.price, l.quantity);
        if (!notional) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        l.notional  = notional.value;
        l.method    = domain::LiquidationMethod::ForceOrder;
        l.source_id = std::to_string(time) + (side == "SELL" ? "-S" : "-B");
        if (auto meta = meta_for(time)) {
            l.meta = *meta;
            detail::push(result_, std::move(l));
        }
    }

    void definition(const json::Value &sym) {
        domain::InstrumentDefinition def{};
        std::string_view contract, status, base, quote;
        const auto filters = sym.get("filters");
        if (!detail::read_string(p_, sym, "contractType", contract) ||
            !detail::read_string(p_, sym, "status", status) ||
            !detail::read_string(p_, sym, "baseAsset", base) ||
            !detail::read_string(p_, sym, "quoteAsset", quote)) {
            return;
        }
        if (!filters || !filters->is_array()) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        bool tick = false, step = false;
        for (std::size_t i = 0; i < filters->size(); ++i) {
            const auto f    = filters->at(i);
            const auto type = f ? f->get("filterType") : std::nullopt;
            const auto name = type ? type->string() : std::nullopt;
            if (name == "PRICE_FILTER") {
                tick = detail::read_price(p_, *f, "tickSize", def.tick_size);
            } else if (name == "LOT_SIZE") {
                step = detail::read_price(p_, *f, "stepSize", def.quantity_step);
            }
        }
        if (!tick || !step || contract != "PERPETUAL" ||
            base.size() > domain::InstrumentId::k_max_symbol_length ||
            quote.size() > domain::InstrumentId::k_max_symbol_length) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        def.contract_kind = domain::ContractKind::LinearPerpetual;
        def.base_asset    = std::string(base);
        def.quote_asset   = std::string(quote);
        def.active        = status == "TRADING";
        if (auto meta = meta_for(frame_.receive_time_ms)) {
            def.meta = *meta;
            detail::push(result_, std::move(def));
        }
    }

    const bridge::RawFrame &frame_;
    std::uint64_t &sequence_;
    Parse p_;
    AdapterResult result_;
};

} // namespace

AdapterResult BinanceAdapter::adapt(const bridge::RawFrame &frame) {
    const auto doc = detail::parse_frame(frame);
    if (!doc) {
        return detail::failed(frame.tag == StreamTag::BinanceWs ||
                                      frame.tag == StreamTag::BinanceDepthSnapshot ||
                                      frame.tag == StreamTag::BinanceKlinesRest ||
                                      frame.tag == StreamTag::BinanceOpenInterestRest ||
                                      frame.tag == StreamTag::BinanceExchangeInfo
                                  ? AdapterError::Malformed
                                  : AdapterError::UnknownStream);
    }
    Frame f(frame, next_sequence_);
    const auto root = doc->root();
    switch (frame.tag) {
    case StreamTag::BinanceWs:
        return f.ws(root);
    case StreamTag::BinanceDepthSnapshot:
        return f.depth_snapshot(root);
    case StreamTag::BinanceKlinesRest:
        return f.klines_rest(root);
    case StreamTag::BinanceOpenInterestRest:
        return f.open_interest(root);
    case StreamTag::BinanceExchangeInfo:
        return f.exchange_info(root);
    default:
        return detail::failed(AdapterError::UnknownStream);
    }
}

} // namespace market_classifier::venues
