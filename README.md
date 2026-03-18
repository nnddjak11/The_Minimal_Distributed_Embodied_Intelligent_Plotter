Read in 中文（[Chinese](https://github.com)）

# The_Minimal_Distributed_Embodied_Intelligent_Plotter(deployed--demo)
## .Main SOC:RK3566 4+32g,x96x6 tvbox ,btw station-m2 armbian OS,which deploy voice tflite and contour tflite.<br>.1st mcu:ESP32 N16R8 to transport udp message,and using INMP441 to achieve recording.<br>.2nd mcu:PICO W(RP2040+ESP8285)is used to control 2 28BYJ48 steppers by ULN2003 driver board,to control the pen drawing.
### I gave up to use its(PICO W) ESP8285 cause it can not receive more than 30bytes,and u must add lots of codes to control its AT.


Language:C and Python3.10.<br>
TensorFlow and TFlite runtime -v:2.14.0(numpy must under 2.x during the deploying in the end side.<br>
Deployment Env:Ubuntu arm 22.04 for armbian based on Podman<br>


==Tag:Emodied intellignece,Machine learning/Deep learning,MFCC,1DCNN,CNN,Distributed system design,OpenCV,ARM Linux development,ESP32,PICO W==

*voice.ipynb:*<br>
*contour.ipynb:*<br>
*pico_w->main.c and CMakeFiles.txt:*<br>
*ESP32 N16R8->main.c:*<br>

**Docs url：（[Feishu——docs](https://github.com)）**
