Read in 中文（[Chinese](https://github.com/nnddjak11/The_Minimal_Distributed_Embodied_Intelligent_Plotter/blob/main/README_CN.md)）
# The Minimal Distributed Embodied Intelligent Plotter (Deployed - Demo Version)
## Core Hardware & Functions
- **Main SOC**：RK3566 (4GB RAM + 32GB Storage), based on x96x6 TV box with Station-M2 version Armbian OS, deployed with Voice Recognition TFLite Model and Contour Extraction TFLite Model.
- **1st Microcontroller (MCU)**：ESP32 N16R8, responsible for UDP message transmission, and implements recording function via INMP441 microphone module.
- **2nd Microcontroller (MCU)**：PICO W (RP2040 + ESP8285), controls 2 x 28BYJ48 stepper motors through ULN2003 driver board to realize the movement control of the drawing pen.

### Hardware Selection Notes
Abandoned the built-in ESP8285 wireless module of PICO W for the following reasons:
- Maximum single data reception limit is 30 bytes;
- Requires extensive code to control its AT command set, leading to high development costs.

## Tech Stack Information
- **Development Languages**：C, Python 3.10
- **Framework/Runtime Version**：TensorFlow & TFLite Runtime v2.14.0 (numpy version must be below 2.x during terminal deployment)
- **Deployment Environment**：Armbian system based on Podman container, underlying is Ubuntu ARM 22.04

## Tags
`Embodied Intelligence` `Machine Learning/Deep Learning` `MFCC` `1DCNN` `CNN` `Distributed System Design` `OpenCV` `ARM Linux Development` `ESP32` `PICO W`

## Core File Description
| Hardware/Function       | Corresponding Files                  | Description                     |
|------------------------|-------------------------------------|---------------------------------|
| Voice Processing       | `voice.ipynb`                       | Jupyter Notebook for voice recognition |
| Contour Extraction     | `contour.ipynb`                     | Jupyter Notebook for image contour extraction |
| PICO W Development Board | `main.c`, `CMakeFiles.txt`        | Main program file, compilation configuration file |
| ESP32 N16R8 Development Board | `main.c`                       | Main program file               |

## Document URL
[Feishu Document](https://github.com)

