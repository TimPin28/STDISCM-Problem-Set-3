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


using namespace std;

#pragma comment(lib, "ws2_32.lib")
constexpr int BUFFER_SIZE = 1024;

std::atomic<int> nextParticleIndex(0); // Global counter for the next particle to update
std::condition_variable cv;
std::mutex cv_m;
bool ready = false; // Flag to signal threads to start processing
bool done = false;  // Flag to indicate processing is done for the current frame
bool explorerMode = false; // Flag to enable explorer mode

class Particle {
public:
    double x, y; // Position
    double radius; // Radius

    Particle(double x, double y, double angle, double radius)
        : x(x), y(y), radius(radius) {
        // Convert angle to radians and calculate velocity components
        /*double rad = angle * (M_PI / 180.0);
        vx = velocity * cos(rad);
        vy = -velocity * sin(rad);*/
    }

    // Default constructor
    Particle() : x(0), y(0), radius(0) {}

    // Deserialize the byte stream into an object
    static Particle deserialize(const std::vector<char>& data) {
        Particle particle;
        memcpy(&particle, data.data(), sizeof(particle));
        return particle;
    }
};

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

// Function to receive particles over TCP
std::vector<Particle> receive_particles(SOCKET clientSocket) {
    std::vector<Particle> particles;

    // First, receive the number of particles
    int numParticles = 0;
    int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&numParticles), sizeof(numParticles), 0);

    // print bytes received
    std::cout << "Bytes received: " << bytesReceived << std::endl;

    if (bytesReceived <= 0) {
        // Handle error or closed connection
        return particles;
    }

    return particles;
}

// Function to receive a serialized object over TCP
Particle receive_object(SOCKET socket_fd) {
    std::vector<char> buffer(sizeof(Particle));
    recv(socket_fd, buffer.data(), buffer.size(), 0);
    return Particle::deserialize(buffer);
}

vector<Particle> receive_particle_data(SOCKET clientSocket) {
    size_t numParticles;
    vector<Particle> test;
    double data[10][3];  // The array to hold received data

    // First, receive the number of particles
    recv(clientSocket, (char*)&numParticles, sizeof(numParticles), 0);

    if (numParticles > 0) {
        // Then receive the particle data
        recv(clientSocket, (char*)data, sizeof(data), 0);

        // Process the received data
        for (size_t i = 0; i < numParticles; ++i) {
            std::cout << "Particle " << i << ": x=" << data[i][0]
                << ", y=" << data[i][1] << ", radius=" << data[i][2] << std::endl;
            test.push_back(Particle(data[i][0], data[i][1], 0, data[i][2]));
        }
    }
    
    

    return test;
}


int main() {
    //// Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }

    //// Create socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed." << endl;
        WSACleanup();
        return 1;
    }

    // Connect to the server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("10.147.17.27");  // Server IP address
    serverAddr.sin_port = htons(12345);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Connection failed." << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server." << endl;

    // Initialize window size
    sf::Vector2u windowSize(1280, 720);
    sf::RenderWindow window(sf::VideoMode(windowSize.x, windowSize.y), "Particle Simulator");

    size_t threadCount = std::thread::hardware_concurrency(); // Use the number of concurrent threads supported by the hardware

    std::vector<std::thread> threads;
    std::vector<Particle> particles;

    // Print the received particles
    /*for (const auto& particle : particles) {
        std::cout << "Particle: x=" << particle.x << ", y=" << particle.y << ", radius=" << particle.radius << std::endl;
    }*/

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

    sf::View explorerView(sf::FloatRect(0, 0, 33.f, 19.f));
    explorerView.setCenter(windowSize.x / 2.f, windowSize.y / 2.f);

    sf::View uiView(sf::FloatRect(0, 0, windowSize.x, windowSize.y));

    while (window.isOpen()) {
        // Receive particles from the server in new delta time
        // particles = receive_particles(clientSocket);

        sf::Event event;
        while (window.pollEvent(event)) {
            gui.handleEvent(event);

            if (event.type == sf::Event::Closed) {
                window.close();
            }

            // Handle sprite movement if in explorer mode
            if (event.type == sf::Event::KeyPressed) {
                float moveSpeed = 2.0f; // Adjust speed
                if (event.key.code == sf::Keyboard::W || event.key.code == sf::Keyboard::Up) {
                    if (sprite.getPosition().y > 0.00) {
                        sprite.move(0, -moveSpeed); // Move up
                    }
                }
                else if (event.key.code == sf::Keyboard::S || event.key.code == sf::Keyboard::Down) {
                    if (sprite.getPosition().y < 720.00) {
                        sprite.move(0, moveSpeed); // Move down                 
                    }
                }
                else if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::Left) {
                    if (sprite.getPosition().x > 0.00) {
                        sprite.move(-moveSpeed, 0); // Move left 
                    }
                }
                else if (event.key.code == sf::Keyboard::D || event.key.code == sf::Keyboard::Right) {
                    if (sprite.getPosition().x < 1280.00) {
                        sprite.move(moveSpeed, 0); // Move right
                    }
                }
            }
        }

        /*Particle receivedParticle = receive_object(clientSocket);
        std::cout << "Particle: x=" << receivedParticle.x << ", y=" << receivedParticle.y << ", radius=" << receivedParticle.radius << std::endl;*/

        particles = receive_particle_data(clientSocket);

        // Adjust the view to center on the sprite's position
        sf::Vector2f spritePosition = sprite.getPosition();
        explorerView.setCenter(spritePosition);
        window.setView(explorerView);
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

        //Draw particles
        for (const auto& particle : particles) {
            sf::CircleShape shape(particle.radius);
            shape.setFillColor(sf::Color::Green);
            shape.setPosition(static_cast<float>(particle.x - particle.radius), static_cast<float>(particle.y - particle.radius));
            window.draw(shape);
        }

        window.draw(sprite); // Draw the sprite in the window

        // Draw the FPS counter in a fixed position
        window.setView(uiView);
        window.draw(fpsText); // Draw the FPS counter on the window
        window.display();
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}

//initialize_network;
//
//while (running) {
//    handle_user_input  // Gather user commands and send them to the server
//    update_network     // Send and receive network messages
//    process_server_messages // Process messages received from the server
//    render            // Render the updated simulation state
//}
//
//cleanup_network