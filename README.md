# Moonlight - Apple TV

A fork of [moonlight-ios](https://github.com/moonlight-stream/moonlight-ios) that adds a few streaming options to the **tvOS** app. It is not affiliated with or endorsed by the Moonlight project, and it is licensed under the GPLv3 like upstream. The original README follows below. Disclaimer: this project is vibecoded

## What this fork changes (tvOS only)

* **YUV 4:4:4.** A new *Enable YUV 4:4:4* setting (off by default). The app only offers 4:4:4 when the host supports it and the Apple TV decodes that profile on a hardware decoder. The Apple TV 4K (2nd generation, A12) was tested and does. Turn on the stats overlay to confirm the stream is *HEVC (4:4:4)*.
* **Bitrate up to 500 Mbps** (upstream tops out at 150 Mbps).
* **Timestamp Paced (Experimental)** frame pacing, next to *Lowest Latency* and *Smoothest Video*. It uses the host's frame timestamps to restore the host's frame rhythm. The other two modes are unchanged.
* **Decoder probe.** Launching the app with `-DecoderProbe` prints which H.264 and HEVC formats the device decodes in hardware, then exits.
* The app is named *Moonlight 4:4:4* with a red icon. The bundle identifier in this repo is a placeholder (`com.example.moonlight-apple-tv`); set your own so the app installs next to the App Store version.

AV1 works as upstream, which needs a hardware AV1 decoder. The Apple TV 4K 2nd generation doesn't have one.

## Building this fork

Follow the build steps below, and set your own signing team and a unique bundle identifier for the **Moonlight TV** target. An app installed with a free Apple ID stops launching after 7 days, and you reinstall it from Xcode to renew it.

---

# Moonlight iOS/tvOS

[![CI](https://github.com/moonlight-stream/moonlight-ios/actions/workflows/ci.yml/badge.svg)](https://github.com/moonlight-stream/moonlight-ios/actions/workflows/ci.yml)

[Moonlight for iOS/tvOS](https://moonlight-stream.org) is an open source client for [Sunshine](https://github.com/LizardByte/Sunshine) and NVIDIA GameStream. Moonlight for iOS/tvOS allows you to stream your full collection of games and apps from your powerful desktop computer to your iOS device or Apple TV.

Moonlight also has a [PC client](https://github.com/moonlight-stream/moonlight-qt) and [Android client](https://github.com/moonlight-stream/moonlight-android).

Check out [the Moonlight wiki](https://github.com/moonlight-stream/moonlight-docs/wiki) for more detailed project information, setup guide, or troubleshooting steps.

[![Moonlight for iOS and tvOS](https://moonlight-stream.org/images/App_Store_Badge_135x40.svg)](https://apps.apple.com/us/app/moonlight-game-streaming/id1000551566)

## Building
* Install Xcode from the [App Store page](https://apps.apple.com/us/app/xcode/id497799835)
* Run `git clone --recursive https://github.com/moonlight-stream/moonlight-ios.git`
  *  If you've already clone the repo without `--recursive`, run `git submodule update --init --recursive`
* Open Moonlight.xcodeproj in Xcode
* To run on a real device, you will need to locally modify the signing options:
    * Click on "Moonlight" at the top of the left sidebar
    * Click on the "Signing & Capabilities" tab
    * Under "Targets", select "Moonlight" (for iOS/iPadOS) or "Moonlight TV" (for tvOS)
    * In the "Team" dropdown, select your name. If your name doesn't appear, you may need to sign into Xcode with your Apple account.
    * Change the "Bundle Identifier" to something different. You can add your name or some random letters to make it unique.
    * Now you can select your Apple device in the top bar as a target and click the Play button to run.
