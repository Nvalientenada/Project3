# ESC 218 – Project 3: Windshield Wiper Subystem 

## Team Members
- Nada Aloussi  
- Daniel Saxe 

---

## System Description
This project implements a model of a car ignition safety system combined with a windshield wiper control system using an ESP32 microcontroller.  
The ignition subsystem ensures that the engine can only start when the driver and passenger are seated and both seatbelts are fastened. If a start attempt is made when these conditions are not met, the engine is inhibited, a buzzer sounds, and descriptive messages are printed. Once the engine is running, it remains on even if seatbelts are removed or seats are vacated, and the ignition button can be used to turn the engine off.

The windshield wiper subsystem operates only when the engine is running and allows the user to select OFF, INTERMITTENT, LOW, or HIGH speed using a potentiometer. In intermittent mode, the wiper performs a single sweep and pauses for a short, medium, or long delay before repeating. A servo motor models the wiper motion, and a 16x2 LCD displays the engine status and current wiper mode in real time.

---

## Design Alternatives and Choices

Several design alternatives were considered during development:

- **Single vs. multiple potentiometers:**  
  We chose to use a single potentiometer to control both the wiper mode and intermittent delay. This simplified the hardware design and reduced the number of required ADC inputs while still meeting all functional requirements.

- **Servo motor vs. DC motor:**  
  A servo motor was selected instead of a DC motor because it allows precise positioning and makes it easier to model a realistic windshield wiper sweep that always returns to a parked position.

- **LCD vs. serial output only:**  
  While serial output was useful for debugging, a 16x2 LCD was chosen to provide clear, user-facing feedback so the system can be understood and operated without a computer connection.

- **Polling vs. interrupts:**  
  Button inputs were handled using polling with a short loop delay. This approach was simpler to implement and sufficient for the responsiveness required in this project.

---

## How the Timing and Motion Settings Work 

- **Main loop update rate:** The program updates about every **10 ms** (LOOP_MS = 10). This makes the system responsive to button presses and knob changes.
- **Buzzer duration:** If ignition is inhibited, the buzzer is turned on for about **0.5 seconds** (BUZZ_MS = 500 ms).
- **Servo control signal:** The servo is controlled using PWM at **50 Hz** (typical servo frequency). The code changes the PWM “duty value” to move the servo.
- **Wiper motion range:** The wiper sweeps between **0° (parked)** and about **90°** to model a realistic wiper movement.
- **Low vs High speed:** The servo moves in small steps every loop:
  - **LOW** mode uses a smaller step (LO_STEP = 2), so it sweeps more slowly.
  - **HIGH** mode uses a larger step (HI_STEP = 5), so it sweeps faster.
- **Intermittent (INT) delays:** In INT mode, the wiper does one sweep and then pauses at 0°. The pause time depends on where the knob is within the INT region:
  - **SHORT ≈ 1 second**
  - **MED ≈ 3 seconds**
  - **LONG ≈ 5 seconds**
- **Potentiometer mode selection:** The knob is read as an ADC value from **0 to 4095**, and that range is split into four regions:
  - **OFF**, **INT**, **LOW**, **HIGH**
  This makes it easy for the user to select a mode by turning the knob.

---

## Testing Results Summary

### Ignition Subsystem

| Specification | Test Process | Results |
|--------------|-------------|---------|
| Green LED turns ON only when all safety conditions are met | Tested all combinations of driver seat, passenger seat, and seatbelts | Passed. Green LED only turned ON when all four conditions were active. |
| Ignition is inhibited when safety conditions are not met | Pressed ignition button with one or more safety inputs inactive | Passed. Engine did not start, buzzer sounded, and error messages were printed. |
| System allows retry after inhibited start | Attempted ignition while unsafe, corrected conditions, retried | Passed. Engine started normally after conditions were met. |
| Engine starts when ignition is pressed and system is ready | All safety inputs active, ignition button pressed | Passed. Yellow LED turned ON and engine state updated. |
| Engine remains running if seats or belts are released after start | Engine started, then safety buttons released | Passed. Engine continued running as specified. |
| Pressing ignition button while engine is running turns engine OFF | Engine running, ignition button pressed again | Passed. Engine turned OFF and yellow LED turned OFF. |

---

### Windshield Wiper Subsystem

| Specification | Test Process | Results |
|--------------|-------------|---------|
| Wipers do not operate when engine is OFF | Engine off, potentiometer moved through all modes | Passed. Servo remained parked at 0°. |
| OFF mode parks wipers at 0° | Engine on, potentiometer set to OFF | Passed. Servo returned to and remained at 0°. |
| LOW mode sweeps continuously at slow speed | Engine on, potentiometer set to LOW | Passed. Servo swept continuously at low speed. |
| HIGH mode sweeps continuously at fast speed | Engine on, potentiometer set to HIGH | Passed. Servo swept continuously at higher speed. |
| INTERMITTENT mode performs single sweep then pauses | Engine on, potentiometer set to INTERMITTENT | Passed. Servo swept once and paused at 0°. |
| Intermittent delay changes with knob position | Tested short, medium, and long delay regions | Passed. Pause duration matched selected delay. |
| Wipers return to parked position when engine is turned OFF | Engine on with active wipers, ignition turned off | Passed. Servo returned to 0° and stopped. |

---

## Notes
- All buttons are wired as active-low inputs using internal pull-up resistors.
- The potentiometer is read using an ADC channel and mapped to wiper modes and delay settings.
- The servo motor is controlled using the ESP32 LEDC PWM peripheral.
- Timing and delays are implemented using FreeRTOS task delays.
- The LCD provides real-time user feedback without requiring a serial connection.
