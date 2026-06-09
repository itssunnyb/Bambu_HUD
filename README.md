# Bambu HUD

A standalone ESP32 touchscreen dashboard for Bambu Lab 3D printers.

Bambu HUD provides real-time printer monitoring on dedicated touchscreen displays without requiring a phone, tablet, or computer. It connects directly to your printer over your local network using Bambu Lab's MQTT interface and displays live print information, temperatures, progress, AMS status, and more.

---

## Features

### Printer Monitoring

* Real-time print progress
* Printer status display
* Nozzle temperature
* Bed temperature
* Chamber temperature
* Remaining print time
* Estimated completion time
* Current layer information
* Active print file name

### AMS Support

* AMS slot monitoring
* Active filament indication
* Custom filament color assignment
* AMS status display

### Multi-Printer Support

* Save multiple printers
* Switch between printers from the setup portal
* Compatible with multiple Bambu printer models

### Touchscreen Interface

* Optimized touchscreen UI
* Multiple color themes
* Multiple display styles
* Screen rotation support
* Power-saving options

### Connectivity

* Built-in WiFi setup portal
* WiFi network scanning
* Browser-based firmware installation
* OTA firmware updates
* NTP clock synchronization

### Power Features

* Battery monitoring (4" HUD version)
* Battery percentage display
* Low battery warning
* Adjustable screen dimming
* Adjustable display timeout

---

## Supported Hardware

## Tested Hardware

The following displays have been tested and are officially supported.

### 4" ESP32 Touchscreen HUD

Recommended for the best experience.

**Features**

* 480x320 resolution
* Touchscreen support
* Battery monitoring
* Portable operation
* Full-size dashboard layout

**Tested Hardware**

https://www.amazon.com/dp/B0BVFXR313

---

### 2.8" ESP32-2432S028 (CYD)

Compact and low-cost version of the HUD.

**Features**

* 320x240 resolution
* Touchscreen support
* USB powered
* Compact dashboard layout

**Tested Hardware**

https://www.amazon.com/dp/B0BVFXR313

---

## Browser Installer

Choose the installer that matches your display.

### 4" HUD Firmware

Designed for:

* 480x320 touchscreen displays
* Battery-powered HUD units

### 2.8" CYD Firmware

Designed for:

* ESP32-2432S028
* Cheap Yellow Display (CYD)
* 320x240 touchscreen displays

---

## Hardware Compatibility

### Officially Tested

✅ 4" ESP32 Touchscreen Display

✅ ESP32-2432S028 (CYD)

### Planned Testing

🔲 3.5" ESP32 Touchscreen Displays

🔲 Additional ESP32 display variants

If you successfully run Bambu HUD on other hardware, please open an issue and let us know.


---

## Supported Printers

### Tested

* Bambu Lab P1S
* Bambu Lab P1P

### Community Testing

* Bambu Lab X1 Carbon
* Bambu Lab A1
* Bambu Lab A1 Mini

---

## Installation

### Browser Installer

Open the installer page:

```text
https://itssunnyb.github.io/Bambu_HUD/install.html
```

Select your display type:

* Install 4" HUD
* Install 2.8" CYD HUD

Connect your ESP32 via USB and follow the prompts.

---

## Initial Setup

After installation:

1. Power on the device.
2. Connect to:

```text
SSID: BambuHUD_Setup
Password: bambuhud
```

3. Open:

```text
http://192.168.4.1
```

4. Configure:

   * WiFi Network
   * WiFi Password
   * Printer Name
   * Printer IP Address
   * Access Code
   * Printer Serial Number

5. Save settings.

The device will restart and connect automatically.

---

## Finding Your Printer Information

### Printer IP Address

Find your printer's IP address from:

```text
Bambu Studio
Devices
Network
```

### Access Code

Find your access code from:

```text
Printer Settings
LAN Mode
Access Code
```

### Printer Serial Number

Find your serial number from:

```text
Printer Settings
Device Information
```

---

## Screen Features

### Dashboard

* Print status
* Progress percentage
* Temperatures
* AMS information
* Print file name
* Clock
* Battery status (4" only)

### Settings

* Theme selection
* Display style selection
* Screen rotation
* Power saving options
* AMS color customization

### Statistics

* Printer uptime
* Connection information
* Additional printer details

---

## Themes

Included themes:

* Green
* Blue
* Carbon
* Matrix
* OLED
* Red
* Orange

---

## Power Saving

Options include:

* Disabled
* Dim Screen
* Screen Off

Timeouts:

* Never
* 1 Minute
* 5 Minutes
* 10 Minutes
* 15 Minutes
* 30 Minutes
* 60 Minutes

---

## Troubleshooting

### Installer Cannot Find Device

Use:

* Google Chrome
* Microsoft Edge

Ensure:

* USB cable supports data
* Correct COM port is selected

### Cannot Connect To Printer

Verify:

* Printer IP address
* Access code
* Serial number
* LAN mode enabled
* Printer and HUD on same network

### Touchscreen Not Working

Confirm:

* Correct firmware installed
* Correct display version selected during installation

---

## Project Status

Current Version:

```text
v0.1.0
```

This is the first public release and is actively under development.

---

## License

MIT License

---

## Credits

Created by Sunny Leonidas

Special thanks to the Bambu Lab community and everyone helping test the project.
