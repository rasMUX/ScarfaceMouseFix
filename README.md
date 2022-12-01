# Scarface Mouse Fix

Mouse Fix aims to make Scarface feel like you aren't emulating a joystick with your mouse.

Requires an asi loader / SilentPatch.

## Features

* Replaces the way the game handles mouse input, eliminating negative acceleration while autotargeting and the map remain unchanged
* Added the option to change refresh rate
* Fixed animations being linked to framerate

## Installation

Place the .asi file into the games main directory.

## Config

Shares the settings.ini with SilentPatch, automatically adds missing entries with default values on runtime.

Default settings are meant to be equivalent to source engine with sensitivity 1.

```ini
[MouseFix]
DegreesPerCount=0.022000
Sensitivity=1.000000
InvertMouse=0
RefreshRate=60
```

## Known Issues

* Custom refresh rates cause recoil to reset faster, increase recoil and other bugs.

https://user-images.githubusercontent.com/91014605/205215284-bb80cd13-21c5-4950-ad87-433a3aed7471.mp4
