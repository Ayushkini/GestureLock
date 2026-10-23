# Project Overview

## Objective
Build a portable gesture-controlled lock prototype that validates a predefined hand-motion sequence and reports status locally.

## Functional blocks
1. **Sensing:** MPU6050 measures acceleration and angular motion.
2. **Controller:** ESP32 reads sensor values and classifies gestures using the sketch's logic.
3. **Sequence validation:** Recognized gestures are compared with `LEFT → RIGHT → FORWARD`.
4. **Feedback:** LCD communicates state; buzzer signals success or failure.
5. **Local monitoring:** ESP32 runs an access point and web server for a browser dashboard.
6. **Power:** USB power bank can supply the ESP32 after firmware upload.

## Data flow

```mermaid
flowchart TD
    A([Start]) --> B[Initialize ESP32, MPU6050, LCD and buzzer]
    B --> C[Start Wi-Fi AP and web server]
    C --> D[Set state to LOCKED]
    D --> E[Read MPU6050]
    E --> F[Detect gesture]
    F --> G[Update sequence progress]
    G --> H{Sequence correct?}
    H -- No --> I[Failure feedback and reset sequence]
    I --> E
    H -- Yes --> J[Unlock and success feedback]
    J --> K[Keep unlocked for 5 seconds]
    K --> L[Lock again and clear sequence]
    L --> E
    G -. status .-> M[Local web dashboard]
    J -. status .-> M
    I -. status .-> M
```

## Scope and limitations
This is an educational prototype. Sensor thresholds need real-world tuning and the local dashboard is not a hardened security interface. Do not rely on it as a production lock.
