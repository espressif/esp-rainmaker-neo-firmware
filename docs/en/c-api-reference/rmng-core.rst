RainMaker Neo Core
##################

The ``esp_rmaker_neo`` component: node lifecycle, event delivery and the host/local
control surfaces.

Core
----
.. include-build-file:: inc/esp_rmaker_core.inc

Flow
----
.. include-build-file:: inc/esp_rmaker_flow.inc

Node
----
.. include-build-file:: inc/esp_rmaker_node.inc

State
-----
.. include-build-file:: inc/esp_rmaker_state.inc

Event Loop
----------
.. include-build-file:: inc/esp_rmaker_event_loop.inc

System Control
--------------
.. include-build-file:: inc/esp_rmaker_system_ctrl.inc

Console
-------
.. include-build-file:: inc/esp_rmaker_console.inc

Credentials Access
------------------
.. include-build-file:: inc/esp_rmaker_credentials_access.inc

Local Configuration Targets
---------------------------
.. include-build-file:: inc/esp_rmaker_local_config_targets.inc

Version
-------

``esp_rmaker_version.h`` gives an application the SDK version it is building
against. It is on the ``esp_rmaker_neo`` include path but is not in the repository: CMake
renders it into the build tree from
:project_file:`components/esp_rmaker_neo/include/versioning/esp_rmaker_version.h.in`,
using the numbers in
:project_file:`components/esp_rmaker_neo/versioning.cmake`.

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - Macro
     - Meaning
   * - ``ESP_RMAKER_VERSION_MAJOR``
     - Major version, an integer.
   * - ``ESP_RMAKER_VERSION_MINOR``
     - Minor version, an integer.
   * - ``ESP_RMAKER_VERSION_PATCH``
     - Patch version, an integer.
   * - ``ESP_RMAKER_VERSION_TYPE``
     - Build-type suffix. Empty when HEAD is tagged for exactly this version;
       otherwise ``-<git short SHA>``. A ``*`` is appended when the worktree is
       dirty.
   * - ``ESP_RMAKER_VERSION_STR``
     - The four above joined as ``"<major>.<minor>.<patch><type>"``, e.g.
       ``"0.8.0"`` for a release build or ``"0.8.0-0ccdca3*"`` from a dirty
       working tree. ``esp_rmaker_node_init()`` logs this at startup.

Because the header is generated, it carries no Doxygen documentation of its own.
Version-gate on the integers rather than parsing the string::

   #if ESP_RMAKER_VERSION_MAJOR > 0 || ESP_RMAKER_VERSION_MINOR >= 3
   /* API added in 0.3 */
   #endif
