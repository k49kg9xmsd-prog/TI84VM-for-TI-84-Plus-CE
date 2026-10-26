# TI84VM beta

TI84VM is an experimental Flash App for the TI-84 Plus CE. It recreates parts of the calculator interface and adds customization and plugin support.

The idea was inspired by [Nyxian](https://github.com/emexlab/Nyxian) and its virtual jailbreak concept for iOS. TI84VM was developed by me in collaboration with GPT.

> [!NOTE]
> TI84VM is a beta. It is not a replacement for TI-OS or a complete recreation of its interface.

## Features

### Appearance settings

- Draw separate battery icons for normal and charging states with the built-in pixel editor.
- Choose a color for the top status bar.

### Plugins

TI84VM detects compatible AppVar plugins. New plugins start disabled. You can enable, disable, or delete them in the plugin manager.

Plugins can add Home screen text, create an APPS page, assign supported hotkeys, and change the top bar color. The current plugin format uses AppVar data and supported hooks; it cannot run arbitrary native code.

## Compatibility

The underlying App was tested on a **TI-84 Plus CE Python Edition running OS 5.8.1**. The public beta build was checked in CEmu with OS 5.8.1, but has not yet been retested on a physical calculator. Other models and OS versions have not been verified.

Some pages have incomplete behavior, and program compatibility is not guaranteed.

## Installation

Download `TI84VM-Beta.zip` from this repository's **Releases** page.

1. Back up your calculator files.
2. If an older TI84VM App is installed, delete it from the calculator's native **MEM → Apps** menu.
3. Install arTIfiCE and the CE C libraries (`clibs.8xg`) if you do not already have them.
4. Transfer all five files from the same ZIP:
   - `TVMINST.8xp`
   - `TVM9A0.8xv`
   - `TVM9A1.8xv`
   - `TVM9A2.8xv`
   - `TVMSPRT.8xv`
5. Run `TVMINST` through arTIfiCE. Wait for **“Installed + VERIFIED.”** before opening TI84VM.

Do not mix installer files from different releases. After installation, you may remove `TVMINST` and `TVM9A0`–`TVM9A2`, but **keep `TVMSPRT`** because the App needs it to start.

## Uninstall

Delete the **TI84VM App** from the native **MEM → Apps** menu. Then, in **MEM → AppVars**, delete `TVMSPRT` and any TI84VM data you want removed:

`TVMBOOT`, `TVMSET`, `TVMBATT`, `TVMCHRG`, `TVMBAR`, `TVMPLCFG`, `TVMPCF2`, `TVMGTMP`

You can also delete leftover installer files (`TVMINST` and `TVM9A0`–`TVM9A2`) and any example plugins you installed, such as `TVMPOWER` or `TVMDEMO`.

## Source code and feedback

The C/eZ80 source is available in `TI84VM-Beta-Source.zip` under **Releases**.

Bug reports are welcome. Please include your calculator model, OS version, steps to reproduce, and a photo or screenshot if possible. **Do not upload your ROM or personal calculator identifiers.**

---

TI84VM is an independent community project and is not affiliated with Texas Instruments.
