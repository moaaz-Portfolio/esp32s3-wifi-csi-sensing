# Browser Firmware Installer

The ESP32-S3 firmware is built by GitHub Actions. The workflow generates the ESP Web Tools manifest from ESP-IDF's `flasher_args.json`, so flash addresses are taken from the actual build rather than hard-coded.

## Build and publish

Push to `activity-recognition` or run **Build ESP32-S3 firmware and publish browser flasher** manually from the Actions tab. Pull requests build the firmware but do not publish the Pages site.

## Enable GitHub Pages once

In repository **Settings → Pages → Build and deployment**, select **GitHub Actions** as the source. The workflow then publishes the site after a successful firmware build.

## Install from a browser

Open the GitHub Pages URL in Chrome or Edge on a computer, connect the ESP32-S3 using a USB data cable, click **Install**, select the serial device, and confirm flashing.

The browser installer uses ESP Web Tools and requires HTTPS plus Web Serial support. Firmware binaries and the generated manifest are published together as one Pages artifact.
