# app_event_loop

This is used to attach a default event loop handler that can be used to handle all possible RMNG events. i.e.,

```c
/* Bare minimum includes */
#include "esp_rmaker_core.h" // RMNG stack
#include "app_event_loop.h" // this header

/* Register the handler before initialization, so we can capture RMAKER_EVENT_INIT_DONE */
esp_rmaker_error_t err = app_event_loop_register_default_handler();

/* ...Then initialize the node */
esp_rmaker_config_t rainmaker_cfg = {
    .enable_time_sync = true,
};
esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Light", "light");
```

Refer to [esp_rmaker_event_loop.h](../../../components/esp_rmaker_neo/include/esp_rmaker_event_loop.h) for possible events.
