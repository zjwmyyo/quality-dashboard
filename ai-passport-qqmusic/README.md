# AI Passport QQ Music player

[简体中文](README.zh_CN.md)

This folder contains the companion server and buildable firmware source for the FoloToy AI Passport QQ Music player. The public firmware uses first-boot setup and contains no personal Wi-Fi settings or QQ Music login data.

## Start the companion server

Run this on a computer or small server that remains on the same local network as AI Passport:

```sh
cd server
docker compose up -d --build
```

Docker installs the needed audio converter. Without Docker, install Node.js 20+ and ffmpeg, then run `npm install` and `npm start` in `server`. The service listens on port 3200. Keep it private to your local network; the QR login and stream endpoints are intended for one household account.

Open `http://localhost:3200` on the server computer to scan the login QR code with the QQ Music app. Find the computer's local IP address for device setup.

## Install and set up the firmware

Install the merged firmware from the FoloToy project page. On first boot the device displays a `Passport-Music-XXXX` hotspot and its device-specific password. Connect a phone to that hotspot and visit `http://192.168.4.1/`. Enter the 2.4 GHz Wi-Fi credentials and the companion server URL, such as `http://192.168.1.100:3200` using your computer's actual local IP. The device restarts after saving.

If Wi-Fi fails, the setup hotspot reappears. The computer must stay awake and the server must keep running for playback.

The current player loads up to 60 tracks per playlist. QQ Music account rights and track availability still apply. This community project is not affiliated with QQ Music.

## Build from source

The firmware needs ESP-IDF 5.5.3. From the `firmware` directory:

```sh
idf.py build
idf.py merge-bin -o AI-Passport-QQMusic-full.bin
```

The merged image is written at offset `0x0`. Flashing can erase device settings. The source includes the FoloToy board-support files under their MIT license and the generated Korean font under its bundled Noto Sans CJK license.

Never commit `server/data`, `firmware/sdkconfig`, build outputs, Wi-Fi passwords, or QQ Music tokens.
