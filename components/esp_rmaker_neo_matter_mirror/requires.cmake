set(RMNG_MATTER_MIRROR_NAME esp_rmaker_neo_matter_mirror)
set(RMNG_MATTER_MIRROR_REQUIRES "esp_rmaker_neo")
# esp_matter is added as a PRIV require on the ESP-IDF path only (see CMakeLists.txt); the POSIX build compiles the
# platform-neutral engine alone (tested against a fake port).
set(RMNG_MATTER_MIRROR_PRIV_REQUIRES "")
