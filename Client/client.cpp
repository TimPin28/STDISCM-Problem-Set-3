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

std::mutex particleMutex;  // Mutex for thread-safe access to particles

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

vector<Particle> receive_particle_data(SOCKET clientSocket) {
    size_t numParticles = 0;
    vector<Particle> receivedParticles;

    // First, receive the number of particles
    int bytesReceived = recv(clientSocket, (char*)&numParticles, sizeof(numParticles), 0);
    if (bytesReceived < 0) {
        cerr << "No particles or connection closed." << endl;
        return receivedParticles;
    }

    cout << "Receiving " << numParticles << " particles" << endl;

    // Use a vector to store received data dynamically
    vector<double> data(numParticles * 3);  // Each particle has 3 values (x, y, radius)

    if (numParticles > 0) {
        // Then receive the particle data
        bytesReceived = recv(clientSocket, (char*)data.data(), numParticles * 3 * sizeof(double), 0);
        if (bytesReceived <= 0) {
            cerr << "Failed to receive particle data or connection closed." << endl;
            return receivedParticles;
        }

        // Process the received data
        for (size_t i = 0; i < numParticles; ++i) {
            double x = data[i * 3];
            double y = data[i * 3 + 1];
            double radius = data[i * 3 + 2];
            cout << "Particle " << i << ": x=" << x << ", y=" << y << ", radius=" << radius << endl;
            receivedParticles.push_back(Particle(x, y, 0, radius));
        }
    }

    return receivedParticles;
}

void updateParticlesFromServer(SOCKET clientSocket, std::vector<Particle>& particles) {
    while (true) {
        std::vector<Particle> newParticles = receive_particle_data(clientSocket);

        std::lock_guard<std::mutex> guard(particleMutex);  // Lock the mutex for safe access to particles
        particles = newParticles;  // Update the shared particles vector
    }
}

void sendSpriteData(SOCKET clientSocket, const sf::Sprite& sprite) {
    struct SpriteData {
        float x, y;
    };

    while (true) {
        SpriteData data;
        data.x = sprite.getPosition().x;
        data.y = sprite.getPosition().y;

        send(clientSocket, (char*)&data, sizeof(data), 0);
    }
}

void receiveSpriteData(SOCKET clientSocket, sf::Sprite& sprite1, sf::Sprite& sprite2) {
    struct SpriteData {
        float x, y;
    };

    // Data for sprite1
    SpriteData data1;

    // Receive data for sprite1
    int bytesReceived1 = recv(clientSocket, (char*)&data1, sizeof(data1), 0);

    if (bytesReceived1 == sizeof(data1)) {
        // Update sprite1's position
        sprite1.setPosition(data1.x, data1.y);
        //print sprite1 position
        std::cout << "Sprite1 position: " << data1.x << ", " << data1.y << std::endl;
    }
    else if (bytesReceived1 == 0) {
        // Client disconnected
        std::cout << "Client disconnected." << std::endl;
    }
    else {
        // Error or incomplete data received
        std::cerr << "Error receiving sprite data for sprite1." << std::endl;
    }

    // Data for sprite2
    SpriteData data2;

    // Receive data for sprite2
    int bytesReceived2 = recv(clientSocket, (char*)&data2, sizeof(data2), 0);

    if (bytesReceived2 == sizeof(data2)) {
        // Update sprite2's position
        sprite2.setPosition(data2.x, data2.y);
        //print sprite2 position
        std::cout << "Sprite2 position: " << data2.x << ", " << data2.y << std::endl;
    }
    else if (bytesReceived2 == 0) {
        // Client disconnected
        std::cout << "Client disconnected." << std::endl;
    }
    else {
        // Error or incomplete data received
        std::cerr << "Error receiving sprite data for sprite2." << std::endl;
    }
}

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }

    // Create socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed." << endl;
        WSACleanup();
        return 1;
    }

    // Connect to the server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("172.20.10.7");  // Server IP address
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

    // Initialize sprites
    sf::Sprite sprite1;
    sf::Sprite sprite2;
    sf::Sprite sprite3;

    sprite1.setTexture(spriteTexture);
    sprite2.setTexture(spriteTexture);
    sprite3.setTexture(spriteTexture);

    sprite1.setPosition(windowSize.x / 2.f, windowSize.y / 2.f); // Starting position
    sprite2.setPosition(-10000, -10000);
    sprite3.setPosition(-10000, -10000);

    // Scale the sprites
    sf::Vector2u textureSize = spriteTexture.getSize();
    float desiredWidth = 1.f; // Set width
    float scale = desiredWidth / textureSize.x;
    sprite1.setScale(scale, scale);
    sprite2.setScale(scale, scale);
    sprite3.setScale(scale, scale);

    sf::View explorerView(sf::FloatRect(0, 0, 33.f, 19.f));
    explorerView.setCenter(windowSize.x / 2.f, windowSize.y / 2.f);

    sf::View uiView(sf::FloatRect(0, 0, windowSize.x, windowSize.y));

    std::vector<Particle> particles;
    std::thread listenerThread(updateParticlesFromServer, clientSocket, std::ref(particles));
    listenerThread.detach();  // Detach the thread

    std::thread spriteSendThread([&]() { sendSpriteData(clientSocket, sprite1); });
    spriteSendThread.detach();

    std::thread spriteReceiveThread([&]() {
        while (true) {
			receiveSpriteData(clientSocket, sprite2, sprite3);
		}
	});
    spriteReceiveThread.detach();

    while (window.isOpen()) {
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
                    if (sprite1.getPosition().y > 0.00) {
                        sprite1.move(0, -moveSpeed); // Move up
                    }
                }
                else if (event.key.code == sf::Keyboard::S || event.key.code == sf::Keyboard::Down) {
                    if (sprite1.getPosition().y < 720.00) {
                        sprite1.move(0, moveSpeed); // Move down                 
                    }
                }
                else if (event.key.code == sf::Keyboard::A || event.key.code == sf::Keyboard::Left) {
                    if (sprite1.getPosition().x > 0.00) {
                        sprite1.move(-moveSpeed, 0); // Move left 
                    }
                }
                else if (event.key.code == sf::Keyboard::D || event.key.code == sf::Keyboard::Right) {
                    if (sprite1.getPosition().x < 1280.00) {
                        sprite1.move(moveSpeed, 0); // Move right
                    }
                }
            }
        }

        // Adjust the view to center on the sprite's position
        sf::Vector2f spritePosition = sprite1.getPosition();
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

        // Access shared particles data safely
        {
            std::lock_guard<std::mutex> guard(particleMutex);
            //Draw particles
            for (const auto& particle : particles) {
                sf::CircleShape shape(particle.radius);
                shape.setFillColor(sf::Color::Green);
                shape.setPosition(static_cast<float>(particle.x - particle.radius), static_cast<float>(particle.y - particle.radius));
                window.draw(shape);
            }
        }
        
        window.draw(sprite1);
        window.draw(sprite2);
        window.draw(sprite3);

        // Draw the FPS counter in a fixed position
        window.setView(uiView);
        window.draw(fpsText); // Draw the FPS counter on the window
        window.display();
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}