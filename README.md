# aerox16-laptop-wmi

This driver is strictly for the Gigabyte AERO X16 laptop. 
It is not supported on any other hardware.

## Installation

### Prerequisites
Copy `aerox16-laptop.conf` to `/etc/modules-load.d/` to ensure the kernel module loads automatically on boot.

### Manual Installation
If you have Secure Boot enabled, you must sign the kernel module after compilation.

To install the driver:
```
make all
sudo make install
```
*Note: This will automatically compile, copy, and load the module.*

### Removal
To remove the DKMS version:
```
# Replace <version> with your driver version (use "dkms status" to check)
sudo dkms remove aerox16-laptop/<version> --all
```

To remove the manually installed module:
```
sudo make uninstall
```

## Usage

All nodes are exposed at: `/sys/devices/platform/aerox16_laptop/`

### Fan Modes
Aero X16 laptops support six fan modes (0-5):
0. Normal
1. Silent
2. Gaming
3. Custom
4. Auto
5. Fixed

Modes 4 and 5 enable custom mode automatically. Modes 0-2 will disable custom mode.
Set the mode via: `/sys/devices/platform/aerox16_laptop/fan_mode`
Example: `echo '2' | sudo tee /sys/devices/platform/aerox16_laptop/fan_mode`

### Custom Fan Speed
Accepted range is 0-255. Only works in Auto or Fixed mode.
Node: `/sys/devices/platform/aerox16_laptop/fan_custom_speed`
Example: `echo '128' | sudo tee /sys/devices/platform/aerox16-laptop/fan_custom_speed`

### Charging Control
- **Charge Mode**: 0 (Normal), 1 (Custom). Node: `/sys/devices/platform/aerox16_laptop/charge_mode`
- **Charge Limit**: Only works in Custom mode. Accepts 60-100. Node: `/sys/devices/platform/aerox16_laptop/charge_limit`
Example: `echo '80' | sudo tee /sys/devices/platform/aerox16-laptop/charge_limit`

### Fan Curve
Custom fan curves are supported in Custom mode.
- `fan_curve_index`: Specify which index (0-254) to modify. Only 15 indices are modifiable.
- `fan_curve_data`: Holds temperature (C) and fan speed.
- Calculation: `(fan_speed * 256) + temperature`.
Example (Index 2, half speed at 55C):
`echo '2' | sudo tee /sys/devices/platform/aerox16_laptop/fan_curve_index`
`echo '32567' | sudo tee /sys/devices/platform/aerox16_laptop/fan_curve_data`

### Other Controls
- **Battery Cycle**: Read-only. Node: `/sys/devices/platform/aerox16_laptop/battery_cycle`
- **GPU Boost**: Mode 1 enables boost. Node: `/sys/devices/platform/aerox16_laptop/gpu_boost`
- **USB Toggles**: S3/S4 power output (Read-only). Nodes: `usb_charge_s3_toggle`, `usb_charge_s4_toggle`
- **Power On Time**: Read-only. Node: `/sys/devices/platform/aerox16_laptop/power_on_time`
- **Light Sensor**: Read-only. Node: `/sys/devices/platform/aerox16_laptop/light_sensor`

### HWMON Nodes
- **Fans**: `fanX_input` (Read-only, RPM). Ch 1: CPU, Ch 2: GPU, Ch 3/4: Fans 3/4 (if equipped).
- **PWM**: `pwmX` and `pwmX_enable` (Read-only). Ch 1: CPU, Ch 2: GPU.
- **Temperature**: `tempX_input` (Read-only, Celsius * 1000). Ch 1: CPU, Ch 2: GPU, Ch 3: Motherboard.

### File Structure
The following control items are available:
```
/sys/devices/platform/aerox16_laptop/
├── battery_cycle
├── battery_led
├── charge_limit
├── charge_mode
├── debug_method
├── driver_override
├── fan_curve_data
├── fan_curve_index
├── fan_custom_speed
├── fan_mode
├── fan_pwm
├── fn_lock
├── gpu_boost
├── hwmon
│  └── hwmon10
│    ├── fan1_input
│    ├── fan2_input
│    ├── fan3_input
│    ├── fan4_input
│    ├── name
│    ├── power
│    │  ├── async
│    │  ├── autosuspend_delay_ms
│    │  ├── control
│    │  ├── runtime_active_kids
│    │  ├── runtime_active_time
│    │  ├── runtime_enabled
│    │  ├── runtime_status
│    │  ├── runtime_suspended_time
│    │  └── runtime_usage
│    ├── pwm1
│    ├── pwm1_enable
│    ├── pwm2
│    ├── pwm2_enable
│    ├── temp1_input
│    ├── temp2_input
│    ├── temp3_input
│    └── uevent
├── keyboard_auto
├── keyboard_brightness
├── light_sensor
├── modalias
├── mute
├── power
│  ├── async
│  ├── autosuspend_delay_ms
│  ├── control
│  ├── runtime_active_kids
│  ├── runtime_active_time
│  ├── runtime_enabled
│  ├── runtime_status
│  ├── runtime_suspended_time
│  └── runtime_usage
├── power_led
├── power_on_time
├── uevent
├── usb_charge_s3_toggle
└── usb_charge_s4_toggle
```
