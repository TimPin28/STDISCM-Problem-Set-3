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

#define SPRITE_PORT 12345
#define PARTICLE_PORT 12346
#define SERVER_IP "10.52.1.246"

using namespace std;

#pragma comment(lib, "ws2_32.lib")
constexpr int BUFFER_SIZE = 1024;

std::mutex particleMutex;  // Mutex for thread-safe access to particles

struct SpriteData {
    float x, y;
};

class Particle {
public:
    double x, y; // Position
    double radius; // Radius

    Particle(double x, double y, double angle, double radius)
        : x(x), y(y), radius(radius) {
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

void receive_particle_data(SOCKET clientSocket, std::vector<Particle>& particles) {
    while (true) {
        size_t numParticles = 0;
        vector<Particle> receivedParticles;

        // First, receive the number of particles
        int bytesReceived = recv(clientSocket, (char*)&numParticles, sizeof(numParticles), 0);
        if (bytesReceived < 0) {
            cerr << "No particles or connection closed." << endl;
        }

        // Use a vector to store received data dynamically
        vector<double> data(numParticles * 2);  // Each particle has 3 values (x, y, radius)

        // Then receive the particle data
        bytesReceived = recv(clientSocket, (char*)data.data(), numParticles * 2 * sizeof(double), 0);
        if (bytesReceived < 0) {
            cerr << "Failed to receive particle data or connection closed." << endl;
        }

        // Process the received data
        for (size_t i = 0; i < numParticles; ++i) {
            double x = data[i * 2];
            double y = data[i * 2 + 1];
            receivedParticles.push_back(Particle(x, y, 0, 1));
        }

        {
            lock_guard<mutex> guard(particleMutex);
            particles = move(receivedParticles);
        }
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
    
    while (true) {
        // Data for sprite1
        SpriteData data1;

        // Receive data for sprite1
        int bytesReceived1 = recv(clientSocket, (char*)&data1, sizeof(data1), 0);

        if (bytesReceived1 == sizeof(data1)) {
            // Update sprite1's position
            sprite1.setPosition(data1.x, data1.y);
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
}

int main() {
    float spawnX = 0;
    float spawnY = 0;
    bool spawnCheck = false;

    while (!spawnCheck) {
        cout << "Enter the spawn position X (Must be in the range of 1 - 1279): ";
        cin >> spawnX;
        cout << "Enter the spawn position Y (Must be in the range of 1 - 719): ";
        cin >> spawnY;

        //input validation
        if (spawnX <= 0 || spawnX >= 1280 || spawnY <= 0 || spawnY >= 720) {
			cerr << "Invalid spawn position." << endl;
		}
        else {
			spawnCheck = true;
		}
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }

    // Create sprite socket
    SOCKET clientSpriteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSpriteSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed for sprite." << endl;
        WSACleanup();
        return 1;
    }

    // Create Particle socket
    SOCKET clientParticleSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientParticleSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed for particles." << endl;
        WSACleanup();
        return 1;
    }

    // Server address for sprite connection
    sockaddr_in serverSpriteAddr;
    serverSpriteAddr.sin_family = AF_INET;
    serverSpriteAddr.sin_addr.s_addr = inet_addr(SERVER_IP);  // Server IP address
    serverSpriteAddr.sin_port = htons(SPRITE_PORT);

    if (connect(clientSpriteSocket, reinterpret_cast<sockaddr*>(&serverSpriteAddr), sizeof(serverSpriteAddr)) == SOCKET_ERROR) {
        cerr << "Connection failed." << WSAGetLastError() << endl;
        closesocket(clientSpriteSocket);
        WSACleanup();
        return 1;
    }

    // Server address for particle connection
    sockaddr_in serverParticleAddr;
    serverParticleAddr.sin_family = AF_INET;
    serverParticleAddr.sin_addr.s_addr = inet_addr(SERVER_IP);  // Server IP address
    serverParticleAddr.sin_port = htons(PARTICLE_PORT);

    if (connect(clientParticleSocket, reinterpret_cast<sockaddr*>(&serverParticleAddr), sizeof(serverParticleAddr)) == SOCKET_ERROR) {
        cerr << "Connection failed." << WSAGetLastError() << endl;
        closesocket(clientParticleSocket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server." << endl;

    // Initialize window size
    sf::Vector2u windowSize(1280, 720);
    sf::RenderWindow window(sf::VideoMode(windowSize.x, windowSize.y), "Particle Simulator");

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

    sprite1.setPosition(spawnX, spawnY); // Starting position
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
    std::thread listenerThread(receive_particle_data, clientParticleSocket, std::ref(particles));
    listenerThread.detach();  // Detach the thread

    std::thread spriteReceiveThread(receiveSpriteData, clientSpriteSocket, std::ref(sprite2), std::ref(sprite3));
    spriteReceiveThread.detach(); // Detach the thread

    std::thread sendSpriteThread(sendSpriteData, clientSpriteSocket, std::ref(sprite1));
    sendSpriteThread.detach(); // Detach the thread

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

        for (const auto& particle : particles) {
            sf::CircleShape shape(particle.radius);
            shape.setFillColor(sf::Color::Green);
            shape.setPosition(static_cast<float>(particle.x - particle.radius), static_cast<float>(particle.y - particle.radius));
            window.draw(shape);
        }

        window.draw(sprite1);
        window.draw(sprite2);
        window.draw(sprite3);

        // Draw the FPS counter in a fixed position
        window.setView(uiView);
        window.draw(fpsText); // Draw the FPS counter on the window
        window.display();
    }

    closesocket(clientParticleSocket);
    closesocket(clientSpriteSocket);
    WSACleanup();
    return 0;
}