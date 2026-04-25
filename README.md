
# M5Cardputer LED Hat Controller

This project transforms your **M5Stack Cardputer** into a remote control terminal for Bluetooth LED Name Badges (often used as LED hats or scrolling name tags). It features a custom keyboard interface, a real-time text preview, and two additional visual modes: a functional NTP/Uptime Clock and a "Predator" style countdown.


---

## 🚀 Features

- **BLE Text Transmission:** Type messages directly on the Cardputer and send them to your LED Hat over Bluetooth.
    
- **Customization:** Cycle through 5 different colors (Red, Green, Blue, Yellow, White) and 3 scroll modes (Fixed, Scroll-Left, Scroll-Right).
    
    
- **Real-time Preview:** A rolling ticker on the Cardputer screen shows exactly what your text looks like before you send it.
    
    
- **Clock Mode:** Displays time via NTP (if WiFi is configured) or system uptime using a stylized 7-segment display.
    
    
- **Predator Mode:** A visual easter egg featuring shifting alien glyphs and a "Final Countdown" display.
    
    
- **SD Config:** Load your badge's MAC address and WiFi credentials automatically from an SD card.
    
    

---

## 🛠️ Installation & Setup

### 1. Hardware Requirements

- M5Stack Cardputer
    
- Compatible BLE LED Hat (Uses the common `d44bc439` service UUID)
    
- MicroSD Card (formatted to FAT32)
    

### 2. SD Card Configuration (`config.txt`)

Create a file named `config.txt` in the root directory of your SD card. This file allows the Cardputer to find your badge and sync the time.

**Required Format:**

Plaintext

```
MAC=xx:xx:xx:xx:xx:xx
SSID=Your_WiFi_Name
PASS=Your_WiFi_Password
TZ=CST6CDT,M3.2.0,M11.1.0
```

- **MAC:** The Bluetooth MAC address of your LED Hat.
    
    
- **SSID/PASS:** Your WiFi credentials for NTP time syncing (optional).
    
    
- **TZ:** Your timezone string (default is Central Time).
    
    

### 3. Arduino IDE Settings

To avoid "Sketch too big" errors, use the following board settings:

- **Board:** M5Stack Cardputer
    
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`
    

---

## ⌨️ Controls

### Navigation

- **`TAB`**: Cycle between **BLE Mode**, **Clock Mode**, and **Predator Mode**.
    

### BLE / Text Mode

- **Type Characters**: Add text to the input buffer (up to 40 characters).
    
- **`ENTER`**: Connect and send current text and settings to the badge.
    
- **`BACKSPACE`**: Delete the last character.
    
- **`` ` `` (Backtick)**: Clear the entire text buffer.
    
- **`/`**: Cycle colors forward.
    
- **`,`**: Cycle colors backward.
    
- **`;`**: Cycle scroll modes forward (Fixed -> Scroll-R -> Scroll-L).
    
- **`.`**: Cycle scroll modes backward.
    

---

## 🧩 Technical Details

The communication uses **AES-128 ECB encryption** to handshake with the LED badge hardware. Text is converted into a column-based bitmask using a custom 5x8 font, which is then bundled with RGB color data and uploaded in 98-byte chunks over BLE
