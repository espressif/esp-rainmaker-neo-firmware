#!/usr/bin/env bash
# Generate the ESP Launchpad site metadata from an already-staged firmware tree.
#
# Expects the layout produced by build_launchpad.sh (merged across all targets):
#   $SITE_DIR/<app>/<target>/<app>-merged.bin
#   $SITE_DIR/<app>/LAUNCHPAD.md
#   $SITE_DIR/_meta/<app>.factory_offset
#   $SITE_DIR/_meta/idf.txt
#
# Writes:
#   $SITE_DIR/launchpad.toml   Launchpad config (flashConfigURL target)
#   $SITE_DIR/README.md       rendered by the Launchpad UI via config_readme_url
# and removes $SITE_DIR/_meta so it is not published.
#
# Inputs (env):
#   SITE_DIR        required, root of the staged tree
#   PAGES_BASE_URL  required, public URL of that root, WITH trailing slash
#   REPO_SLUG       optional, owner/repo — used to link the SDK commit
#   COMMIT_SHA      optional, SDK commit built
#   REF_NAME        optional, branch or tag built
#
# Sample launchpad config: https://github.com/espressif/esp-launchpad/blob/main/config/config.toml

set -euo pipefail

: "${SITE_DIR:?SITE_DIR is not set}"
: "${PAGES_BASE_URL:?PAGES_BASE_URL is not set}"
REPO_SLUG="${REPO_SLUG:-}"
COMMIT_SHA="${COMMIT_SHA:-}"
REF_NAME="${REF_NAME:-}"

# Companion phone apps. Used both in the README and, per app, as Launchpad's
# ios_app_url / android_app_url keys, which the UI renders as app store badges.
IOS_APP_URL="https://apps.apple.com/us/app/esp-rainmaker-home/id1563728960"
ANDROID_APP_URL="https://play.google.com/store/apps/details?id=com.espressif.novahome"

# Launchpad resolves every image.* value RELATIVE to firmware_images_url, so it must
# end in a slash or the last path component is dropped.
case "$PAGES_BASE_URL" in
  */) ;;
  *) PAGES_BASE_URL="$PAGES_BASE_URL/" ;;
esac

META_DIR="$SITE_DIR/_meta"
TOML="$SITE_DIR/launchpad.toml"
README="$SITE_DIR/README.md"

# Launchpad expects the marketing chip name, esptool the lowercase target id.
to_chipset() {
  case "$1" in
    esp32)   echo "ESP32" ;;
    esp32s2) echo "ESP32-S2" ;;
    esp32s3) echo "ESP32-S3" ;;
    esp32c2) echo "ESP32-C2" ;;
    esp32c3) echo "ESP32-C3" ;;
    esp32c5) echo "ESP32-C5" ;;
    esp32c6) echo "ESP32-C6" ;;
    esp32h2) echo "ESP32-H2" ;;
    *) echo "ESP32-$(echo "${1#esp32}" | tr 'a-z' 'A-Z')" ;;
  esac
}
to_image_key() {
  case "$1" in
    esp32) echo "esp32" ;;
    *)     echo "esp32-$(echo "${1#esp32}" | tr 'A-Z' 'a-z')" ;;
  esac
}

# Apps = staged directories holding at least one merged binary.
APPS=()
for app_dir in "$SITE_DIR"/*/; do
  [ -d "$app_dir" ] || continue
  app=$(basename "$app_dir")
  [ "$app" = "_meta" ] && continue
  # shellcheck disable=SC2012
  [ -n "$(ls "$app_dir"*/*-merged.bin 2>/dev/null)" ] || continue
  APPS+=("$app")
done

if [ ${#APPS[@]} -eq 0 ]; then
  echo "ERROR: no staged applications found under $SITE_DIR." >&2
  exit 1
fi

# --- README.md ---------------------------------------------------------------
IDF_VERSION_STR="unknown"
IDF_COMMIT=""
if [ -f "$META_DIR/idf.txt" ]; then
  IDF_VERSION_STR=$(sed -n '1p' "$META_DIR/idf.txt")
  IDF_COMMIT=$(sed -n '2p' "$META_DIR/idf.txt")
fi
if [ -n "$IDF_COMMIT" ]; then
  IDF_SHORT=$(printf '%s' "$IDF_COMMIT" | cut -c1-8)
  IDF_VERSION_MD="\`${IDF_VERSION_STR}\` ([${IDF_SHORT}](https://github.com/espressif/esp-idf/tree/${IDF_COMMIT}))"
else
  IDF_VERSION_MD="\`${IDF_VERSION_STR}\`"
fi

if [ -n "$REPO_SLUG" ] && [ -n "$COMMIT_SHA" ]; then
  SDK_COMMIT_MD="[\`$(printf '%s' "$COMMIT_SHA" | cut -c1-8)\`](https://github.com/${REPO_SLUG}/tree/${COMMIT_SHA})"
else
  SDK_COMMIT_MD="\`${COMMIT_SHA:-unknown}\`"
fi

{
  echo "## Getting Started"
  echo ""
  echo "### Public ESP RainMaker Neo deployment (default)"
  echo ""
  echo "These builds use the default configuration, which has **assisted claiming**"
  echo "enabled alongside BLE provisioning. The node obtains its cloud credentials"
  echo "over the provisioning session, so there is nothing to flash beyond the example"
  echo "itself:"
  echo ""
  echo "1. Flash the example."
  echo "2. Open the **ESP RainMaker Home** app and provision the device."
  echo ""
  echo "### Private or self-hosted deployments"
  echo ""
  echo "Assisted claiming does not apply — credentials come from a **factory partition**"
  echo "that you flash yourself, at that example's factory (\`fctry\`) partition offset."
  echo "The offset is example-specific, so use the value listed below for the example you"
  echo "are flashing."
  echo ""
  echo "1. Obtain a factory partition binary from your deployment's dashboard."
  echo "2. In ESP Launchpad, open the **DIY** section."
  echo "3. (Recommended) Erase the connected device's flash first."
  echo "4. Add the factory partition file and set its flash offset to the example's"
  echo "   offset from the table below."
  echo "5. Flash the factory partition."
  echo "6. Flash the desired example."
  echo ""
  echo "Example-specific factory offsets:"
  echo ""
  for app in "${APPS[@]}"; do
    off_file="$META_DIR/${app}.factory_offset"
    if [ -f "$off_file" ]; then
      off=$(head -n1 "$off_file")
      # Normalise to 0x + uppercase hex for consistency with the partition CSVs.
      off_disp=$(printf '0x%X' "$off" 2>/dev/null) || off_disp="$off"
      echo "- \`$app\` — \`$off_disp\`"
    else
      echo "- \`$app\` — _(no factory partition)_"
    fi
  done
  echo ""
  echo "## Phone Application"
  echo ""
  echo "### Global region"
  echo ""
  echo "For rest of the world, phone applications can be downloaded from the respective stores."
  echo ""
  echo "- For iOS application - ${IOS_APP_URL}"
  echo "- For Android application - ${ANDROID_APP_URL}"
  echo ""
  echo "### China region"
  echo ""
  echo "There is currently no support yet."
  echo ""
  echo "## Build Configuration"
  echo ""
  echo "- **Ref:** \`${REF_NAME:-unknown}\`"
  echo "- **SDK commit:** ${SDK_COMMIT_MD}"
  echo "- **IDF version:** ${IDF_VERSION_MD}"
} > "$README"

# --- launchpad.toml -----------------------------------------------------------
supported_apps=""
for app in "${APPS[@]}"; do
  [ -n "$supported_apps" ] && supported_apps="$supported_apps, "
  supported_apps="${supported_apps}\"RainMaker_Neo_${app}\""
done

{
  echo "esp_toml_version = 1.0"
  echo ""
  echo "firmware_images_url = \"${PAGES_BASE_URL}\""
  echo "config_readme_url = \"${PAGES_BASE_URL}README.md\""
  echo ""
  echo "supported_apps = [$supported_apps]"
  echo ""
  for app in "${APPS[@]}"; do
    # The Launchpad app identifier (TOML section + supported_apps entry, shown in the
    # UI) is DECOUPLED from the published folder, which stays the bare example name:
    # image.* / readme.text keep pointing at <app>/...
    echo "[RainMaker_Neo_${app}]"
    chipsets=""
    for target_dir in "$SITE_DIR/$app"/*/; do
      [ -d "$target_dir" ] || continue
      target=$(basename "$target_dir")
      # shellcheck disable=SC2012
      [ -n "$(ls "$target_dir"*-merged.bin 2>/dev/null)" ] || continue
      cs=$(to_chipset "$target")
      [ -n "$chipsets" ] && chipsets="$chipsets, "
      chipsets="${chipsets}\"$cs\""
    done
    echo "chipsets = [$chipsets]"
    for target_dir in "$SITE_DIR/$app"/*/; do
      [ -d "$target_dir" ] || continue
      target=$(basename "$target_dir")
      # shellcheck disable=SC2012
      bin_path=$(ls "$target_dir"*-merged.bin 2>/dev/null | head -n1)
      [ -n "$bin_path" ] || continue
      echo "image.$(to_image_key "$target") = \"$app/$target/$(basename "$bin_path")\""
    done
    # Rendered as app store badges in the Launchpad UI. Per app, not global.
    echo "ios_app_url = \"${IOS_APP_URL}\""
    echo "android_app_url = \"${ANDROID_APP_URL}\""
    # readme.text is fetched DIRECTLY by Launchpad (it does NOT prepend
    # firmware_images_url, unlike image.*), so it must be a full public URL.
    if [ -f "$SITE_DIR/$app/LAUNCHPAD.md" ]; then
      echo "readme.text = \"${PAGES_BASE_URL}${app}/LAUNCHPAD.md\""
    fi
    echo ""
  done
} > "$TOML"

rm -rf "$META_DIR"

echo "============================================================"
echo "Launchpad site staged in $SITE_DIR"
echo "  apps:     ${APPS[*]}"
echo "  images:   $(find "$SITE_DIR" -name '*-merged.bin' | wc -l | tr -d ' ')"
echo "  toml:     ${PAGES_BASE_URL}launchpad.toml"
echo "  launchpad: https://espressif.github.io/esp-launchpad/?flashConfigURL=${PAGES_BASE_URL}launchpad.toml"
echo "============================================================"
cat "$TOML"
