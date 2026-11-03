import time
import mido
import serial

# --- CONFIGURATION ---
# Replace with your board's serial port:
# Windows: 'COM3' (Check Device Manager for STM32 STLink Virtual COM Port)
# Linux/Mac: '/dev/ttyACM0' or '/dev/cu.usbmodem...'
SERIAL_PORT = "COM3"
BAUD_RATE = 115200
MIDI_FILE_PATH = "song.mid"  # Place your MIDI file in the same folder

# Mapping MIDI Pitch Classes to your 7 LEDs (C Major Scale: Do, Re, Mi, Fa, Sol, La, Si)
# MIDI Pitch Class (note % 12): 0=C, 2=D, 4=E, 5=F, 7=G, 9=A, 11=B
NOTE_TO_LED_INDEX = {
    0: 0,  # C  -> Do  (PC0)
    2: 1,  # D  -> Re  (PC1)
    4: 2,  # E  -> Mi  (PC2)
    5: 3,  # F  -> Fa  (PC3)
    7: 4,  # G  -> Sol (PC4)
    9: 5,  # A  -> La  (PC5)
    11: 6,  # B  -> Si  (PC6)
}


def main():
  print(f"Connecting to STM32 on {SERIAL_PORT} at {BAUD_RATE} baud...")
  try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print("Connected successfully!")
  except Exception as e:
    print(
        f"Error opening serial port: {e}\nMake sure to update SERIAL_PORT to your"
        " correct COM port."
    )
    return

  print(f"Loading MIDI file: {MIDI_FILE_PATH}...")
  try:
    mid = mido.MidiFile(MIDI_FILE_PATH)
  except Exception as e:
    print(f"Error loading MIDI file: {e}")
    ser.close()
    return

  print("Starting MIDI playback to STM32. Press Ctrl+C to stop.")

  try:
    # Iterate through all messages in the MIDI file with proper timing
    for msg in mid.play():
      # Wait for the delta time between messages
      if hasattr(msg, "time") and msg.time > 0:
        time.sleep(msg.time)

      # We care about 'note_on' and 'note_off' messages
      if msg.type == "note_on" or msg.type == "note_off":
        pitch_class = msg.note % 12

        # Check if the note belongs to our C Major scale mapping
        if pitch_class in NOTE_TO_LED_INDEX:
          led_index = NOTE_TO_LED_INDEX[pitch_class]
          channel = msg.channel if hasattr(msg, "channel") else 0
          # If it's a note_off message, velocity becomes 0
          velocity = msg.velocity if msg.type == "note_on" else 0

          # Pack data into 3 bytes: [Channel, Note Index (0-6), Velocity]
          packet = bytes([channel & 0xFF, led_index & 0xFF, velocity & 0xFF])

          # Send to STM32 via USART2
          ser.write(packet)
          print(
              f"Sent -> Channel: {channel}, Note Index: {led_index},"
              f" Velocity: {velocity}"
          )

  except KeyboardInterrupt:
    print("\nPlayback stopped by user.")
  finally:
    # Send all notes off before closing
    for i in range(7):
      ser.write(bytes([0, i, 0]))
    ser.close()
    print("Serial connection closed.")


if __name__ == "__main__":
  main()