# Tests Folder

This folder contains all test files for the project.


## Audio Files
These files were used to test the PDM microphone implementation. They contain a variety of drum beats and basslines that were selected to evaluate different aspects of the microphone's performance and signal processing capabilities.

The audio samples help verify functionality such as signal detection, frequency response, beat recognition, and overall audio input quality under different conditions.

## Captures
These capture files were recorded using a logic analyzer connected to a professional lighting console (ChamSys MQ60). They were collected to analyze and verify DMX signal timing, protocol behavior, and communication reliability.

Additional information about the captured files, test setup, and recording conditions can be found in the accompanying documentation:

[DMXTimings](./Captures/DMXTimings.md)

## DMX To LED Strip
This directory contains code intended to receive DMX data. The project was developed using Visual Studio Code with the Raspberry Pi Pico extension and uses CMake as its build system.

At its current stage, the implementation is not fully functional. Testing showed that the received DMX signal was not consistent enough for reliable DMX communication, preventing stable operation of the LED strip. The code is retained for reference and further development.

## DMX Test
This code is used to test the DMX transmit functionality for controlling a lighting fixture. It was initially developed as a standalone test and later reused during testing of the DMX receiver board to verify overall system functionality.

The implementation has been successfully tested with a DMX-controlled fixture and confirmed to work as expected, providing a reliable reference for both sending and validating DMX signals.

## PSoc6
This code contains a test implementation for receiving PDM audio and converting it to PCM format. It was used to validate audio input functionality on the PSoC6 platform and to verify correct handling of PDM microphone data streams.

The test focuses on ensuring stable audio capture and correct conversion from the PDM signal into usable PCM audio data for further processing.

## Read DMX
This code uses the PlatformIO extension to test DMX reception. The implementation was successful and demonstrates fairly consistent DMX signal reading under normal operation.

However, issues arise when combining the DMX receiver with the FastLED library to control an LED strip. Both systems attempt to use overlapping or conflicting hardware resources (such as shared pins and timing-sensitive routines), which leads to interference. As a result, DMX reception becomes blocked or unreliable when FastLED is active.

Further work is needed to resolve the resource conflict and ensure stable concurrent operation of DMX input and LED output.