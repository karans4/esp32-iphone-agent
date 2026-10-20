# 🛑 READ THIS FIRST 🛑

**Stop. Before touching anything in `hardware`, read [the Mimir READ_THIS_FIRST note](https://github.com/OdinFANN/personal-ai/blob/karan/mimir-phone-home/hardware/mimir-phone-agent/READ_THIS_FIRST.md).**

It says what actually works on branch `karan/mimir-phone-home` (much of it is built but never run on hardware),
how to set the Mac, the board and the iPhone up from zero, and exactly where to pick up next.

> TL;DR (phone-home branch): the board-side uplink only compiles; the home side was tested with a fake board.
> Odin's USB route works. The Bluetooth HID pointer works. Bluetooth keyboard, the Mac-over-Bluetooth
> command link on the BLE HID build, and the phone-home uplink are **not verified**. "Typing works but no pointer"
> = toggle AssistiveTouch off and on with the board connected.
