# STDISCM-PROBLEM-SET-3 ([STILL NEEDS TO BE REVISED])
 
# Distributed Particle Simulator

## Introduction

This Particle Simulator is a graphical application designed to simulate the behavior of particles within a confined space. It uses SFML for rendering graphics and TGUI for the user interface, providing an interactive environment where users can add particles and observe particle dynamics including collisions and reflections. Building upon the Particle Simulator: Explorer Mode, this expansion utilizes a distributed approach that splits the Developer and Explorer Mode into separate applications. Users may create up to 3 instances of the Explorer application. This uses the Winsock2 library to handle the network.

## Requirements

- C++17 compiler
- SFML (Simple and Fast Multimedia Library) 2.5 or newer
- TGUI (Texus' Graphical User Interface) 0.9 or newer
- Windows OS (due to the use of Winsock2 for networking).
- Network connectivity between the Developer and Explorer application.

## Installation

To compile and run the Distributed Particle Simulator, follow these steps:

1. **Clone the Repository:** Clone this repository to your local machine using `git clone`, or download the source code as a ZIP file and extract it.

2. **Winsock2 Library:** Ensure that the Winsock2 library is accessible to your compiler. This project uses winsock2.h, which is typically available on Windows systems.

3. **Configuration:** Configure the IP address and port in the Explorer application to match the Developer's listening address and port. Ensure that both the Developer application and Explorer application are connected to the same network.

4. **Compilation**: Compile both the Developer and Explorer applications using your C++ compiler. For example, with g++:

```bash
g++ main.cpp -o developer -lws2_32
g++ client.cpp -o explorer -lws2_32
``` 
5. **Run the Application:** Once compiled, you can run the application.

## Usage

After launching the Distributed Particle Simulator, you will be presented with a graphical interface that allows you to interact with the simulation:

### Developer Application: Adding Particles
- The program will initially start in Developer Mode where users may add bouncing particles into the canvas.
- Individual Particle Addition: Use the input fields to specify properties for individual particles (position, velocity, angle) and click "Add Particle".
- Batch Particle Addition: The application supports adding particles in batches through several forms:
  - Form 1: Specify a start and end position, and the application will distribute particles evenly along the line connecting these points.
  - Form 2: Define an angle range and the application will distribute particles evenly across the specified angular direction from a central point.
  - Form 3: Input a range of velocities, and particles will be added with velocities distributed within this range.

### Explorer Application: Traversing with the Sprite
- The program will ask first where to spawn the Sprite in the window.
- The user can traverse through the canvas with the use of the W,A,S,D keys or arrow keys.

### Simulation Control
- Particles move automatically and interact with boundaries. You can only dynamically add particles in Developer Application.
- Use the checkbox found above to hide/show the input fields.
- An FPS counter will always be displayed on the upper-left corner of the screen.

## Authors
* **Go, Eldrich**
* **Pangan, John Paul**
* **Pinawin, Timothy**
* **Yu, Ethan**
