#include "market_classifier/venues/hyperliquid_adapter.hpp"

#include "adapter_common.hpp"

// Field meanings: docs/protocols/hyperliquid.md (verified 2026-09-27 against
// https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/).

namespace market_classifier::venues {
namespace {

using detail::Parse;
constexpr auto k_venue = domain::Venue::Hyperliquid;
// Perp prices allow at most 5 significant figures and at most (6 - szDecimals)
// decimals; integer prices are always allowed.
constexpr int k_max_sig_figs      = 5;
constexpr int k_perp_max_decimals = 6;

bool coin_ok(Parse &p, const json::Value &obj, std::string_view key) {
    std::string_view s;
    if (!detail::read_string(p, obj, key, s)) {
        return false;
    }
    return s == instrument_for(k_venue).native_symbol || p.fail(AdapterError::WrongSymbol);
}

// `{px, sz, n}` levels; bounded count.
bool read_levels(Parse &p, const json::Value &arr, std::vector<domain::BookLevel> &out) {
    if (!arr.is_array()) {
        return p.fail(AdapterError::Malformed);
    }
    if (!detail::level_count_ok(arr.size())) {
        return p.fail(AdapterError::OutOfBounds);
    }
    out.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const auto level = arr.at(i);
        domain::BookLevel l{};
        std::int64_t n{};
        if (!level || !detail::read_price(p, *level, "px", l.price) ||
            !detail::read_quantity(p, *level, "sz", l.quantity) ||
            !detail::read_int(p, *level, "n", n)) {
            return p.error != AdapterError::None || p.fail(AdapterError::Malformed);
        }
        if (n < 0 || n > 0xFFFFFFFFLL) {
            return p.fail(AdapterError::OutOfBounds);
        }
        l.order_count = static_cast<std::uint32_t>(n);
        out.push_back(l);
    }
    return true;
}

// Number of digits left of the decimal point (at least 1).
int integer_digits(const domain::Decimal &d) {
    auto m = d.mantissa() < 0 ? -d.mantissa() : d.mantissa();
    for (int i = 0; i < d.scale(); ++i) {
        m /= 10;
    }
    int digits = 1;
    while (m >= 10) {
        m /= 10;
        ++digits;
    }
    return digits;
}

domain::Decimal pow10_decimal(int exponent) {
    if (exponent >= 0) {
        std::int64_t m = 1;
        for (int i = 0; i < exponent; ++i) {
            m *= 10;
        }
        return domain::Decimal::from_parts(m, 0).value;
    }
    return domain::Decimal::from_parts(1, static_cast<std::uint8_t>(-exponent)).value;
}

class Frame {
  public:
    Frame(const bridge::RawFrame &frame, std::uint64_t &sequence)
        : frame_(&frame), sequence_(&sequence) {}

    AdapterResult ws(const json::Value &root) {
        std::string_view channel;
        if (!detail::read_string(p_, root, "channel", channel)) {
            return finish();
        }
        if (channel == "subscriptionResponse" || channel == "pong") {
            return detail::failed(AdapterError::Ignored);
        }
        const auto data = root.get("data");
        if (!data) {
            p_.fail(AdapterError::Malformed);
            return finish();
        }
        if (channel == "trades") {
            trades(*data);
        } else if (channel == "l2Book") {
            l2_book(*data);
        } else if (channel == "bbo") {
            bbo(*data);
        } else if (channel == "activeAssetCtx") {
            asset_ctx(*data);
        } else if (channel == "candle") {
            candle(*data, frame_->receive_time_ms, false);
        } else if (channel == "error") {
            p_.fail(AdapterError::Malformed);
        } else {
            p_.fail(AdapterError::UnknownStream);
        }
        return finish();
    }

    AdapterResult candle_snapshot(const json::Value &root) {
        if (!root.is_array() || root.size() == 0) {
            p_.fail(AdapterError::Malformed);
            return finish();
        }
        for (std::size_t i = 0; i < root.size() && p_.error == AdapterError::None; ++i) {
            const auto c = root.at(i);
            if (!c) {
                p_.fail(AdapterError::Malformed);
                break;
            }
            // Snapshot is ordered oldest first; only the newest bar may be open.
            candle(*c, frame_->receive_time_ms, i + 1 < root.size());
        }
        return finish();
    }

    // Response: [meta, assetCtxs]; universe[i] pairs with assetCtxs[i].
    AdapterResult meta(const json::Value &root) {
        const auto meta     = root.at(0);
        const auto ctxs     = root.at(1);
        const auto universe = meta ? meta->get("universe") : std::nullopt;
        if (!universe || !universe->is_array() || !ctxs || !ctxs->is_array()) {
            p_.fail(AdapterError::Malformed);
            return finish();
        }
        for (std::size_t i = 0; i < universe->size(); ++i) {
            const auto u    = universe->at(i);
            const auto name = u ? u->get("name") : std::nullopt;
            if (!name || name->string() != instrument_for(k_venue).native_symbol) {
                continue;
            }
            const auto ctx = ctxs->at(i);
            if (!ctx) {
                p_.fail(AdapterError::Malformed);
                return finish();
            }
            definition(*u, *ctx);
            return finish();
        }
        p_.fail(AdapterError::WrongSymbol);
        return finish();
    }

  private:
    std::optional<domain::EventMeta> meta_for(std::int64_t source_time) {
        return detail::make_meta(p_, k_venue, source_time, frame_->receive_time_ms, *sequence_);
    }

    AdapterResult finish() {
        if (p_.error != AdapterError::None) {
            return detail::failed(p_.error);
        }
        return std::move(result_);
    }

    void trades(const json::Value &data) {
        if (!data.is_array() || data.size() == 0) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        if (data.size() > runtime::k_max_events_per_batch) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        for (std::size_t i = 0; i < data.size() && p_.error == AdapterError::None; ++i) {
            const auto item = data.at(i);
            domain::Trade t{};
            std::string_view side;
            std::int64_t time{};
            std::uint64_t tid{};
            if (!item || !coin_ok(p_, *item, "coin") ||
                !detail::read_string(p_, *item, "side", side) ||
                !detail::read_price(p_, *item, "px", t.price) ||
                !detail::read_decimal(p_, *item, "sz", t.quantity) ||
                !detail::read_time(p_, *item, "time", time) ||
                !detail::read_u64(p_, *item, "tid", tid)) {
                p_.fail(AdapterError::Malformed);
                return;
            }
            // Documented: side is the aggressor ("B" bid/buy, "A" ask/sell).
            if (side == "B") {
                t.aggressor_side = domain::AggressorSide::Buy;
            } else if (side == "A") {
                t.aggressor_side = domain::AggressorSide::Sell;
            } else {
                p_.fail(AdapterError::Malformed);
                return;
            }
            if (!detail::positive(t.quantity)) {
                p_.fail(AdapterError::OutOfBounds);
                return;
            }
            const auto notional = domain::mul(t.price, t.quantity);
            if (!notional) {
                p_.fail(AdapterError::OutOfBounds);
                return;
            }
            t.usd_notional = notional.value; // USDC treated as USD
            t.source_id    = detail::to_text(tid);
            if (auto meta = meta_for(time)) {
                t.meta = *meta;
                detail::push(result_, std::move(t));
            }
        }
    }

    void l2_book(const json::Value &data) {
        domain::BookSnapshot s{};
        std::int64_t time{};
        const auto levels = data.get("levels");
        if (!coin_ok(p_, data, "coin") || !detail::read_time(p_, data, "time", time)) {
            return;
        }
        const auto bids = levels ? levels->at(0) : std::nullopt;
        const auto asks = levels ? levels->at(1) : std::nullopt;
        if (!bids || !asks || levels->size() != 2) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        if (!read_levels(p_, *bids, s.bids) || !read_levels(p_, *asks, s.asks)) {
            return;
        }
        if (auto meta = meta_for(time)) {
            s.meta = *meta;
            detail::push(result_, std::move(s));
        }
    }

    void bbo(const json::Value &data) {
        std::int64_t time{};
        const auto pair = data.get("bbo");
        if (!coin_ok(p_, data, "coin") || !detail::read_time(p_, data, "time", time)) {
            return;
        }
        const auto bid = pair ? pair->at(0) : std::nullopt;
        const auto ask = pair ? pair->at(1) : std::nullopt;
        if (!bid || !ask) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        // An empty side is valid upstream but cannot form a Bbo.
        if (bid->is_null() || ask->is_null()) {
            p_.fail(AdapterError::Ignored);
            return;
        }
        domain::Bbo b{};
        if (!detail::read_price(p_, *bid, "px", b.bid_price) ||
            !detail::read_quantity(p_, *bid, "sz", b.bid_quantity) ||
            !detail::read_price(p_, *ask, "px", b.ask_price) ||
            !detail::read_quantity(p_, *ask, "sz", b.ask_quantity)) {
            return;
        }
        if (auto meta = meta_for(time)) {
            b.meta = *meta;
            detail::push(result_, std::move(b));
        }
    }

    // activeAssetCtx carries no timestamp; receive time is used as source time.
    void asset_ctx(const json::Value &data) {
        const auto ctx = data.get("ctx");
        if (!coin_ok(p_, data, "coin")) {
            return;
        }
        if (!ctx || !ctx->is_object()) {
            p_.fail(AdapterError::Malformed);
            return;
        }
        domain::Decimal mark{};
        domain::Decimal oracle{};
        domain::Decimal funding{};
        domain::Decimal oi{};
        domain::Decimal volume{};
        domain::Decimal prev{};
        domain::Decimal mid{};
        if (!detail::read_price(p_, *ctx, "markPx", mark) ||
            !detail::read_price(p_, *ctx, "oraclePx", oracle) ||
            !detail::read_decimal(p_, *ctx, "funding", funding) ||
            !detail::read_quantity(p_, *ctx, "openInterest", oi) ||
            !detail::read_quantity(p_, *ctx, "dayNtlVlm", volume) ||
            !detail::read_price(p_, *ctx, "prevDayPx", prev)) {
            return;
        }
        // midPx is null when a book side is empty.
        std::optional<domain::Decimal> mid_price;
        if (const auto m = ctx->get("midPx"); m && !m->is_null()) {
            if (!detail::read_price(p_, *ctx, "midPx", mid)) {
                return;
            }
            mid_price = mid;
        }
        const auto notional = domain::mul(oi, mark);
        const auto change   = domain::sub(mark, prev);
        if (!notional || !change) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        const auto t = frame_->receive_time_ms;
        auto m1      = meta_for(t);
        auto m2      = meta_for(t);
        auto m3      = meta_for(t);
        if (!m1 || !m2 || !m3) {
            return;
        }
        domain::AssetMetrics metrics{};
        metrics.meta         = *m1;
        metrics.mark_price   = mark;
        metrics.oracle_price = oracle;
        metrics.funding_rate = funding;
        detail::push(result_, std::move(metrics));

        domain::OpenInterest open_interest{};
        open_interest.meta               = *m2;
        open_interest.native_quantity    = oi;
        open_interest.usd_notional       = notional.value;
        open_interest.sample_interval_ms = 0; // streamed, not polled
        detail::push(result_, std::move(open_interest));

        // change_24h = markPx - prevDayPx (Hyperliquid publishes no last trade price here).
        domain::MarketSummary summary{};
        summary.meta          = *m3;
        summary.mid_price     = mid_price;
        summary.change_24h    = change.value;
        summary.volume_24h    = volume;
        summary.open_interest = oi;
        summary.funding_rate  = funding;
        detail::push(result_, std::move(summary));
    }

    void candle(const json::Value &c, std::int64_t source_time, bool closed) {
        domain::Candle out{};
        std::string_view interval;
        std::int64_t trades{};
        if (!coin_ok(p_, c, "s") || !detail::read_string(p_, c, "i", interval) ||
            !detail::read_time(p_, c, "t", out.open_time_ms) ||
            !detail::read_time(p_, c, "T", out.close_time_ms) ||
            !detail::read_price(p_, c, "o", out.open) ||
            !detail::read_price(p_, c, "h", out.high) || !detail::read_price(p_, c, "l", out.low) ||
            !detail::read_price(p_, c, "c", out.close) ||
            !detail::read_quantity(p_, c, "v", out.base_volume) ||
            !detail::read_int(p_, c, "n", trades)) {
            return;
        }
        const auto ms = interval_ms(interval);
        if (!ms) {
            p_.fail(AdapterError::UnknownStream);
            return;
        }
        if (trades < 0 || out.low > out.high || out.close_time_ms < out.open_time_ms) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        out.interval_ms = *ms;
        out.trade_count = static_cast<std::uint64_t>(trades);
        // Live frames have no closed flag; a bar is closed once its end has passed.
        out.closed = closed || out.close_time_ms < frame_->receive_time_ms;
        if (auto meta = meta_for(source_time)) {
            out.meta = *meta;
            detail::push(result_, std::move(out));
        }
    }

    void definition(const json::Value &universe, const json::Value &ctx) {
        std::int64_t sz_decimals{};
        domain::Decimal mark{};
        if (!detail::read_int(p_, universe, "szDecimals", sz_decimals) ||
            !detail::read_price(p_, ctx, "markPx", mark)) {
            return;
        }
        if (sz_decimals < 0 || sz_decimals > k_perp_max_decimals) {
            p_.fail(AdapterError::OutOfBounds);
            return;
        }
        domain::InstrumentDefinition def{};
        // Tick at the current price: coarser of the significant-figure and decimal limits.
        const int sig_exponent = integer_digits(mark) - k_max_sig_figs;
        const int dec_exponent = -(k_perp_max_decimals - static_cast<int>(sz_decimals));
        def.tick_size = pow10_decimal(sig_exponent > dec_exponent ? sig_exponent : dec_exponent);
        def.quantity_step   = pow10_decimal(-static_cast<int>(sz_decimals));
        def.contract_kind   = domain::ContractKind::LinearPerpetual;
        def.base_asset      = "BTC";
        def.quote_asset     = "USDC";
        const auto delisted = universe.get("isDelisted");
        def.active          = !(delisted && delisted->boolean() == true);
        if (auto meta = meta_for(frame_->receive_time_ms)) {
            def.meta = *meta;
            detail::push(result_, std::move(def));
        }
    }

    const bridge::RawFrame *frame_; // borrowed for one adapt() call
    std::uint64_t *sequence_;
    Parse p_;
    AdapterResult result_;
};

} // namespace

AdapterResult HyperliquidAdapter::adapt(const bridge::RawFrame &frame) {
    const auto doc = detail::parse_frame(frame);
    if (!doc) {
        return detail::failed(AdapterError::Malformed);
    }
    Frame f(frame, next_sequence_);
    switch (frame.tag) {
    case StreamTag::HyperliquidWs:
        return f.ws(doc->root());
    case StreamTag::HyperliquidCandleSnapshot:
        return f.candle_snapshot(doc->root());
    case StreamTag::HyperliquidMeta:
        return f.meta(doc->root());
    default:
        return detail::failed(AdapterError::UnknownStream);
    }
}

} // namespace market_classifier::venues
