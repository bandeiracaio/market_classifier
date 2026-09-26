#include "market_classifier/runtime/read_model.hpp"

#include <variant>

namespace market_classifier::runtime {

void drain_ingress(BoundedIngress &ingress, DummyReadModel &model) {
    while (auto batch = ingress.consume_next()) {
        for (const auto &event : batch->events) {
            ++model.total_events;
            ++model.event_counts[event.index()];
            std::visit(
                [&model](const auto &value) {
                    model.last_local_sequence  = value.meta.local_sequence().value;
                    model.last_source_time_ms  = value.meta.source_time().value;
                    model.last_receive_time_ms = value.meta.receive_time().value;
                    if (!model.has_data ||
                        domain::can_transition(model.quality, value.meta.quality())) {
                        model.quality = value.meta.quality();
                    }
                    model.has_data = true;
                },
                event);
        }
    }

    const auto status      = ingress.status();
    model.ingress_counters = status.counters;
    if (status.quality != domain::DataQuality::Live) {
        model.quality = status.quality;
    }
}

} // namespace market_classifier::runtime
