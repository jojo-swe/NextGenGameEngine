#include "engine/core/events/event_system.h"

namespace nge::events {

EventBus& GetGlobalEventBus() {
    static EventBus s_bus;
    return s_bus;
}

} // namespace nge::events
