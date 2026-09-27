#include "market_classifier/processors/metrics.hpp"

#include "market_classifier/domain/decimal_math.hpp"

#include <algorithm>
#include <cfenv>
#include <cmath>

namespace market_classifier::processors {
namespace {

// Latest value per bucket: replaces the newest point in the same bucket, else appends.
template <typename Ring, typename Point> void upsert(Ring &ring, const Point &point) {
    if (!ring.empty() && ring.back().t_ms == point.t_ms) {
        ring.back() = point;
    } else if (ring.empty() || ring.back().t_ms < point.t_ms) {
        ring.push(point);
    }
}

const domain::Decimal k_half = domain::Decimal::from_parts(5, 1).value;

} // namespace

void Metrics::touch(std::int64_t t_ms) noexcept {
    view_.last_update_ms = std::max(view_.last_update_ms, t_ms);
}

void Metrics::on_asset_metrics(const domain::AssetMetrics &metrics) {
    const auto t = metrics.meta.source_time().value;
    touch(t);
    view_.asset = metrics;
    upsert(view_.funding, SeriesPoint{floor_to_ms(t, k_minute_ms), metrics.funding_rate});
}

void Metrics::on_open_interest(const domain::OpenInterest &oi) {
    const auto t = oi.meta.source_time().value;
    touch(t);
    view_.open_interest_last    = oi;
    view_.oi_sample_interval_ms = oi.sample_interval_ms;
    view_.oi_update_ms          = t;
    upsert(view_.open_interest, SeriesPoint{floor_to_ms(t, k_minute_ms), oi.native_quantity});
}

void Metrics::on_summary(const domain::MarketSummary &summary) {
    touch(summary.meta.source_time().value);
    view_.summary = summary;
}

void Metrics::on_bbo(const domain::Bbo &bbo) {
    const auto t = bbo.meta.source_time().value;
    touch(t);
    view_.bbo         = bbo;
    const auto sum    = domain::add(bbo.bid_price, bbo.ask_price);
    const auto mid    = sum ? domain::mul(sum.value, k_half) : sum;
    const auto spread = domain::sub(bbo.ask_price, bbo.bid_price);
    if (!mid || !spread) {
        view_.mid.reset();
        return;
    }
    view_.mid = mid.value;
    upsert(view_.spread, SpreadPoint{floor_to_ms(t, 1000), spread.value, mid.value});
}

double round_half_even_2dp(double value) noexcept {
    const int previous = std::fegetround();
    std::fesetround(FE_TONEAREST); // ties-to-even
    const double rounded = std::nearbyint(value * 100.0) / 100.0;
    std::fesetround(previous);
    return rounded;
}

std::optional<BasisView> cross_venue_basis(const Metrics &binance, const Metrics &hyperliquid) {
    const auto &b = binance.view();
    const auto &h = hyperliquid.view();
    if (!b.mid || !h.mid || h.mid->mantissa() == 0) {
        return std::nullopt;
    }
    const auto basis = domain::sub(*b.mid, *h.mid);
    if (!basis) {
        return std::nullopt;
    }
    BasisView view;
    view.binance_mid     = *b.mid;
    view.hyperliquid_mid = *h.mid;
    view.basis           = basis.value;
    // bps is presentation-only: double division, then half-even to 2 dp.
    view.basis_bps =
        round_half_even_2dp(domain::to_double(basis.value) / domain::to_double(*h.mid) * 10'000.0);
    view.last_update_ms = std::max(b.last_update_ms, h.last_update_ms);
    return view;
}

domain::Decimal annualized_funding(const domain::Decimal &rate, domain::Venue venue) {
    const auto periods =
        domain::Decimal::from_parts(venue == domain::Venue::BinanceUsdM ? 3 * 365 : 24 * 365, 0)
            .value;
    const auto r = domain::mul(rate, periods);
    return r ? r.value : domain::Decimal{};
}

std::int64_t next_hourly_funding_ms(std::int64_t now_ms) noexcept {
    constexpr std::int64_t hour = 3'600'000;
    return floor_to_ms(now_ms, hour) + hour;
}

} // namespace market_classifier::processors
