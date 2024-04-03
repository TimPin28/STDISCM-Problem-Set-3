#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <SFML/Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <TGUI/Backend/SFML-Graphics.hpp>
#include <TGUI/Widget.hpp>
#include <TGUI/String.hpp>
#include <winsock2.h>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <queue>
#include <mutex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#pragma comment(lib, "ws2_32.lib")

std::atomic<int> nextParticleIndex(0); // Global counter for the next particle to update
std::condition_variable cv;
std::mutex cv_m;
bool ready = false; // Flag to signal threads to start processing
bool done = false;  // Flag to indicate processing is done for the current frame
bool explorerMode = false; // Flag to enable explorer mode

class Particle {
public:
    double x, y; // Position
    double vx, vy; // Velocity
    double radius; // Radius

    Particle(double x, double y, double angle, double velocity, double radius)
        : x(x), y(y), radius(radius) {
        // Convert angle to radians and calculate velocity components
        double rad = angle * (M_PI / 180.0);
        vx = velocity * cos(rad);
        vy = -velocity * sin(rad);
    }

    Particle(double x, double y, double radius) {
        x = x;
        y = y;
        radius = radius;
    }
    Particle() {
		x = 0;
		y = 0;
		radius = 0;
	}

    // Serialize the object into a byte stream
    std::vector<char> serialize() const {
        std::vector<char> data(sizeof(this));
        memcpy(data.data(), this, sizeof(this));
        return data;
    }

    void updatePosition(double deltaTime, double simWidth, double simHeight) {
        double nextX = x + vx * deltaTime;
        double nextY = y + vy * deltaTime;

        // Boundary collision
        if (nextX - radius < 0 || nextX + radius > simWidth) vx = -vx;
        if (nextY - radius < 0 || nextY + radius > simHeight) vy = -vy;

        // Update position
        x += vx * deltaTime;
        y += vy * deltaTime;
    }
};

void updateParticleWorker(std::vector<Particle>& particles, double deltaTime, double simWidth, double simHeight, SOCKET serverSocket) {
    while (!done) {
        std::unique_lock<std::mutex> lk(cv_m);
        cv.wait(lk, [] { return ready || done; });
        lk.unlock();

        while (true) {
            int index = nextParticleIndex.fetch_add(1);
            if (index >= particles.size()) {
                break;
            }
            particles[index].updatePosition(deltaTime, simWidth, simHeight);           
        }
    }
}

void drawGrid(sf::RenderWindow& window, int gridSize) {
    int width = window.getSize().x;
    int height = window.getSize().y;
    sf::Color gridColor = sf::Color(200, 200, 200, 100); // Light grey color for the grid
    
    sf::RectangleShape rectangle;
    rectangle.setSize(sf::Vector2f(1280, 720));
    rectangle.setPosition(0.f, 0.f);
    rectangle.setFillColor(sf::Color(186, 224, 230, 100));
    window.draw(rectangle);

    // Draw vertical lines
    for (int x = 0; x <= width; x += gridSize) {
        sf::Vertex line[] = {
            sf::Vertex(sf::Vector2f(static_cast<float>(x), 0.f), gridColor),
            sf::Vertex(sf::Vector2f(static_cast<float>(x), static_cast<float>(height)), gridColor)
        };
        window.draw(line, 2, sf::Lines);
    }

    // Draw horizontal lines
    for (int y = 0; y <= height; y += gridSize) {
        sf::Vertex line[] = {
            sf::Vertex(sf::Vector2f(0.f, static_cast<float>(y)), gridColor),
            sf::Vertex(sf::Vector2f(static_cast<float>(width), static_cast<float>(y)), gridColor)
        };
        window.draw(line, 2, sf::Lines);
    }
}

void startFrame() {
    nextParticleIndex.store(0); // Reset the counter for the next frame
    ready = true;
    cv.notify_all();
}

// Function to accept and identify client type
SOCKET acceptAndIdentifyClient(SOCKET serverSocket) {
    SOCKET client;
    client = accept(serverSocket, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        std::cerr << "Accept failed." << std::endl;
        return client;
    }
    std::cout << "Client connected." << std::endl;
    return client;
}

void send_particle_data(const std::vector<Particle>& particles, SOCKET serverSocket) {
    size_t numParticles = particles.size();

    // Sending each particle's x, y, and radius as a continuous array of doubles
    std::vector<double> data(numParticles * 3);

    for (size_t i = 0; i < numParticles; ++i) {
        data[i * 3] = particles[i].x;
        data[i * 3 + 1] = particles[i].y;
        data[i * 3 + 2] = particles[i].radius;
    }

    // Send the number of particles first
    send(serverSocket, (char*)&numParticles, sizeof(numParticles), 0);

    // Then send the particle data
    send(serverSocket, (char*)data.data(), data.size() * sizeof(double), 0);
}

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // Create socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return 1;
    }

    // Bind the socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(12345);

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Listen for incoming connections
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Define SOCKET variables outside the threads
    SOCKET spriteClient = INVALID_SOCKET;

    // Accept two client connections
    std::thread connectClient1([&]() {
        spriteClient = acceptAndIdentifyClient(serverSocket);
        });
    connectClient1.detach();

    // Initialize window size
    sf::Vector2u windowSize(1280, 720);
    sf::RenderWindow window(sf::VideoMode(windowSize.x, windowSize.y), "Particle Simulator");

    size_t threadCount = std::thread::hardware_concurrency(); // Use the number of concurrent threads supported by the hardware

    std::vector<std::thread> threads;
    std::vector<Particle> particles;

    double deltaTime = 1; // Time step for updating particle positions

    // Set the frame rate limit
    window.setFramerateLimit(60);

    sf::Clock clock; // Starts the clock for FPS calculation  
    sf::Clock fpsUpdateClock; // Clock to update the FPS counter every 0.5 seconds

    sf::Font font;
    if (!font.loadFromFile("OpenSans-Regular.ttf")) {
        std::cerr << "Could not load font\n";
        return -1;
    }

    sf::Text fpsText("", font, 20);
    fpsText.setFillColor(sf::Color::White);
    fpsText.setPosition(5.f, 5.f); // Position the FPS counter in the top-left corner

    tgui::Gui gui(window); // Initialize TGUI Gui object for the window

    // Check box to toggle visibility of input fields
    auto toggleCheckbox = tgui::CheckBox::create();
    toggleCheckbox->setPosition("10%", "1%");
    toggleCheckbox->setText("Toggle Input Boxes");
    gui.add(toggleCheckbox);

    auto renderer = toggleCheckbox->getRenderer();
    renderer->setTextColor(sf::Color::White);
    
    // Widgets for input fields

    // Particle Input Form 1
    auto noParticles1 = tgui::EditBox::create();
    noParticles1->setPosition("10%", "5%");
    noParticles1->setSize("18%", "4%");
    noParticles1->setDefaultText("Number of Particles");
    gui.add(noParticles1);

    auto X1PosEditBox = tgui::EditBox::create();
    X1PosEditBox->setPosition("10%", "10%");
    X1PosEditBox->setSize("18%", "4%");
    X1PosEditBox->setDefaultText("X1 Coordinate (0-1280)");
    gui.add(X1PosEditBox);

    auto Y1PosEditBox = tgui::EditBox::create();
    Y1PosEditBox->setPosition("10%", "15%");
    Y1PosEditBox->setSize("18%", "4%");
    Y1PosEditBox->setDefaultText("Y1 Coordinate (0-720)");
    gui.add(Y1PosEditBox);

    auto X2PosEditBox = tgui::EditBox::create();
    X2PosEditBox->setPosition("10%", "20%");
    X2PosEditBox->setSize("18%", "4%");
    X2PosEditBox->setDefaultText("X2 Coordinate (0-1280)");
    gui.add(X2PosEditBox);

    auto Y2PosEditBox = tgui::EditBox::create();
    Y2PosEditBox->setPosition("10%", "25%");
    Y2PosEditBox->setSize("18%", "4%");
    Y2PosEditBox->setDefaultText("Y2 Coordinate (0-720)");
    gui.add(Y2PosEditBox);

    auto addButton1 = tgui::Button::create("Add Batch Particle 1");
    addButton1->setPosition("10%", "30%"); // Adjust the percentage for layout
    addButton1->setSize("18%", "4%");
    gui.add(addButton1);

    // Particle Input Form 2
    auto noParticles2 = tgui::EditBox::create();
    noParticles2->setPosition("30%", "5%");
    noParticles2->setSize("18%", "4%");
    noParticles2->setDefaultText("Number of Particles");
    gui.add(noParticles2);

    auto startAngleEditBox = tgui::EditBox::create();
    startAngleEditBox->setPosition("30%", "10%");
    startAngleEditBox->setSize("18%", "4%");
    startAngleEditBox->setDefaultText("Start Angle (0-360)");
    gui.add(startAngleEditBox);

    auto endAngleEditBox = tgui::EditBox::create();
    endAngleEditBox->setPosition("30%", "15%");
    endAngleEditBox->setSize("18%", "4%");
    endAngleEditBox->setDefaultText("End Angle (0-360)");
    gui.add(endAngleEditBox);

    auto addButton2 = tgui::Button::create("Add Batch Particle 2");
    addButton2->setPosition("30%", "20%"); // Adjust the percentage for layout
    addButton2->setSize("18%", "4%");
    gui.add(addButton2);

    // Particle Input Form 3
    auto noParticles3 = tgui::EditBox::create();
    noParticles3->setPosition("50%", "5%");
    noParticles3->setSize("18%", "4%");
    noParticles3->setDefaultText("Number of Particles");
    gui.add(noParticles3);

    auto startVelocityEditBox = tgui::EditBox::create();
    startVelocityEditBox->setPosition("50%", "10%");
    startVelocityEditBox->setSize("18%", "4%");
    startVelocityEditBox->setDefaultText("Start Velocity (1-175)");
    gui.add(startVelocityEditBox);

    auto endVelocityEditBox = tgui::EditBox::create();
    endVelocityEditBox->setPosition("50%", "15%");
    endVelocityEditBox->setSize("18%", "4%");
    endVelocityEditBox->setDefaultText("End Velocity (1-175)");
    gui.add(endVelocityEditBox);

    auto addButton3 = tgui::Button::create("Add Batch Particle 3");
    addButton3->setPosition("50%", "20%"); // Adjust the percentage for layout
    addButton3->setSize("18%", "4%");
    gui.add(addButton3);

    // Basic Particle Input 
    auto basicX1PosEditBox = tgui::EditBox::create();
    basicX1PosEditBox->setPosition("75%", "5%");
    basicX1PosEditBox->setSize("18%", "4%");
    basicX1PosEditBox->setDefaultText("X1 Coordinate (0-1280)");
    gui.add(basicX1PosEditBox);

    auto basicY1PosEditBox = tgui::EditBox::create();
    basicY1PosEditBox->setPosition("75%", "10%");
    basicY1PosEditBox->setSize("18%", "4%");
    basicY1PosEditBox->setDefaultText("Y1 Coordinate (0-720)");
    gui.add(basicY1PosEditBox);

    auto basicAngleEditBox = tgui::EditBox::create();
    basicAngleEditBox->setPosition("75%", "15%");
    basicAngleEditBox->setSize("18%", "4%");
    basicAngleEditBox->setDefaultText("Angle Directon (0-360)");
    gui.add(basicAngleEditBox);

    auto basicVelocityEditBox = tgui::EditBox::create();
    basicVelocityEditBox->setPosition("75%", "20%");
    basicVelocityEditBox->setSize("18%", "4%");
    basicVelocityEditBox->setDefaultText("Velocity (1-175)");
    gui.add(basicVelocityEditBox);

    auto basicaddButton = tgui::Button::create("Add Particle");
    basicaddButton->setPosition("75%", "25%"); // Adjust the percentage for layout
    basicaddButton->setSize("18%", "4%");
    gui.add(basicaddButton);

    //Checkbox event handler
    toggleCheckbox->onChange([&](bool checked) {
        if (checked) {
            // Hide the input fields
            noParticles1->setVisible(false);
            X1PosEditBox->setVisible(false);
            Y1PosEditBox->setVisible(false);
            X2PosEditBox->setVisible(false);
            Y2PosEditBox->setVisible(false);
            addButton1->setVisible(false);

            noParticles2->setVisible(false);
            startAngleEditBox->setVisible(false);
            endAngleEditBox->setVisible(false);
            addButton2->setVisible(false);

            noParticles3->setVisible(false);
            startVelocityEditBox->setVisible(false);
            endVelocityEditBox->setVisible(false);
            addButton3->setVisible(false);

            basicX1PosEditBox->setVisible(false);
            basicY1PosEditBox->setVisible(false);
            basicAngleEditBox->setVisible(false);
            basicVelocityEditBox->setVisible(false);
            basicaddButton->setVisible(false);
        }
        else {
            // Show the input boxes
            noParticles1->setVisible(true);
            X1PosEditBox->setVisible(true);
            Y1PosEditBox->setVisible(true);
            X2PosEditBox->setVisible(true);
            Y2PosEditBox->setVisible(true);
            addButton1->setVisible(true);

            noParticles2->setVisible(true);
            startAngleEditBox->setVisible(true);
            endAngleEditBox->setVisible(true);
            addButton2->setVisible(true);

            noParticles3->setVisible(true);
            startVelocityEditBox->setVisible(true);
            endVelocityEditBox->setVisible(true);
            addButton3->setVisible(true);

            basicX1PosEditBox->setVisible(true);
            basicY1PosEditBox->setVisible(true);
            basicAngleEditBox->setVisible(true);
            basicVelocityEditBox->setVisible(true);
            basicaddButton->setVisible(true);
        }
        });

    // Attach an event handler to the "Add Particle" button for Form 1
    addButton1->onPress([&]() {
        try {
            int n = std::stoi(noParticles1->getText().toStdString()); // Number of particles
            float x1 = std::stof(X1PosEditBox->getText().toStdString()); // Start X coordinate
            float y1 = std::stof(Y1PosEditBox->getText().toStdString()); // Start Y coordinate
            float x2 = std::stof(X2PosEditBox->getText().toStdString()); // End X coordinate
            float y2 = std::stof(Y2PosEditBox->getText().toStdString()); // End Y coordinate

            float velocity = 20.0f; // constant velocity value
            float angle = 45.0f; // constant angle in degrees

            if (n <= 0) throw std::invalid_argument("Number of particles must be positive.");
            if (x1 < 0 || x1 > 1280) throw std::invalid_argument("X1 coordinate must be between 0 and 1280.");
            if (y1 < 0 || y1 > 720) throw std::invalid_argument("Y1 coordinate must be between 0 and 720.");
            if (x2 < 0 || x2 > 1280) throw std::invalid_argument("X2 coordinate must be between 0 and 1280.");
            if (y2 < 0 || y2 > 720) throw std::invalid_argument("Y2 coordinate must be between 0 and 720.");

            float xStep = (x2 - x1) / std::max(1, n - 1); // Calculate the x step between particles
            float yStep = (y2 - y1) / std::max(1, n - 1); // Calculate the y step between particles

            for (int i = 0; i < n; ++i) {
                float xPos = x1 + i * xStep; // Calculate the x position for each particle
                float yPos = y1 + i * yStep; // Calculate the y position for each particle

                // Add each particle to the simulation
                particles.push_back(Particle(xPos, yPos, angle, velocity, 1));// radius is 5
            }

            // Clear the edit boxes after adding particles
            noParticles1->setText("");
            X1PosEditBox->setText("");
            Y1PosEditBox->setText("");
            X2PosEditBox->setText("");
            Y2PosEditBox->setText("");
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input: " << e.what() << '\n';
        }
        catch (const std::out_of_range& e) {
            std::cerr << "Input out of range: " << e.what() << '\n';
        }
        });

    // Attach an event handler to the "Add Particle" button for Form 2
    addButton2->onPress([&]() {
        try {
            int n = std::stoi(noParticles2->getText().toStdString()); // Number of particles
            float startTheta = std::stof(startAngleEditBox->getText().toStdString()); // Start angle in degrees
            float endTheta = std::stof(endAngleEditBox->getText().toStdString()); // End angle in degrees

            // constant velocity and a starting point for all particles
            float velocity = 20.0f; // velocity
            sf::Vector2f startPoint(640, 360); // start point

            if (n <= 0) throw std::invalid_argument("Number of particles must be positive.");
            if (startTheta < 0 || startTheta > 360) throw std::invalid_argument("Start Theta must be positive and must be less than 360.");
            if (endTheta < 0 || endTheta> 360) throw std::invalid_argument("End Theta must be positive and must be less than or equal 360.");
            if (startTheta > endTheta) throw std::invalid_argument("Start Theta must be less than End Theta.");


            float angularStep = (n > 1) ? (endTheta - startTheta) / (n - 1) : 0;

            if (startTheta == 0.0f && endTheta == 360.0f) {
                angularStep = (n > 1) ? (endTheta - startTheta) / (n) : 0;
            }

            for (int i = 0; i < n; ++i) {
                float angle = startTheta + i * angularStep; // Calculate the angle for each particle
                double angleRad = angle * (M_PI / 180.0); // Convert angle from degrees to radians              

                // Add each particle to the simulation
                particles.push_back(Particle(startPoint.x, startPoint.y, angle, velocity, 1)); // radius is 5
            }

            // Clear the edit boxes after adding particles
            noParticles2->setText("");
            startAngleEditBox->setText("");
            endAngleEditBox->setText("");
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input: " << e.what() << '\n';
        }
        catch (const std::out_of_range& e) {
            std::cerr << "Input out of range: " << e.what() << '\n';

        }
        });

    // Attach an event handler to the "Add Particle" button for Form 3
    addButton3->onPress([&]() {
        try {
            int n = std::stoi(noParticles3->getText().toStdString()); // Number of particles
            float startVelocity = std::stof(startVelocityEditBox->getText().toStdString()); // Start velocity
            float endVelocity = std::stof(endVelocityEditBox->getText().toStdString()); // End velocity
            float angle = 45.0f; // constant angle in degrees
            sf::Vector2f startPoint(400, 300); // constant start point

            if (n <= 0) throw std::invalid_argument("Number of particles must be positive.");
            if (startVelocity <= 0) throw std::invalid_argument("Start Velocity must be greater than 0.");
            if (endVelocity <= 0) throw std::invalid_argument("End Velocity must be greater than 0.");
            if (startVelocity >= endVelocity) throw std::invalid_argument("Start Velocity must be less than End Velocity.");;
            if (startVelocity >= 176) throw std::invalid_argument("Start Velocity must be less than or equal 175.");
            if (endVelocity >= 176) throw std::invalid_argument("End Velocity must be less than or equal 175.");
            float velocityStep = (endVelocity - startVelocity) / std::max(1, n - 1); // Calculate the velocity step between particles

            for (int i = 0; i < n; ++i) {
                float velocity = startVelocity + i * velocityStep; // Calculate the velocity for each particle
                double angleRad = angle * (M_PI / 180.0); // Convert angle from degrees to radians

                // Add each particle to the simulation
                particles.push_back(Particle(startPoint.x, startPoint.y, angle, velocity, 1)); // radius is 5
            }

            // Clear the edit boxes after adding particles
            noParticles3->setText("");
            startVelocityEditBox->setText("");
            endVelocityEditBox->setText("");
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input: " << e.what() << '\n';
        }
        catch (const std::out_of_range& e) {
            std::cerr << "Input out of range: " << e.what() << '\n';
        }
        });

    // Attach an event handler to the "Add Particle" button for Basic Add Particle
    basicaddButton->onPress([&]() {
        try {
            float xPos = std::stof(basicX1PosEditBox->getText().toStdString()); // X coordinate
            float yPos = std::stof(basicY1PosEditBox->getText().toStdString()); // Y coordinate
            float angle = std::stof(basicAngleEditBox->getText().toStdString()); // Angle
            float velocity = std::stof(basicVelocityEditBox->getText().toStdString()); // Velocity

            if (xPos < 0 || xPos > 1280) throw std::invalid_argument("X coordinate must be between 0 and 1280.");
            if (yPos < 0 || yPos > 720) throw std::invalid_argument("Y coordinate must be between 0 and 720.");
            if (angle < 0 || angle > 360) throw std::invalid_argument("Angle must be between 0 and 360.");
            if (velocity < 0) throw std::invalid_argument("Velocity must be greater than 0.");
            if (velocity >= 176) throw std::invalid_argument("Start Velocity must be less than or equal 175.");

            // Add particle to the simulation
            particles.push_back(Particle(xPos, yPos, angle, velocity, 1)); // radius is 5

            // Clear the edit boxes after adding particles
            basicX1PosEditBox->setText("");
            basicY1PosEditBox->setText("");
            basicAngleEditBox->setText("");
            basicVelocityEditBox->setText("");
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input: " << e.what() << '\n';

        }
        catch (const std::out_of_range& e) {
            std::cerr << "Input out of range: " << e.what() << '\n';

        }
        });

    // Load sprite texture
    sf::Texture spriteTexture;
    if (!spriteTexture.loadFromFile("Image/sprite.png")) {
        // Handle error
        std::cerr << "Could not load sprite texture\n";
        return -1;
    }

    // Initialize sprite
    sf::Sprite sprite;
    sprite.setTexture(spriteTexture);
    sprite.setPosition(windowSize.x / 2.f, windowSize.y / 2.f); // Starting position

    // Scale the sprite
    sf::Vector2u textureSize = spriteTexture.getSize();
    float desiredWidth = 1.f; // Set width
    float scale = desiredWidth / textureSize.x;
    sprite.setScale(scale, scale); // Apply scaling

    // Create worker threads
    for (size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back(updateParticleWorker, std::ref(particles), deltaTime, 1280.0, 720.0, serverSocket);
    }

    sf::View uiView(sf::FloatRect(0, 0, windowSize.x, windowSize.y));

    while (window.isOpen()) {

        nextParticleIndex.store(0); // Reset the counter for the next frame

        sf::Event event;
        while (window.pollEvent(event)) {
            gui.handleEvent(event);

            if (event.type == sf::Event::Closed) {
                window.close();
            }
        }

        window.clear();

        // Draw the grid as the background
        drawGrid(window, 50);

        // Compute framerate
        float currentTime = clock.restart().asSeconds();
        float fps = 1.0f / (currentTime);

        if (fpsUpdateClock.getElapsedTime().asSeconds() >= 0.5f) {
            std::stringstream ss;
            ss.precision(0); // Set precision to zero
            ss << "FPS: " << std::fixed << fps;
            fpsText.setString(ss.str());
            fpsUpdateClock.restart(); // Reset the fpsUpdateClock for the next 0.5-second interval
        }

        startFrame(); // Signal threads to start processing
        ready = false; // Threads are now processing

        if (spriteClient != INVALID_SOCKET && !particles.empty()) {
            send_particle_data(particles, spriteClient);          
		}

        //Draw particles
        for (const auto& particle : particles) {
            sf::CircleShape shape(particle.radius);
            shape.setFillColor(sf::Color::Green);
            shape.setPosition(static_cast<float>(particle.x - particle.radius), static_cast<float>(particle.y - particle.radius));
            window.draw(shape);
        }

        sf::Vector2u textureSize = spriteTexture.getSize();
        float desiredWidth = 5.f; // Set width
        float scale = desiredWidth / textureSize.x;
        sprite.setScale(scale, scale); // Apply scaling
        gui.draw(); // Draw the GUI
   
        window.draw(sprite); // Draw the sprite in the window
        // Draw the FPS counter in a fixed position
        window.setView(uiView);
        window.draw(fpsText); // Draw the FPS counter on the window
        window.display();
    }

    // Cleanup: Signal threads to exit and join them
    done = true;
    ready = true; // Ensure threads are not stuck waiting
    cv.notify_all();

    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}